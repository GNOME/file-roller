/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2007, 2008 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-error.h"
#include "fr-process.h"
#include "gio-utils.h"
#include "glib-utils.h"


#define MAX_CHUNK_LEN		 (NCARGS * 2 / 3) /* Max command line length */
#define LIST_LENGTH_TO_USE_FILE	 10
#ifndef NCARGS
  #define NCARGS		 _POSIX_ARG_MAX
#endif


/* -- DroppedItemsData -- */


typedef struct {
	FrCommand           *command;
	GList               *item_list;
	char                *base_dir;
	char                *dest_dir;
	gboolean             update;
	char                *password;
	gboolean             encrypt_header;
	FrCompression        compression;
	guint                volume_size;
	GCancellable        *cancellable;
	GAsyncReadyCallback  callback;
	gpointer             user_data;
} DroppedItemsData;


static DroppedItemsData *
dropped_items_data_new (FrCommand           *command,
			GList               *item_list,
			const char          *base_dir,
			const char          *dest_dir,
			gboolean             update,
			const char          *password,
			gboolean             encrypt_header,
			FrCompression        compression,
			guint                volume_size,
			GCancellable        *cancellable,
			GAsyncReadyCallback  callback,
			gpointer             user_data)
{
	DroppedItemsData *data;

	data = g_new0 (DroppedItemsData, 1);
	data->command = command;
	data->item_list = _g_string_list_dup (item_list);
	if (base_dir != NULL)
		data->base_dir = g_strdup (base_dir);
	if (dest_dir != NULL)
		data->dest_dir = g_strdup (dest_dir);
	data->update = update;
	if (password != NULL)
		data->password = g_strdup (password);
	data->encrypt_header = encrypt_header;
	data->compression = compression;
	data->volume_size = volume_size;
	data->cancellable = _g_object_ref (cancellable);
	data->callback = callback;
	data->user_data = user_data;

	return data;
}


static void
dropped_items_data_free (DroppedItemsData *data)
{
	if (data == NULL)
		return;
	_g_string_list_free (data->item_list);
	g_free (data->base_dir);
	g_free (data->dest_dir);
	g_free (data->password);
	_g_object_unref (data->cancellable);
	g_free (data);
}


/* -- XferData -- */


typedef struct {
	FrArchive          *archive;
	char               *uri;
	FrAction            action;
	GList              *file_list;
	char               *base_uri;
	char               *dest_dir;
	gboolean            update;
	gboolean            recursive;
	char               *tmp_dir;
	guint               source_id;
	char               *password;
	gboolean            encrypt_header;
	FrCompression       compression;
	guint               volume_size;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
} XferData;


static void
xfer_data_free (XferData *data)
{
	if (data == NULL)
		return;

	g_free (data->uri);
	g_free (data->password);
	_g_string_list_free (data->file_list);
	g_free (data->base_uri);
	g_free (data->dest_dir);
	g_free (data->tmp_dir);
	_g_object_unref (data->cancellable);
	_g_object_unref (data->result);
	g_free (data);
}


/* -- FrCommand -- */


G_DEFINE_TYPE (FrCommand, fr_command, FR_TYPE_ARCHIVE)


/* Properties */
enum {
        PROP_0,
        PROP_PROCESS,
        PROP_FILENAME
};


struct _FrCommandPrivate {
	GFile            *local_copy;
	gboolean          is_remote;
	char             *temp_dir;
	gboolean          continue_adding_dropped_items;
	DroppedItemsData *dropped_items_data;
	char             *temp_extraction_dir;
	gboolean          remote_extraction;
};


static void
_fr_command_remove_temp_work_dir (FrCommand *self)
{
	if (self->priv->temp_dir == NULL)
		return;
	_g_path_remove_directory (self->priv->temp_dir);
	g_free (self->priv->temp_dir);
	self->priv->temp_dir = NULL;
}


static const char *
_fr_command_get_temp_work_dir (FrCommand *self)
{
	_fr_command_remove_temp_work_dir (self);
	self->priv->temp_dir = _g_path_get_temp_work_dir (NULL);
	return self->priv->temp_dir;
}


/* -- copy_archive_to_remote_location -- */


static void
copy_archive_to_remote_location_done (GError   *error,
				      gpointer  user_data)
{
	XferData *xfer_data = user_data;

	if (error != NULL)
		g_simple_async_result_set_from_error (xfer_data->result, error);
	g_simple_async_result_complete_in_idle (xfer_data->result);

	xfer_data_free (xfer_data);
}


static void
copy_archive_to_remote_location_progress (goffset   current_file,
					  goffset   total_files,
					  GFile    *source,
					  GFile    *destination,
					  goffset   current_num_bytes,
					  goffset   total_num_bytes,
					  gpointer  user_data)
{
	XferData *xfer_data = user_data;

	fr_archive_progress (xfer_data->archive, (double) current_num_bytes / total_num_bytes);
}


static void
copy_archive_to_remote_location (FrArchive          *archive,
				 GSimpleAsyncResult *result,
				 GCancellable       *cancellable)
{
	XferData *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (archive);
	xfer_data->result = _g_object_ref (result);
	xfer_data->cancellable = _g_object_ref (cancellable);

	fr_archive_action_started (archive, FR_ACTION_SAVING_REMOTE_ARCHIVE);

	g_copy_file_async (FR_COMMAND (xfer_data->archive)->priv->local_copy,
			   fr_archive_get_file (xfer_data->archive),
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   xfer_data->cancellable,
			   copy_archive_to_remote_location_progress,
			   xfer_data,
			   copy_archive_to_remote_location_done,
			   xfer_data);
}


/* -- copy_extracted_files_to_destination -- */


static void
move_here (FrArchive    *archive,
	   GCancellable *cancellable)
{
	const char *extraction_destination;
	char       *content_uri;
	char       *parent;
	char       *parent_parent;
	char       *new_content_uri;
	GFile      *source, *destination, *parent_file;
	GError     *error = NULL;

	extraction_destination = fr_archive_get_last_extraction_destination (archive);
	content_uri = _g_uri_get_dir_content_if_unique (extraction_destination);
	if (content_uri == NULL)
		return;

	parent = _g_path_remove_level (content_uri);

	if (_g_uri_cmp (parent, extraction_destination) == 0) {
		char *new_uri;

		new_uri = _g_uri_create_alternative_for_uri (extraction_destination);

		source = g_file_new_for_uri (extraction_destination);
		destination = g_file_new_for_uri (new_uri);
		if (! g_file_move (source, destination, 0, NULL, NULL, NULL, &error)) {
			g_warning ("could not rename %s to %s: %s", extraction_destination, new_uri, error->message);
			g_clear_error (&error);
		}
		g_object_unref (source);
		g_object_unref (destination);

		fr_archive_set_last_extraction_destination (archive, new_uri);

		g_free (parent);

		content_uri = _g_uri_get_dir_content_if_unique (new_uri);
		if (content_uri == NULL)
			return;

		parent = _g_path_remove_level (content_uri);
	}

	parent_parent = _g_path_remove_level (parent);
	new_content_uri = _g_uri_create_alternative (parent_parent, _g_path_get_file_name (content_uri));

	source = g_file_new_for_uri (content_uri);
	destination = g_file_new_for_uri (new_content_uri);
	if (! g_file_move (source, destination, 0, NULL, NULL, NULL, &error)) {
		g_warning ("could not rename %s to %s: %s", content_uri, new_content_uri, error->message);
		g_clear_error (&error);
	}

	parent_file = g_file_new_for_uri (parent);
	if (! g_file_delete (parent_file, cancellable, &error)) {
		g_warning ("could not remove directory %s: %s", parent, error->message);
		g_clear_error (&error);
	}
	g_object_unref (parent_file);

	fr_archive_set_last_extraction_destination (archive, new_content_uri);

	g_free (parent_parent);
	g_free (parent);
	g_free (content_uri);
}


static void
copy_extracted_files_done (GError   *error,
			   gpointer  user_data)
{
	XferData  *xfer_data = user_data;
	FrCommand *self = FR_COMMAND (xfer_data->archive);

	if (error != NULL)
		g_simple_async_result_set_from_error (xfer_data->result, error);

	_g_path_remove_directory (self->priv->temp_extraction_dir);
	g_free (self->priv->temp_extraction_dir);
	self->priv->temp_extraction_dir = NULL;

	if ((error == NULL) && (xfer_data->archive->extract_here))
		move_here (xfer_data->archive, xfer_data->cancellable);

	g_simple_async_result_complete_in_idle (xfer_data->result);

	xfer_data_free (xfer_data);
}


static void
copy_extracted_files_progress (goffset   current_file,
                               goffset   total_files,
                               GFile    *source,
                               GFile    *destination,
                               goffset   current_num_bytes,
                               goffset   total_num_bytes,
                               gpointer  user_data)
{
	FrArchive *archive = user_data;

	fr_archive_progress (archive, (double) current_file / (total_files + 1));
}


static void
copy_extracted_files_to_destination (FrArchive          *archive,
				     GSimpleAsyncResult *result,
				     GCancellable       *cancellable)
{
	FrCommand *self = FR_COMMAND (archive);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (archive);
	xfer_data->result = _g_object_ref (result);
	xfer_data->cancellable = _g_object_ref (cancellable);

	fr_archive_action_started (archive, FR_ACTION_COPYING_FILES_TO_REMOTE);

	g_directory_copy_async (self->priv->temp_extraction_dir,
				fr_archive_get_last_extraction_destination (archive),
				G_FILE_COPY_OVERWRITE,
				G_PRIORITY_DEFAULT,
				cancellable,
				copy_extracted_files_progress,
				archive,
				copy_extracted_files_done,
				xfer_data);
}


/* -- virtual functions  -- */


static void
fr_command_add (FrCommand  *self,
		const char *from_file,
		GList      *file_list,
		const char *base_dir,
		gboolean    update,
		gboolean    recursive)
{
	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->add (self,
						     from_file,
						     file_list,
						     base_dir,
						     update,
						     recursive);
}


static void
fr_command_delete (FrCommand  *self,
		   const char *from_file,
		   GList      *file_list)
{
	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->delete (self, from_file, file_list);
}


static void
fr_command_extract (FrCommand  *self,
		    const char *from_file,
		    GList      *file_list,
		    const char *dest_dir,
		    gboolean    overwrite,
		    gboolean    skip_older,
		    gboolean    junk_paths)
{
	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->extract (self,
							 from_file,
							 file_list,
							 dest_dir,
							 overwrite,
							 skip_older,
							 junk_paths);
}


static void
fr_command_test (FrCommand *self)
{
	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->test (self);
}


static void
fr_command_uncompress (FrCommand *self)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (self))->uncompress (self);
}


static void
fr_command_recompress (FrCommand *self)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (self))->recompress (self);
}


static gboolean
fr_command_handle_process_error (FrCommand     *self,
				 GAsyncResult  *result,
				 FrError      **error)
{
	self->process->restart = FALSE;

	if (! fr_process_execute_finish (self->process, result, error)) {
		if (g_error_matches ((*error)->gerror, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			(*error)->type = FR_ERROR_STOPPED;

		if ((*error)->type != FR_ERROR_STOPPED)
			FR_COMMAND_GET_CLASS (G_OBJECT (self))->handle_error (self, (*error));

		if (self->process->restart) {
			fr_process_restart (self->process);
			return FALSE;
		}
	}

	return TRUE;
}


/* -- _fr_command_set_process -- */


static gboolean
process_sticky_only_cb (FrProcess *process,
                        gpointer   user_data)
{
	FrArchive *archive = user_data;

	fr_archive_set_stoppable (archive, FALSE);
        return TRUE;
}


static void
_fr_command_set_process (FrCommand *self,
			 FrProcess *process)
{
	if (self->process != NULL) {
		g_signal_handlers_disconnect_matched (G_OBJECT (self->process),
						      G_SIGNAL_MATCH_DATA,
						      0,
						      0, NULL,
						      0,
						      self);
		g_object_unref (G_OBJECT (self->process));
		self->process = NULL;
	}

	if (process == NULL)
		return;

	self->process = g_object_ref (process);
	g_signal_connect (G_OBJECT (self->process),
			  "sticky_only",
			  G_CALLBACK (process_sticky_only_cb),
			  self);
}


static void
_fr_command_set_filename (FrCommand  *self,
			  const char *filename)
{
	if (self->filename != NULL) {
		g_free (self->filename);
		self->filename = NULL;
	}

	if (self->e_filename != NULL) {
		g_free (self->e_filename);
		self->e_filename = NULL;
	}

	if (filename != NULL) {
		if (! g_path_is_absolute (filename)) {
			char *current_dir;

			current_dir = g_get_current_dir ();
			self->filename = g_strconcat (current_dir,
						      "/",
						      filename,
						      NULL);
			g_free (current_dir);
		}
		else
			self->filename = g_strdup (filename);

		self->e_filename = g_shell_quote (self->filename);

		debug (DEBUG_INFO, "filename : %s\n", self->filename);
		debug (DEBUG_INFO, "e_filename : %s\n", self->e_filename);
	}

	fr_archive_working_archive (FR_ARCHIVE (self), self->filename);
}


static void
_fr_command_set_filename_from_file (FrCommand *self,
				    GFile     *file)
{
	char *filename;

	filename = g_file_get_path (file);
	_fr_command_set_filename (self, filename);

	g_free (filename);
}


static void
fr_command_set_property (GObject      *object,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
	FrCommand *self;

	self = FR_COMMAND (object);

	switch (prop_id) {
	case PROP_PROCESS:
		_fr_command_set_process (self, g_value_get_object (value));
		break;
	case PROP_FILENAME:
		_fr_command_set_filename_from_file (self, g_value_get_object (value));
		break;
	default:
		break;
	}
}


static void
fr_command_get_property (GObject    *object,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
	FrCommand *self;
	GFile     *file;

	self = FR_COMMAND (object);

	switch (prop_id) {
	case PROP_PROCESS:
		g_value_set_object (value, self->process);
		break;
	case PROP_FILENAME:
		file = g_file_new_for_path (self->filename);
		g_value_take_object (value, file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


static void
fr_command_finalize (GObject *object)
{
	FrCommand *self;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND (object));

	self = FR_COMMAND (object);

	_g_object_unref (self->process);
	_fr_command_remove_temp_work_dir (self);
	if (self->priv->dropped_items_data != NULL) {
		dropped_items_data_free (self->priv->dropped_items_data);
		self->priv->dropped_items_data = NULL;
	}
	g_free (self->priv->temp_extraction_dir);

	if (G_OBJECT_CLASS (fr_command_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_parent_class)->finalize (object);
}


/* -- load -- */


typedef struct {
	FrArchive          *archive;
	char               *password;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
} LoadData;


static void
load_data_free (LoadData *add_data)
{
	_g_object_unref (add_data->archive);
	g_free (add_data->password);
	_g_object_unref (add_data->cancellable);
	_g_object_unref (add_data->result);
	g_free (add_data);
}


static void
_fr_command_load_complete_with_error (LoadData *add_data,
				      GError   *error)
{
	g_return_if_fail (error != NULL);

	g_simple_async_result_set_from_error (add_data->result, error);
	g_simple_async_result_complete_in_idle (add_data->result);

	load_data_free (add_data);
}


static void
_fr_command_load_complete (LoadData *load_data)
{
	FrArchive *archive;

	archive = load_data->archive;

	/* order the list by name to speed up search */
	g_ptr_array_sort (archive->files, file_data_compare_by_path);

	/* the name of the volumes are different from the
	 * original name */
	if (archive->multi_volume)
		fr_archive_change_name (archive, FR_COMMAND (archive)->filename);
	fr_archive_update_capabilities (archive);

	g_simple_async_result_complete_in_idle (load_data->result);

	load_data_free (load_data);
}


static void
load_local_archive_list_ready_cb (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	LoadData *load_data = user_data;
	FrError  *error = NULL;

	if (! fr_command_handle_process_error (FR_COMMAND (load_data->archive), result, &error))
		return;

	if (error != NULL) {
		_fr_command_load_complete_with_error (load_data, error->gerror);
		fr_error_free (error);
		return;
	}

	_fr_command_load_complete (load_data);

	fr_error_free (error);
}


static void
load_local_archive (LoadData *load_data)
{
	FrCommand *self = FR_COMMAND (load_data->archive);

	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);
	fr_process_use_standard_locale (self->process, TRUE);
	load_data->archive->multi_volume = FALSE;

        g_object_set (self,
                      "filename", self->priv->local_copy,
                      "password", load_data->password,
                      NULL);

        fr_process_clear (self->process);
	if (FR_COMMAND_GET_CLASS (G_OBJECT (self))->list (self))
		fr_process_execute (self->process,
				    load_data->cancellable,
				    load_local_archive_list_ready_cb,
				    load_data);
	else
		_fr_command_load_complete (load_data);
}


static void
copy_remote_file_done (GError   *error,
		       gpointer  user_data)
{
	LoadData *load_data = user_data;

	if (error != NULL)
		_fr_command_load_complete_with_error (load_data, error);
	else
		load_local_archive (load_data);
}


static void
copy_remote_file_progress (goffset   current_file,
                           goffset   total_files,
                           GFile    *source,
                           GFile    *destination,
                           goffset   current_num_bytes,
                           goffset   total_num_bytes,
                           gpointer  user_data)
{
	LoadData *load_data = user_data;

	fr_archive_progress (load_data->archive, (double) current_num_bytes / total_num_bytes);
}


static gboolean
copy_remote_file_done_cb (gpointer user_data)
{
	LoadData *load_data = user_data;

	copy_remote_file_done (NULL, load_data);

	return FALSE;
}


static void
copy_remote_file (LoadData *load_data)
{
	FrCommand *self = FR_COMMAND (load_data->archive);

	if (! g_file_query_exists (fr_archive_get_file (FR_ARCHIVE (self)),
				   load_data->cancellable))
	{
		GError *error;

		error = g_error_new (G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Archive not found"));
		_fr_command_load_complete_with_error (load_data, error);
		g_error_free (error);

		return;
	}

	if (self->priv->is_remote) {
		fr_archive_action_started (load_data->archive, FR_ACTION_LOADING_ARCHIVE);
		g_copy_file_async (fr_archive_get_file (FR_ARCHIVE (self)),
				   self->priv->local_copy,
				   G_FILE_COPY_OVERWRITE,
				   G_PRIORITY_DEFAULT,
				   load_data->cancellable,
				   copy_remote_file_progress,
				   load_data,
				   copy_remote_file_done,
				   load_data);
	}
	else
		g_idle_add (copy_remote_file_done_cb, load_data);
}


static void
fr_command_load (FrArchive           *archive,
		 const char          *password,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	LoadData *load_data;

	load_data = g_new0 (LoadData, 1);
	load_data->archive = g_object_ref (archive);
	load_data->password = g_strdup (password);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_load);

	copy_remote_file (load_data);
}


/* -- add -- */


static char *
create_tmp_base_dir (const char *base_dir,
		     const char *dest_path)
{
	char *dest_dir;
	char *temp_dir;
	char *tmp;
	char *parent_dir;
	char *dir;

	if ((dest_path == NULL)
	    || (*dest_path == '\0')
	    || (strcmp (dest_path, "/") == 0))
	{
		return g_strdup (base_dir);
	}

	dest_dir = g_strdup (dest_path);
	if (dest_dir[strlen (dest_dir) - 1] == G_DIR_SEPARATOR)
		dest_dir[strlen (dest_dir) - 1] = 0;

	debug (DEBUG_INFO, "base_dir: %s\n", base_dir);
	debug (DEBUG_INFO, "dest_dir: %s\n", dest_dir);

	temp_dir = _g_path_get_temp_work_dir (NULL);
	tmp = _g_path_remove_level (dest_dir);
	parent_dir =  g_build_filename (temp_dir, tmp, NULL);
	g_free (tmp);

	debug (DEBUG_INFO, "mkdir %s\n", parent_dir);
	_g_path_make_directory_tree (parent_dir, 0700, NULL);
	g_free (parent_dir);

	dir = g_build_filename (temp_dir, "/", dest_dir, NULL);
	debug (DEBUG_INFO, "symlink %s --> %s\n", dir, base_dir);
	if (! symlink (base_dir, dir)) {
		/* void */
	}

	g_free (dir);
	g_free (dest_dir);

	return temp_dir;
}


static FileData *
find_file_in_archive (FrArchive *archive,
		      char      *path)
{
	int i;

	g_return_val_if_fail (path != NULL, NULL);

	i = find_path_in_file_data_array (archive->files, path);
	if (i >= 0)
		return (FileData *) g_ptr_array_index (archive->files, i);
	else
		return NULL;
}


static void delete_from_archive (FrCommand *self,
				 GList     *file_list);


static GList *
newer_files_only (FrArchive  *archive,
		  GList      *file_list,
		  const char *base_dir)
{
	GList *newer_files = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char     *filename = scan->data;
		char     *fullpath;
		char     *uri;
		FileData *fdata;

		fdata = find_file_in_archive (archive, filename);

		if (fdata == NULL) {
			newer_files = g_list_prepend (newer_files, g_strdup (scan->data));
			continue;
		}

		fullpath = g_strconcat (base_dir, "/", filename, NULL);
		uri = g_filename_to_uri (fullpath, NULL, NULL);

		if (fdata->modified >= _g_uri_get_file_mtime (uri)) {
			g_free (fullpath);
			g_free (uri);
			continue;
		}
		g_free (fullpath);
		g_free (uri);

		newer_files = g_list_prepend (newer_files, g_strdup (scan->data));
	}

	return newer_files;
}


static gboolean
save_list_to_temp_file (GList   *file_list,
		        char   **list_dir,
		        char   **list_filename,
		        GError **error)
{
	gboolean           error_occurred = FALSE;
	GFile             *list_file;
	GFileOutputStream *ostream;

	if (error != NULL)
		*error = NULL;
	*list_dir = _g_path_get_temp_work_dir (NULL);
	*list_filename = g_build_filename (*list_dir, "file-list", NULL);
	list_file = g_file_new_for_path (*list_filename);
	ostream = g_file_create (list_file, G_FILE_CREATE_PRIVATE, NULL, error);

	if (ostream != NULL) {
		GList *scan;

		for (scan = file_list; scan != NULL; scan = scan->next) {
			char *filename = scan->data;

			filename = _g_str_substitute (filename, "\n", "\\n");
			if ((g_output_stream_write (G_OUTPUT_STREAM (ostream), filename, strlen (filename), NULL, error) < 0)
			    || (g_output_stream_write (G_OUTPUT_STREAM (ostream), "\n", 1, NULL, error) < 0))
			{
				error_occurred = TRUE;
			}

			g_free (filename);

			if (error_occurred)
				break;
		}
		if (! error_occurred && ! g_output_stream_close (G_OUTPUT_STREAM (ostream), NULL, error))
			error_occurred = TRUE;
		g_object_unref (ostream);
	}
	else
		error_occurred = TRUE;

	if (error_occurred) {
		_g_path_remove_directory (*list_dir);
		g_free (*list_dir);
		g_free (*list_filename);
		*list_dir = NULL;
		*list_filename = NULL;
	}

	g_object_unref (list_file);

	return ! error_occurred;
}


static GList *
split_in_chunks (GList *file_list)
{
	GList *chunks = NULL;
	GList *new_file_list;
	GList *scan;

	new_file_list = g_list_copy (file_list);
	for (scan = new_file_list; scan != NULL; /* void */) {
		GList *prev = scan->prev;
		GList *chunk;
		int    l;

		chunk = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += strlen (scan->data);
		}
		if (prev != NULL) {
			if (prev->next != NULL)
				prev->next->prev = NULL;
			prev->next = NULL;
		}
		chunks = g_list_append (chunks, chunk);
	}

	return chunks;
}


static gboolean
_fr_command_add (FrCommand      *self,
		 GList          *file_list,
		 const char     *base_dir,
		 const char     *dest_dir,
		 gboolean        update,
		 gboolean        recursive,
		 const char     *password,
		 gboolean        encrypt_header,
		 FrCompression   compression,
		 guint           volume_size,
		 GCancellable   *cancellable,
		 GError        **error)
{
	FrArchive *archive = FR_ARCHIVE (self);
	GList     *new_file_list = NULL;
	gboolean   base_dir_created = FALSE;
	GList     *scan;
	char      *tmp_base_dir = NULL;
	char      *tmp_archive_dir = NULL;
	char      *archive_filename = NULL;
	char      *tmp_archive_filename = NULL;
	gboolean   error_occurred = FALSE;

	if (file_list == NULL)
		return FALSE;

	g_object_set (self,
		      "filename", self->priv->local_copy,
		      "password", password,
		      "encrypt-header", encrypt_header,
		      "compression", compression,
		      "volume-size", volume_size,
		      NULL);

	fr_archive_set_stoppable (archive, TRUE);

	/* dest_dir is the destination folder inside the archive */

	if ((dest_dir != NULL) && (*dest_dir != '\0') && (strcmp (dest_dir, "/") != 0)) {
		const char *rel_dest_dir = dest_dir;

		tmp_base_dir = create_tmp_base_dir (base_dir, dest_dir);
		base_dir_created = TRUE;

		if (dest_dir[0] == G_DIR_SEPARATOR)
			rel_dest_dir = dest_dir + 1;

		new_file_list = NULL;
		for (scan = file_list; scan != NULL; scan = scan->next) {
			char *filename = scan->data;
			new_file_list = g_list_prepend (new_file_list, g_build_filename (rel_dest_dir, filename, NULL));
		}
	}
	else {
		tmp_base_dir = g_strdup (base_dir);
		new_file_list = _g_string_list_dup (file_list);
	}

	/* if the command cannot update,  get the list of files that are
	 * newer than the ones in the archive. */

	if (update && ! archive->propAddCanUpdate) {
		GList *tmp_file_list;

		tmp_file_list = new_file_list;
		new_file_list = newer_files_only (archive, tmp_file_list, tmp_base_dir);
		_g_string_list_free (tmp_file_list);
	}

	if (new_file_list == NULL) {
		debug (DEBUG_INFO, "nothing to update.\n");

		if (base_dir_created)
			_g_path_remove_directory (tmp_base_dir);
		g_free (tmp_base_dir);

		return FALSE;
	}

	self->creating_archive = ! g_file_query_exists (self->priv->local_copy, cancellable);

	/* create the new archive in a temporary sub-directory, this allows
	 * to cancel the operation without losing the original archive and
	 * removing possible temporary files created by the command. */

	{
		GFile *local_copy_parent;
		char  *archive_dir;
		GFile *tmp_file;

		/* create the new archive in a sub-folder of the original
		 * archive this way the 'mv' command is fast. */

		local_copy_parent = g_file_get_parent (self->priv->local_copy);
		archive_dir = g_file_get_path (local_copy_parent);
		tmp_archive_dir = _g_path_get_temp_work_dir (archive_dir);
		archive_filename = g_file_get_path (self->priv->local_copy);
		tmp_archive_filename = g_build_filename (tmp_archive_dir,
							 _g_path_get_file_name (archive_filename),
							 NULL);
		tmp_file = g_file_new_for_path (tmp_archive_filename);
		g_object_set (self, "filename", tmp_file, NULL);

		if (! self->creating_archive) {
			/* copy the original archive to the new position */

			fr_process_begin_command (self->process, "cp");
			fr_process_add_arg (self->process, "-f");
			fr_process_add_arg (self->process, archive_filename);
			fr_process_add_arg (self->process, tmp_archive_filename);
			fr_process_end_command (self->process);
		}

		g_object_unref (tmp_file);
		g_free (archive_dir);
		g_object_unref (local_copy_parent);
	}

	fr_command_uncompress (self);

	/* when files are already present in a tar archive and are added
	 * again, they are not replaced, so we have to delete them first. */

	/* if we are adding (== ! update) and 'add' cannot replace or
	 * if we are updating and 'add' cannot update,
	 * delete the files first. */

	if ((! update && ! archive->propAddCanReplace)
	    || (update && ! archive->propAddCanUpdate))
	{
		GList *del_list = NULL;

		for (scan = new_file_list; scan != NULL; scan = scan->next) {
			char *filename = scan->data;
			if (find_file_in_archive (archive, filename))
				del_list = g_list_prepend (del_list, filename);
		}

		/* delete */

		if (del_list != NULL) {
			delete_from_archive (self, del_list);
			fr_process_set_ignore_error (self->process, TRUE);
			g_list_free (del_list);
		}
	}

	/* add now. */

	fr_archive_set_n_files (archive, g_list_length (new_file_list));

	if (archive->propListFromFile && (archive->n_files > LIST_LENGTH_TO_USE_FILE)) {
		char   *list_dir;
		char   *list_filename;

		if (! save_list_to_temp_file (new_file_list, &list_dir, &list_filename, error)) {
			error_occurred = TRUE;
		}
		else {
			fr_command_add (self,
					list_filename,
					new_file_list,
					tmp_base_dir,
					update,
					recursive);

			/* remove the temp dir */

			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir());
			fr_process_set_sticky (self->process, TRUE);
			fr_process_add_arg (self->process, "-rf");
			fr_process_add_arg (self->process, list_dir);
			fr_process_end_command (self->process);
		}

		g_free (list_filename);
		g_free (list_dir);
	}
	else {
		GList *chunks = NULL;

		/* specify the file list on the command line, splitting
		 * in more commands to avoid to overflow the command line
		 * length limit. */

		chunks = split_in_chunks (new_file_list);
		for (scan = chunks; scan != NULL; scan = scan->next) {
			GList *chunk = scan->data;

			fr_command_add (self,
					NULL,
					chunk,
					tmp_base_dir,
					update,
					recursive);
			g_list_free (chunk);
		}

		g_list_free (chunks);
	}

	_g_string_list_free (new_file_list);

	if (! error_occurred) {
		fr_command_recompress (self);

		/* move the new archive to the original position */

		fr_process_begin_command (self->process, "mv");
		fr_process_add_arg (self->process, "-f");
		fr_process_add_arg (self->process, tmp_archive_filename);
		fr_process_add_arg (self->process, archive_filename);
		fr_process_end_command (self->process);

		/* remove the temp sub-directory */

		fr_process_begin_command (self->process, "rm");
		fr_process_set_working_dir (self->process, g_get_tmp_dir ());
		fr_process_set_sticky (self->process, TRUE);
		fr_process_add_arg (self->process, "-rf");
		fr_process_add_arg (self->process, tmp_archive_dir);
		fr_process_end_command (self->process);

		/* remove the archive dir */

		if (base_dir_created) {
			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir ());
			fr_process_set_sticky (self->process, TRUE);
			fr_process_add_arg (self->process, "-rf");
			fr_process_add_arg (self->process, tmp_base_dir);
			fr_process_end_command (self->process);
		}
	}

	g_free (tmp_archive_filename);
	g_free (archive_filename);
	g_free (tmp_archive_dir);
	g_free (tmp_base_dir);

	return ! error_occurred;
}


/* -- fr_command_add_files -- */


typedef struct {
	FrArchive          *archive;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
} AddData;


static void
add_data_free (AddData *add_data)
{
	_g_object_unref (add_data->archive);
	_g_object_unref (add_data->cancellable);
	_g_object_unref (add_data->result);
	g_free (add_data);
}


static void add_dropped_items (DroppedItemsData *data);


static void
process_ready_for_add_files_cb (GObject      *source_object,
				GAsyncResult *result,
				gpointer      user_data)
{
	AddData *add_data = user_data;
	FrError *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error)) {
		g_simple_async_result_set_from_error (add_data->result, error->gerror);
	}
	else {
		FrArchive *archive = add_data->archive;
		FrCommand *self = FR_COMMAND (archive);

		_fr_command_remove_temp_work_dir (self);

		if (self->priv->continue_adding_dropped_items) {
			add_dropped_items (self->priv->dropped_items_data);
			return;
		}

		if (self->priv->dropped_items_data != NULL) {
			dropped_items_data_free (self->priv->dropped_items_data);
			self->priv->dropped_items_data = NULL;
		}

		/* the name of the volumes are different from the
		 * original name */
		if (archive->multi_volume)
			fr_archive_change_name (archive, self->filename);

		if (! g_file_has_uri_scheme (fr_archive_get_file (archive), "file")) {
			copy_archive_to_remote_location (add_data->archive,
							 add_data->result,
							 add_data->cancellable);

			add_data_free (add_data);
			return;
		}
	}

	g_simple_async_result_complete_in_idle (add_data->result);

	fr_error_free (error);
	add_data_free (add_data);
}


static void
_fr_command_add_local_files (FrCommand           *self,
			     GList               *file_list,
			     const char          *base_dir,
			     const char          *dest_dir,
			     gboolean             update,
			     gboolean             recursive,
			     const char          *password,
			     gboolean             encrypt_header,
			     FrCompression        compression,
			     guint                volume_size,
			     GCancellable        *cancellable,
			     GSimpleAsyncResult  *command_result)
{
	AddData *add_data;
	GError  *error = NULL;

	g_object_set (self, "filename", self->priv->local_copy, NULL);
	fr_process_clear (self->process);
	if (! _fr_command_add (self,
			       file_list,
			       base_dir,
			       dest_dir,
			       update,
			       recursive,
			       password,
			       encrypt_header,
			       compression,
			       volume_size,
			       cancellable,
			       &error))
	{
		if (error != NULL) {
			g_simple_async_result_set_from_error (command_result, error);
			g_error_free (error);
		}
		g_simple_async_result_complete_in_idle (command_result);
		return;
	}

	add_data = g_new0 (AddData, 1);
	add_data->archive = _g_object_ref (self);
	add_data->cancellable = _g_object_ref (cancellable);
	add_data->result = _g_object_ref (command_result);

	fr_process_execute (self->process,
			    add_data->cancellable,
			    process_ready_for_add_files_cb,
			    add_data);
}


static void
copy_remote_files_done (GError   *error,
			gpointer  user_data)
{
	XferData *xfer_data = user_data;

	if (error != NULL) {
		g_simple_async_result_set_from_error (xfer_data->result, error);
		g_simple_async_result_complete_in_idle (xfer_data->result);

	}
	else
		_fr_command_add_local_files (FR_COMMAND (xfer_data->archive),
					     xfer_data->file_list,
					     xfer_data->tmp_dir,
					     xfer_data->dest_dir,
					     FALSE,
					     xfer_data->recursive,
					     xfer_data->password,
					     xfer_data->encrypt_header,
					     xfer_data->compression,
					     xfer_data->volume_size,
					     xfer_data->cancellable,
					     g_object_ref (xfer_data->result));

	xfer_data_free (xfer_data);
}


static void
copy_remote_files_progress (goffset   current_file,
                            goffset   total_files,
                            GFile    *source,
                            GFile    *destination,
                            goffset   current_num_bytes,
                            goffset   total_num_bytes,
                            gpointer  user_data)
{
	XferData *xfer_data = user_data;

	fr_archive_progress (xfer_data->archive, (double) current_file / (total_files + 1));
}


static void
copy_remote_files (FrCommand           *self,
		   GList               *file_list,
		   const char          *base_uri,
		   const char          *dest_dir,
		   gboolean             update,
		   gboolean             recursive,
		   const char          *password,
		   gboolean             encrypt_header,
		   FrCompression        compression,
		   guint                volume_size,
		   const char          *tmp_dir,
		   GCancellable        *cancellable,
		   GSimpleAsyncResult  *result)
{
	GList        *sources = NULL;
	GList        *destinations = NULL;
	GHashTable   *created_folders;
	GList        *scan;
	XferData     *xfer_data;

	sources = NULL;
	destinations = NULL;
	created_folders = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
	for (scan = file_list; scan; scan = scan->next) {
		char *partial_filename = scan->data;
		char *local_uri;
		char *local_folder_uri;
		char *remote_uri;

		local_uri = g_strconcat ("file://", tmp_dir, "/", partial_filename, NULL);
		local_folder_uri = _g_path_remove_level (local_uri);
		if (g_hash_table_lookup (created_folders, local_folder_uri) == NULL) {
			GError *error = NULL;
			if (! _g_uri_ensure_dir_exists (local_folder_uri, 0755, &error)) {
				g_simple_async_result_set_from_error (result, error);
				g_simple_async_result_complete_in_idle (result);

				g_clear_error (&error);
				g_object_unref (result);
				g_free (local_folder_uri);
				g_free (local_uri);
				_g_file_list_free (sources);
				_g_file_list_free (destinations);
				g_hash_table_destroy (created_folders);

				return;
			}

			g_hash_table_insert (created_folders, local_folder_uri, GINT_TO_POINTER (1));
		}
		else
			g_free (local_folder_uri);

		remote_uri = g_strconcat (base_uri, "/", partial_filename, NULL);
		sources = g_list_append (sources, g_file_new_for_uri (remote_uri));
		g_free (remote_uri);

		destinations = g_list_append (destinations, g_file_new_for_uri (local_uri));
		g_free (local_uri);
	}
	g_hash_table_destroy (created_folders);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = FR_ARCHIVE (self);
	xfer_data->file_list = _g_string_list_dup (file_list);
	xfer_data->base_uri = g_strdup (base_uri);
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->update = update;
	xfer_data->recursive = recursive;
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->password = g_strdup (password);
	xfer_data->encrypt_header = encrypt_header;
	xfer_data->compression = compression;
	xfer_data->volume_size = volume_size;
	xfer_data->tmp_dir = g_strdup (tmp_dir);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = result;

	fr_archive_action_started (FR_ARCHIVE (self), FR_ACTION_COPYING_FILES_FROM_REMOTE);

	g_copy_files_async (sources,
			    destinations,
			    G_FILE_COPY_OVERWRITE,
			    G_PRIORITY_DEFAULT,
			    cancellable,
			    copy_remote_files_progress,
			    xfer_data,
			    copy_remote_files_done,
			    xfer_data);

	_g_file_list_free (sources);
	_g_file_list_free (destinations);
}


static void
fr_command_add_files (FrArchive           *base,
		      GList               *file_list,
		      const char          *base_dir,
		      const char          *dest_dir,
		      gboolean             update,
		      gboolean             recursive,
		      const char          *password,
		      gboolean             encrypt_header,
		      FrCompression        compression,
		      guint                volume_size,
		      GCancellable        *cancellable,
		      GAsyncReadyCallback  callback,
		      gpointer             user_data)
{
	FrCommand          *self = FR_COMMAND (base);
	GSimpleAsyncResult *result;

	result = g_simple_async_result_new (G_OBJECT (self),
					    callback,
					    user_data,
					    fr_archive_add_files);

	if (_g_uri_is_local (base_dir)) {
		char *local_dir;

		local_dir = g_filename_from_uri (base_dir, NULL, NULL);
		_fr_command_add_local_files (self,
					     file_list,
					     local_dir,
					     dest_dir,
					     update,
					     recursive,
					     password,
					     encrypt_header,
					     compression,
					     volume_size,
					     cancellable,
					     result);

		g_free (local_dir);
	}
	else
		copy_remote_files (self,
				   file_list,
				   base_dir,
				   dest_dir,
				   update,
				   recursive,
				   password,
				   encrypt_header,
				   compression,
				   volume_size,
				   _fr_command_get_temp_work_dir (self),
				   cancellable,
				   result);
}


/* -- remove -- */


static gboolean
file_is_in_subfolder_of (const char *filename,
		         GList      *folder_list)
{
	GList *scan;

	if (filename == NULL)
		return FALSE;

	for (scan = folder_list; scan; scan = scan->next) {
		char *folder_in_list = (char*) scan->data;

		if (_g_path_is_parent_of (folder_in_list, filename))
			return TRUE;
	}

	return FALSE;
}


static void
delete_from_archive (FrCommand *self,
		     GList     *file_list)
{
	FrArchive *archive = FR_ARCHIVE (self);
	gboolean   file_list_created = FALSE;
	GList     *tmp_file_list = NULL;
	gboolean   tmp_file_list_created = FALSE;
	GList     *scan;

	/* file_list == NULL means delete all the files in the archive. */

	if (file_list == NULL) {
		int i;

		for (i = 0; i < archive->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->files, i);
			file_list = g_list_prepend (file_list, fdata->original_path);
		}

		file_list_created = TRUE;
	}

	if (! archive->propCanDeleteNonEmptyFolders) {
		GList *folders_to_remove;

		/* remove from the list the files contained in folders to be
		 * removed. */

		folders_to_remove = NULL;
		for (scan = file_list; scan != NULL; scan = scan->next) {
			char *path = scan->data;

			if (path[strlen (path) - 1] == '/')
				folders_to_remove = g_list_prepend (folders_to_remove, path);
		}

		if (folders_to_remove != NULL) {
			tmp_file_list = NULL;
			for (scan = file_list; scan != NULL; scan = scan->next) {
				char *path = scan->data;

				if (! file_is_in_subfolder_of (path, folders_to_remove))
					tmp_file_list = g_list_prepend (tmp_file_list, path);
			}
			tmp_file_list_created = TRUE;
			g_list_free (folders_to_remove);
		}
	}

	if (! tmp_file_list_created)
		tmp_file_list = g_list_copy (file_list);

	if (file_list_created)
		g_list_free (file_list);

	fr_archive_set_n_files (archive, g_list_length (tmp_file_list));

	if (archive->propListFromFile && (archive->n_files > LIST_LENGTH_TO_USE_FILE)) {
		char *list_dir;
		char *list_filename;

		if (save_list_to_temp_file (tmp_file_list, &list_dir, &list_filename, NULL)) {
			fr_command_delete (self,
					   list_filename,
					   tmp_file_list);

			/* remove the temp dir */

			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir());
			fr_process_set_sticky (self->process, TRUE);
			fr_process_add_arg (self->process, "-rf");
			fr_process_add_arg (self->process, list_dir);
			fr_process_end_command (self->process);
		}

		g_free (list_filename);
		g_free (list_dir);
	}
	else {
		for (scan = tmp_file_list; scan != NULL; ) {
			GList *prev = scan->prev;
			GList *chunk_list;
			int    l;

			chunk_list = scan;
			l = 0;
			while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
				if (l == 0)
					l = strlen (scan->data);
				prev = scan;
				scan = scan->next;
				if (scan != NULL)
					l += strlen (scan->data);
			}

			prev->next = NULL;
			fr_command_delete (self, NULL, chunk_list);
			prev->next = scan;
		}
	}

	g_list_free (tmp_file_list);
}


static void
_fr_command_remove (FrCommand     *self,
		    GList         *file_list,
		    FrCompression  compression)
{
	FrArchive *archive = FR_ARCHIVE (self);
	char      *tmp_archive_dir = NULL;
	char      *archive_filename = NULL;
	char      *tmp_archive_filename = NULL;

	g_return_if_fail (self != NULL);

	fr_archive_set_stoppable (archive, TRUE);
	self->creating_archive = FALSE;
	g_object_set (self, "compression", compression, NULL);

	/* create the new archive in a temporary sub-directory, this allows
	 * to cancel the operation without losing the original archive and
	 * removing possible temporary files created by the command. */

	{
		GFile *local_copy_parent;
		char  *archive_dir;
		GFile *tmp_file;

		/* create the new archive in a sub-folder of the original
		 * archive this way the 'mv' command is fast. */

		local_copy_parent = g_file_get_parent (self->priv->local_copy);
		archive_dir = g_file_get_path (local_copy_parent);
		tmp_archive_dir = _g_path_get_temp_work_dir (archive_dir);
		archive_filename = g_file_get_path (self->priv->local_copy);
		tmp_archive_filename = g_build_filename (tmp_archive_dir, _g_path_get_file_name (archive_filename), NULL);
		tmp_file = g_file_new_for_path (tmp_archive_filename);
		g_object_set (self, "filename", tmp_file, NULL);

		if (! self->creating_archive) {
			/* copy the original self to the new position */

			fr_process_begin_command (self->process, "cp");
			fr_process_add_arg (self->process, "-f");
			fr_process_add_arg (self->process, archive_filename);
			fr_process_add_arg (self->process, tmp_archive_filename);
			fr_process_end_command (self->process);
		}

		g_object_unref (tmp_file);
		g_free (archive_dir);
		g_object_unref (local_copy_parent);
	}

	/* uncompress, delete and recompress */

	fr_command_uncompress (self);
	delete_from_archive (self, file_list);
	fr_command_recompress (self);

	/* move the new self to the original position */

	fr_process_begin_command (self->process, "mv");
	fr_process_add_arg (self->process, "-f");
	fr_process_add_arg (self->process, tmp_archive_filename);
	fr_process_add_arg (self->process, archive_filename);
	fr_process_end_command (self->process);

	/* remove the temp sub-directory */

	fr_process_begin_command (self->process, "rm");
	fr_process_set_working_dir (self->process, g_get_tmp_dir ());
	fr_process_set_sticky (self->process, TRUE);
	fr_process_add_arg (self->process, "-rf");
	fr_process_add_arg (self->process, tmp_archive_dir);
	fr_process_end_command (self->process);

	g_free (tmp_archive_filename);
	g_free (archive_filename);
	g_free (tmp_archive_dir);
}


static void
process_ready_for_remove_files_cb (GObject      *source_object,
				   GAsyncResult *result,
				   gpointer      user_data)
{
	XferData *xfer_data = user_data;
	FrError  *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error)) {
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);
		fr_error_free (error);
	}
	else {
		if (! g_file_has_uri_scheme (fr_archive_get_file (xfer_data->archive), "file")) {
			copy_archive_to_remote_location (xfer_data->archive,
							 xfer_data->result,
							 xfer_data->cancellable);

			xfer_data_free (xfer_data);
			return;
		}
	}

	g_simple_async_result_complete_in_idle (xfer_data->result);

	xfer_data_free (xfer_data);
}


static void
fr_command_remove_files (FrArchive           *archive,
		  	 GList               *file_list,
		  	 FrCompression        compression,
		  	 GCancellable        *cancellable,
		  	 GAsyncReadyCallback  callback,
		  	 gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (archive);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (archive);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_remove);

	g_object_set (self, "filename", self->priv->local_copy, NULL);
	fr_process_clear (self->process);
	_fr_command_remove (self, file_list, compression);

	fr_process_execute (self->process,
			    cancellable,
			    process_ready_for_remove_files_cb,
			    xfer_data);
}


/* -- extract -- */


static void
move_files_to_dir (FrCommand *self,
		   GList            *file_list,
		   const char       *source_dir,
		   const char       *dest_dir,
		   gboolean          overwrite)
{
	GList *list;
	GList *scan;

	/* we prefer mv instead of cp for performance reasons,
	 * but if the destination folder already exists mv
	 * doesn't work correctly. (bug #590027) */

	list = g_list_copy (file_list);
	for (scan = list; scan; /* void */) {
		GList *next = scan->next;
		char  *filename = scan->data;
		char  *basename;
		char  *destname;

		basename = g_path_get_basename (filename);
		destname = g_build_filename (dest_dir, basename, NULL);
		if (g_file_test (destname, G_FILE_TEST_IS_DIR)) {
			fr_process_begin_command (self->process, "cp");
			fr_process_add_arg (self->process, "-R");
			if (overwrite)
				fr_process_add_arg (self->process, "-f");
			else
				fr_process_add_arg (self->process, "-n");
			if (filename[0] == '/')
				fr_process_add_arg_concat (self->process, source_dir, filename, NULL);
			else
				fr_process_add_arg_concat (self->process, source_dir, "/", filename, NULL);
			fr_process_add_arg (self->process, dest_dir);
			fr_process_end_command (self->process);

			list = g_list_remove_link (list, scan);
			g_list_free (scan);
		}

		g_free (destname);
		g_free (basename);

		scan = next;
	}

	if (list == NULL)
		return;

	/* 'list' now contains the files that can be moved without problems */

	fr_process_begin_command (self->process, "mv");
	if (overwrite)
		fr_process_add_arg (self->process, "-f");
	else
		fr_process_add_arg (self->process, "-n");
	for (scan = list; scan; scan = scan->next) {
		char *filename = scan->data;

		if (filename[0] == '/')
			fr_process_add_arg_concat (self->process, source_dir, filename, NULL);
		else
			fr_process_add_arg_concat (self->process, source_dir, "/", filename, NULL);
	}
	fr_process_add_arg (self->process, dest_dir);
	fr_process_end_command (self->process);

	g_list_free (list);
}


static void
move_files_in_chunks (FrCommand *self,
		      GList            *file_list,
		      const char       *temp_dir,
		      const char       *dest_dir,
		      gboolean          overwrite)
{
	GList *scan;
	int    temp_dir_l;

	temp_dir_l = strlen (temp_dir);

	for (scan = file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;

		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = temp_dir_l + 1 + strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += temp_dir_l + 1 + strlen (scan->data);
		}

		prev->next = NULL;
		move_files_to_dir (self, chunk_list, temp_dir, dest_dir, overwrite);
		prev->next = scan;
	}
}


static void
extract_from_archive (FrCommand  *self,
		      GList      *file_list,
		      const char *dest_dir,
		      gboolean    overwrite,
		      gboolean    skip_older,
		      gboolean    junk_paths,
		      const char *password)
{
	GList *scan;

	g_object_set (self, "password", password, NULL);

	if (file_list == NULL) {
		fr_command_extract (self,
				    NULL,
				    file_list,
				    dest_dir,
				    overwrite,
				    skip_older,
				    junk_paths);
		return;
	}

	if (FR_ARCHIVE (self)->propListFromFile
	    && (g_list_length (file_list) > LIST_LENGTH_TO_USE_FILE))
	{
		char *list_dir;
		char *list_filename;

		if (save_list_to_temp_file (file_list, &list_dir, &list_filename, NULL)) {
			fr_command_extract (self,
					    list_filename,
					    file_list,
					    dest_dir,
					    overwrite,
					    skip_older,
					    junk_paths);

			/* remove the temp dir */

			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir());
			fr_process_set_sticky (self->process, TRUE);
			fr_process_add_arg (self->process, "-rf");
			fr_process_add_arg (self->process, list_dir);
			fr_process_end_command (self->process);
		}

		g_free (list_filename);
		g_free (list_dir);
	}
	else {
		for (scan = file_list; scan != NULL; ) {
			GList *prev = scan->prev;
			GList *chunk_list;
			int    l;

			chunk_list = scan;
			l = 0;
			while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
				if (l == 0)
					l = strlen (scan->data);
				prev = scan;
				scan = scan->next;
				if (scan != NULL)
					l += strlen (scan->data);
			}

			prev->next = NULL;
			fr_command_extract (self,
					    NULL,
					    chunk_list,
					    dest_dir,
					    overwrite,
					    skip_older,
					    junk_paths);
			prev->next = scan;
		}
	}
}


static char*
compute_base_path (const char *base_dir,
		   const char *path,
		   gboolean    junk_paths,
		   gboolean    can_junk_paths)
{
	int         base_dir_len = strlen (base_dir);
	int         path_len = strlen (path);
	const char *base_path;
	char       *name_end;
	char       *new_path;

	if (junk_paths) {
		if (can_junk_paths)
			new_path = g_strdup (_g_path_get_file_name (path));
		else
			new_path = g_strdup (path);

		/*debug (DEBUG_INFO, "%s, %s --> %s\n", base_dir, path, new_path);*/

		return new_path;
	}

	if (path_len <= base_dir_len)
		return NULL;

	base_path = path + base_dir_len;
	if (path[0] != '/')
		base_path -= 1;
	name_end = strchr (base_path, '/');

	if (name_end == NULL)
		new_path = g_strdup (path);
	else {
		int name_len = name_end - path;
		new_path = g_strndup (path, name_len);
	}

	/*debug (DEBUG_INFO, "%s, %s --> %s\n", base_dir, path, new_path);*/

	return new_path;
}


static GList*
compute_list_base_path (const char *base_dir,
			GList      *filtered,
			gboolean    junk_paths,
			gboolean    can_junk_paths)
{
	GList *scan;
	GList *list = NULL, *list_unique = NULL;
	GList *last_inserted;

	if (filtered == NULL)
		return NULL;

	for (scan = filtered; scan; scan = scan->next) {
		const char *path = scan->data;
		char       *new_path;

		new_path = compute_base_path (base_dir, path, junk_paths, can_junk_paths);
		if (new_path != NULL)
			list = g_list_prepend (list, new_path);
	}

	/* The above operation can create duplicates, we remove them here. */
	list = g_list_sort (list, (GCompareFunc)strcmp);

	last_inserted = NULL;
	for (scan = list; scan; scan = scan->next) {
		const char *path = scan->data;

		if (last_inserted != NULL) {
			const char *last_path = (const char*)last_inserted->data;
			if (strcmp (last_path, path) == 0) {
				g_free (scan->data);
				continue;
			}
		}

		last_inserted = scan;
		list_unique = g_list_prepend (list_unique, scan->data);
	}

	g_list_free (list);

	return list_unique;
}


static gboolean
file_list_contains_files_in_this_dir (GList      *file_list,
				      const char *dirname)
{
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;

		if (_g_path_is_parent_of (dirname, filename))
			return TRUE;
	}

	return FALSE;
}


static GList*
remove_files_contained_in_this_dir (GList *file_list,
				    GList *dir_pointer)
{
	char  *dirname = dir_pointer->data;
	int    dirname_l = strlen (dirname);
	GList *scan;

	for (scan = dir_pointer->next; scan; /* empty */) {
		char *filename = scan->data;

		if (strncmp (dirname, filename, dirname_l) != 0)
			break;

		if (_g_path_is_parent_of (dirname, filename)) {
			GList *next = scan->next;

			file_list = g_list_remove_link (file_list, scan);
			g_list_free (scan);

			scan = next;
		}
		else
			scan = scan->next;
	}

	return file_list;
}


static void
_fr_command_extract (FrCommand  *self,
		     GList      *file_list,
		     const char *destination,
		     const char *base_dir,
		     gboolean    skip_older,
		     gboolean    overwrite,
		     gboolean    junk_paths,
		     const char *password)
{
	FrArchive *archive = FR_ARCHIVE (self);
	GList     *filtered;
	GList     *scan;
	gboolean   extract_all;
	gboolean   use_base_dir;
	gboolean   all_options_supported;
	gboolean   move_to_dest_dir;
	gboolean   file_list_created = FALSE;

	g_return_if_fail (archive != NULL);

	fr_archive_set_stoppable (archive, TRUE);
	g_object_set (self, "filename", self->priv->local_copy, NULL);

	/* if a command supports all the requested options use
	 * fr_command_extract_files directly. */

	use_base_dir = ! ((base_dir == NULL)
			  || (strcmp (base_dir, "") == 0)
			  || (strcmp (base_dir, "/") == 0));

	all_options_supported = (! use_base_dir
				 && ! (! overwrite && ! archive->propExtractCanAvoidOverwrite)
				 && ! (skip_older && ! archive->propExtractCanSkipOlder)
				 && ! (junk_paths && ! archive->propExtractCanJunkPaths));

	extract_all = (file_list == NULL);
	if (extract_all && (! all_options_supported || ! archive->propCanExtractAll)) {
		int i;

		file_list = NULL;
		for (i = 0; i < archive->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->files, i);
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}
		file_list_created = TRUE;
	}

	if (extract_all && (file_list == NULL))
		fr_archive_set_n_files (archive, archive->files->len);
	else
		fr_archive_set_n_files (archive, g_list_length (file_list));

	if (all_options_supported) {
		gboolean created_filtered_list = FALSE;

		if (! extract_all && ! archive->propCanExtractNonEmptyFolders) {
			created_filtered_list = TRUE;
			filtered = g_list_copy (file_list);
			filtered = g_list_sort (filtered, (GCompareFunc) strcmp);
			for (scan = filtered; scan; scan = scan->next)
				filtered = remove_files_contained_in_this_dir (filtered, scan);
		}
		else
			filtered = file_list;

		if (! (created_filtered_list && (filtered == NULL)))
			extract_from_archive (self,
					      filtered,
					      destination,
					      overwrite,
					      skip_older,
					      junk_paths,
					      password);

		if (created_filtered_list && (filtered != NULL))
			g_list_free (filtered);

		if (file_list_created)
			_g_string_list_free (file_list);

		return;
	}

	/* .. else we have to implement the unsupported options. */

	move_to_dest_dir = (use_base_dir
			    || ((junk_paths
				 && ! archive->propExtractCanJunkPaths)));

	if (extract_all && ! file_list_created) {
		int i;

		file_list = NULL;
		for (i = 0; i < archive->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->files, i);
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}

		file_list_created = TRUE;
	}

	filtered = NULL;
	for (scan = file_list; scan; scan = scan->next) {
		FileData   *fdata;
		char       *archive_list_filename = scan->data;
		char        dest_filename[4096];
		const char *filename;

		fdata = find_file_in_archive (archive, archive_list_filename);

		if (fdata == NULL)
			continue;

		if (! archive->propCanExtractNonEmptyFolders
		    && fdata->dir
		    && file_list_contains_files_in_this_dir (file_list, archive_list_filename))
			continue;

		/* get the destination file path. */

		if (! junk_paths)
			filename = archive_list_filename;
		else
			filename = _g_path_get_file_name (archive_list_filename);

		if ((destination[strlen (destination) - 1] == '/')
		    || (filename[0] == '/'))
			sprintf (dest_filename, "%s%s", destination, filename);
		else
			sprintf (dest_filename, "%s/%s", destination, filename);

		/*debug (DEBUG_INFO, "-> %s\n", dest_filename);*/

		/**/

		if (! archive->propExtractCanSkipOlder
		    && skip_older
		    && g_file_test (dest_filename, G_FILE_TEST_EXISTS)
		    && (fdata->modified < _g_path_get_file_mtime (dest_filename)))
			continue;

		if (! archive->propExtractCanAvoidOverwrite
		    && ! overwrite
		    && g_file_test (dest_filename, G_FILE_TEST_EXISTS))
			continue;

		filtered = g_list_prepend (filtered, fdata->original_path);
	}

	if (filtered == NULL) {
		/* all files got filtered, do nothing. */
		debug (DEBUG_INFO, "All files got filtered, nothing to do.\n");

		if (extract_all)
			_g_string_list_free (file_list);
		return;
	}

	if (move_to_dest_dir) {
		char *temp_dir;

		temp_dir = _g_path_get_temp_work_dir (destination);
		extract_from_archive (self,
				      filtered,
				      temp_dir,
				      overwrite,
				      skip_older,
				      junk_paths,
				      password);

		if (use_base_dir) {
			GList *tmp_list = compute_list_base_path (base_dir, filtered, junk_paths, archive->propExtractCanJunkPaths);
			g_list_free (filtered);
			filtered = tmp_list;
		}

		move_files_in_chunks (self,
				      filtered,
				      temp_dir,
				      destination,
				      overwrite);

		/* remove the temp dir. */

		fr_process_begin_command (self->process, "rm");
		fr_process_add_arg (self->process, "-rf");
		fr_process_add_arg (self->process, temp_dir);
		fr_process_end_command (self->process);

		g_free (temp_dir);
	}
	else
		extract_from_archive (self,
				      filtered,
				      destination,
				      overwrite,
				      skip_older,
				      junk_paths,
				      password);

	if (filtered != NULL)
		g_list_free (filtered);
	if (file_list_created)
		_g_string_list_free (file_list);
}


/* -- _fr_command_extract_to_local -- */


static void
process_ready_for_extract_to_local_cb (GObject      *source_object,
				       GAsyncResult *result,
				       gpointer      user_data)
{
	XferData  *xfer_data = user_data;
	FrError   *error = NULL;
	FrCommand *self = FR_COMMAND (xfer_data->archive);

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error))
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);

	if (error == NULL) {
		if (self->priv->remote_extraction) {
			copy_extracted_files_to_destination (xfer_data->archive,
							     xfer_data->result,
							     xfer_data->cancellable);
			xfer_data_free (xfer_data);
			return;
		}
		else if (xfer_data->archive->extract_here)
			move_here (xfer_data->archive, xfer_data->cancellable);
	}
	else {
		/* if an error occurred during extraction remove the
		 * temp extraction dir, if used. */
		g_print ("action_performed: ERROR!\n");

		if ((self->priv->remote_extraction) && (self->priv->temp_extraction_dir != NULL)) {
			_g_path_remove_directory (self->priv->temp_extraction_dir);
			g_free (self->priv->temp_extraction_dir);
			self->priv->temp_extraction_dir = NULL;
		}

		if (xfer_data->archive->extract_here)
			_g_uri_remove_directory (fr_archive_get_last_extraction_destination (xfer_data->archive));
	}

	g_simple_async_result_complete_in_idle (xfer_data->result);

	fr_error_free (error);
	xfer_data_free (xfer_data);
}


static void
_fr_command_extract_to_local (FrCommand           *self,
			      GList               *file_list,
			      const char          *destination,
			      const char          *base_dir,
			      gboolean             skip_older,
			      gboolean             overwrite,
			      gboolean             junk_paths,
			      const char          *password,
			      GCancellable        *cancellable,
			      GAsyncReadyCallback  callback,
			      gpointer             user_data)
{
	XferData *xfer_data;

	fr_process_clear (self->process);
	_fr_command_extract (self,
			     file_list,
			     destination,
			     base_dir,
			     skip_older,
			     overwrite,
			     junk_paths,
			     password);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (self);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_extract);
	fr_process_execute (self->process,
			    cancellable,
			    process_ready_for_extract_to_local_cb,
			    xfer_data);
}


static void
fr_command_extract_files (FrArchive           *base,
			  GList               *file_list,
			  const char          *destination,
			  const char          *base_dir,
			  gboolean             skip_older,
			  gboolean             overwrite,
			  gboolean             junk_paths,
			  const char          *password,
			  GCancellable        *cancellable,
			  GAsyncReadyCallback  callback,
			  gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (base);

	g_free (self->priv->temp_extraction_dir);
	self->priv->temp_extraction_dir = NULL;

	self->priv->remote_extraction = ! _g_uri_is_local (destination);
	if (self->priv->remote_extraction) {
		self->priv->temp_extraction_dir = _g_path_get_temp_work_dir (NULL);
		_fr_command_extract_to_local (self,
					     file_list,
					     self->priv->temp_extraction_dir,
					     base_dir,
					     skip_older,
					     overwrite,
					     junk_paths,
					     password,
					     cancellable,
					     callback,
					     user_data);
	}
	else {
		char *local_destination;

		local_destination = g_filename_from_uri (destination, NULL, NULL);
		_fr_command_extract_to_local (self,
					     file_list,
					     local_destination,
					     base_dir,
					     skip_older,
					     overwrite,
					     junk_paths,
					     password,
					     cancellable,
					     callback,
					     user_data);
		g_free (local_destination);
	}
}


/* -- fr_command_test_integrity -- */


static void
process_ready_for_test_integrity (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	XferData  *xfer_data = user_data;
	FrError   *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error))
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);

	g_simple_async_result_complete_in_idle (xfer_data->result);

	fr_error_free (error);
	xfer_data_free (xfer_data);
}


static void
fr_command_test_integrity (FrArchive           *archive,
			   const char          *password,
			   GCancellable        *cancellable,
			   GAsyncReadyCallback  callback,
			   gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (archive);
	XferData  *xfer_data;

	fr_archive_set_stoppable (archive, TRUE);
	g_object_set (archive,
		      "filename", self->priv->local_copy,
		      "password", password,
		      NULL);
	fr_archive_set_n_files (archive, 0);

	fr_process_clear (self->process);
	fr_command_test (self);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (self);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_test);

	fr_process_execute (self->process,
			    cancellable,
			    process_ready_for_test_integrity,
			    xfer_data);
}


/* -- fr_command_rename -- */


static void
process_ready_for_rename (GObject      *source_object,
			  GAsyncResult *result,
			  gpointer      user_data)
{
	XferData  *xfer_data = user_data;
	FrError   *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error))
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);

	g_simple_async_result_complete_in_idle (xfer_data->result);

	fr_error_free (error);
	xfer_data_free (xfer_data);
}


static void
fr_command_rename (FrArchive           *archive,
		   GList               *file_list,
		   const char          *old_name,
		   const char          *new_name,
		   const char          *current_dir,
		   gboolean             is_dir,
		   gboolean             dir_in_archive,
		   const char          *original_path,
		   GCancellable        *cancellable,
		   GAsyncReadyCallback  callback,
		   gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (archive);
	char      *tmp_dir;
	gboolean   added_dir;
	char      *new_dirname;
	GList     *new_file_list;
	GList     *scan;
	GError    *error = NULL;
	XferData  *xfer_data;

	fr_archive_set_stoppable (archive, TRUE);
	g_object_set (archive,
		      "filename", self->priv->local_copy,
		      NULL);

	tmp_dir = _g_path_get_temp_work_dir (NULL);

	fr_process_clear (self->process);
	_fr_command_extract (self,
			     file_list,
			     tmp_dir,
			     NULL,
			     FALSE,
			     TRUE,
			     FALSE,
			     archive->password);

	/* temporarily add the dir to rename to the list if it's stored in the
	 * archive, this way it will be removed from the archive... */
	added_dir = FALSE;
	if (is_dir && dir_in_archive && ! g_list_find_custom (file_list, original_path, (GCompareFunc) strcmp)) {
		file_list = g_list_prepend (file_list, g_strdup (original_path));
		added_dir = TRUE;
	}

	/* FIXME: libarchive, check this */
	_fr_command_remove (self, file_list, archive->compression);

	/* ...and remove it from the list again */
	if (added_dir) {
		GList *tmp;

		tmp = file_list;
		file_list = g_list_remove_link (file_list, tmp);

		g_free (tmp->data);
		g_list_free (tmp);
	}

	/* rename the files. */

	new_dirname = g_build_filename (current_dir + 1, new_name, "/", NULL);
	new_file_list = NULL;
	if (is_dir) {
		char *old_path;
		char *new_path;

		old_path = g_build_filename (tmp_dir, current_dir, old_name, NULL);
		new_path = g_build_filename (tmp_dir, current_dir, new_name, NULL);

		fr_process_begin_command (self->process, "mv");
		fr_process_add_arg (self->process, "-f");
		fr_process_add_arg (self->process, old_path);
		fr_process_add_arg (self->process, new_path);
		fr_process_end_command (self->process);

		g_free (old_path);
		g_free (new_path);
	}

	for (scan = file_list; scan; scan = scan->next) {
		const char *current_dir_relative = current_dir + 1;
		const char *filename = (char*) scan->data;
		char       *old_path = NULL, *common = NULL, *new_path = NULL;
		char       *new_filename;

		old_path = g_build_filename (tmp_dir, filename, NULL);

		if (strlen (filename) > (strlen (current_dir) + strlen (old_name)))
			common = g_strdup (filename + strlen (current_dir) + strlen (old_name));
		new_path = g_build_filename (tmp_dir, current_dir, new_name, common, NULL);

		if (! is_dir) {
			fr_process_begin_command (self->process, "mv");
			fr_process_add_arg (self->process, "-f");
			fr_process_add_arg (self->process, old_path);
			fr_process_add_arg (self->process, new_path);
			fr_process_end_command (self->process);
		}

		new_filename = g_build_filename (current_dir_relative, new_name, common, NULL);
		new_file_list = g_list_prepend (new_file_list, new_filename);

		g_free (old_path);
		g_free (common);
		g_free (new_path);
	}
	new_file_list = g_list_reverse (new_file_list);

	/* FIXME: this is broken for tar archives.
	if (is_dir && dir_in_archive && ! g_list_find_custom (new_file_list, new_dirname, (GCompareFunc) strcmp))
		new_file_list = g_list_prepend (new_file_list, g_build_filename (rdata->current_dir + 1, rdata->new_name, NULL));
	*/

	if (! _fr_command_add (self,
			       new_file_list,
			       tmp_dir,
			       NULL,
			       FALSE,
			       FALSE,
			       archive->password,
			       archive->encrypt_header,
			       archive->compression,
			       archive->volume_size,
			       cancellable,
			       &error))
	{
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (self),
						    callback,
						    user_data,
						    fr_archive_rename);
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_unlink (tmp_dir);

		g_object_unref (result);
		g_error_free (error);
		g_free (tmp_dir);

		return;
	}

	g_free (new_dirname);
	_g_string_list_free (new_file_list);

	/* remove the tmp dir */

	fr_process_begin_command (self->process, "rm");
	fr_process_set_working_dir (self->process, g_get_tmp_dir ());
	fr_process_set_sticky (self->process, TRUE);
	fr_process_add_arg (self->process, "-rf");
	fr_process_add_arg (self->process, tmp_dir);
	fr_process_end_command (self->process);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (self);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_rename);

	fr_process_execute (self->process,
			    cancellable,
			    process_ready_for_rename,
			    xfer_data);

	g_free (tmp_dir);
}


/* -- fr_command_paste_clipboard -- */


static void
process_ready_for_paste_clipboard (GObject      *source_object,
				   GAsyncResult *result,
				   gpointer      user_data)
{
	XferData  *xfer_data = user_data;
	FrError   *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error))
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);

	g_simple_async_result_complete_in_idle (xfer_data->result);

	fr_error_free (error);
	xfer_data_free (xfer_data);
}


static void
fr_command_paste_clipboard (FrArchive           *archive,
		    	    char                *archive_uri,
		    	    char                *password,
		    	    gboolean             encrypt_header,
		    	    FrCompression        compression,
		    	    guint                volume_size,
		    	    FrClipboardOp        op,
		    	    char                *base_dir,
		    	    GList               *files,
		    	    char                *tmp_dir,
		    	    char                *current_dir,
		    	    GCancellable        *cancellable,
		    	    GAsyncReadyCallback  callback,
		    	    gpointer             user_data)
{
	FrCommand  *command = FR_COMMAND (archive);
	const char *current_dir_relative = current_dir + 1;
	GList      *scan;
	GList      *new_file_list = NULL;
	GError     *error = NULL;
	XferData   *xfer_data;

	fr_process_clear (command->process);

	for (scan = files; scan; scan = scan->next) {
		const char *old_name = (char*) scan->data;
		char       *new_name = g_build_filename (current_dir_relative, old_name + strlen (base_dir) - 1, NULL);

		/* skip folders */

		if ((strcmp (old_name, new_name) != 0)
		    && (old_name[strlen (old_name) - 1] != '/'))
		{
			fr_process_begin_command (command->process, "mv");
			fr_process_set_working_dir (command->process, tmp_dir);
			fr_process_add_arg (command->process, "-f");
			if (old_name[0] == '/')
				old_name = old_name + 1;
			fr_process_add_arg (command->process, old_name);
			fr_process_add_arg (command->process, new_name);
			fr_process_end_command (command->process);
		}

		new_file_list = g_list_prepend (new_file_list, new_name);
	}

	if (! _fr_command_add (command,
			       new_file_list,
			       tmp_dir,
			       NULL,
			       FALSE,
			       FALSE,
			       password,
			       encrypt_header,
			       compression,
			       volume_size,
			       cancellable,
			       &error))
	{
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (command),
						    callback,
						    user_data,
						    fr_archive_paste_clipboard);

		if (error != NULL) {
			g_simple_async_result_set_from_error (result, error);
			g_error_free (error);
		}

		g_simple_async_result_complete_in_idle (result);

		g_object_unref (result);
		return;
	}

	_g_string_list_free (new_file_list);

	/* remove the tmp dir */

	fr_process_begin_command (command->process, "rm");
	fr_process_set_working_dir (command->process, g_get_tmp_dir ());
	fr_process_set_sticky (command->process, TRUE);
	fr_process_add_arg (command->process, "-rf");
	fr_process_add_arg (command->process, tmp_dir);
	fr_process_end_command (command->process);

	/**/

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (command);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (command),
						       callback,
						       user_data,
						       fr_archive_paste_clipboard);

	fr_process_execute (command->process,
			    cancellable,
			    process_ready_for_paste_clipboard,
			    xfer_data);
}


/* -- fr_command_add_dropped_items -- */


static gboolean
all_files_in_same_dir (GList *list)
{
	gboolean  same_dir = TRUE;
	char     *first_basedir;
	GList    *scan;

	if (list == NULL)
		return FALSE;

	first_basedir = _g_path_remove_level (list->data);
	if (first_basedir == NULL)
		return TRUE;

	for (scan = list->next; scan; scan = scan->next) {
		char *path = scan->data;
		char *basedir;

		basedir = _g_path_remove_level (path);
		if (basedir == NULL) {
			same_dir = FALSE;
			break;
		}

		if (strcmp (first_basedir, basedir) != 0) {
			same_dir = FALSE;
			g_free (basedir);
			break;
		}
		g_free (basedir);
	}
	g_free (first_basedir);

	return same_dir;
}


static void
add_dropped_items (DroppedItemsData *data)
{
	FrCommand *self = data->command;
	GList     *list = data->item_list;
	GList     *scan;

	if (list == NULL) {
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (data->command),
						    data->callback,
						    data->user_data,
						    fr_archive_add_dropped_items);
		g_simple_async_result_complete_in_idle (result);

		dropped_items_data_free (self->priv->dropped_items_data);
		self->priv->dropped_items_data = NULL;
		return;
	}

	/* if all files/dirs are in the same directory call fr_archive_add_items... */

	if (all_files_in_same_dir (list)) {
		char *first_base_dir;

		first_base_dir = _g_path_remove_level (list->data);
		fr_archive_add_items (FR_ARCHIVE (self),
				      list,
				      first_base_dir,
				      data->dest_dir,
				      data->update,
				      data->password,
				      data->encrypt_header,
				      data->compression,
				      data->volume_size,
				      data->cancellable,
				      data->callback,
				      data->user_data);

		g_free (first_base_dir);
		dropped_items_data_free (self->priv->dropped_items_data);
		self->priv->dropped_items_data = NULL;

		return;
	}

	/* ...else add a directory at a time. */

	for (scan = list; scan; scan = scan->next) {
		char *path = scan->data;
		char *base_dir;

		if (! _g_uri_query_is_dir (path))
			continue;

		data->item_list = g_list_remove_link (list, scan);
		if (data->item_list != NULL)
			self->priv->continue_adding_dropped_items = TRUE;
		base_dir = _g_path_remove_level (path);

		fr_archive_add_directory (FR_ARCHIVE (self),
					  _g_path_get_file_name (path),
					  base_dir,
					  data->dest_dir,
					  data->update,
					  data->password,
					  data->encrypt_header,
					  data->compression,
					  data->volume_size,
					  data->cancellable,
					  data->callback,
					  data->user_data);

		g_free (base_dir);
		g_free (path);

		return;
	}

	/* if all files are in the same directory call fr_archive_add_files. */

	if (all_files_in_same_dir (list)) {
		char  *first_basedir;
		GList *only_names_list = NULL;

		first_basedir = _g_path_remove_level (list->data);

		for (scan = list; scan; scan = scan->next) {
			char *name;

			name = g_uri_unescape_string (_g_path_get_file_name (scan->data), NULL);
			only_names_list = g_list_prepend (only_names_list, name);
		}

		fr_archive_add_files (FR_ARCHIVE (self),
				      only_names_list,
				      first_basedir,
				      data->dest_dir,
				      data->update,
				      FALSE,
				      data->password,
				      data->encrypt_header,
				      data->compression,
				      data->volume_size,
				      data->cancellable,
				      data->callback,
				      data->user_data);

		_g_string_list_free (only_names_list);
		g_free (first_basedir);

		return;
	}

	/* ...else call fr_command_add for each file.  This is needed to add
	 * files without path info. FIXME: doesn't work with remote files. */

	fr_archive_set_stoppable (FR_ARCHIVE (self), TRUE);
	self->creating_archive = ! g_file_query_exists (self->priv->local_copy, data->cancellable);
	g_object_set (self,
		      "filename", self->priv->local_copy,
		      "password", data->password,
		      "encrypt-header", data->encrypt_header,
		      "compression", data->compression,
		      "volume-size", data->volume_size,
		      NULL);

	fr_process_clear (self->process);
	fr_command_uncompress (self);
	for (scan = list; scan; scan = scan->next) {
		char  *fullpath = scan->data;
		char  *basedir;
		GList *singleton;

		basedir = _g_path_remove_level (fullpath);
		singleton = g_list_prepend (NULL, (char*)_g_path_get_file_name (fullpath));
		fr_command_add (self,
				NULL,
				singleton,
				basedir,
				data->update,
				FALSE);
		g_list_free (singleton);
		g_free (basedir);
	}
	fr_command_recompress (self);
	fr_process_execute (self->process,
			    data->cancellable,
			    data->callback,
			    data->user_data);

	_g_string_list_free (data->item_list);
	data->item_list = NULL;
}


static void
fr_command_add_dropped_items (FrArchive           *archive,
		   	      GList               *item_list,
		   	      const char          *base_dir,
		   	      const char          *dest_dir,
		   	      gboolean             update,
		   	      const char          *password,
		   	      gboolean             encrypt_header,
		   	      FrCompression        compression,
		   	      guint                volume_size,
		   	      GCancellable        *cancellable,
		   	      GAsyncReadyCallback  callback,
		   	      gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (archive);

	if (self->priv->dropped_items_data != NULL)
		dropped_items_data_free (self->priv->dropped_items_data);
	self->priv->dropped_items_data = dropped_items_data_new (self,
								 item_list,
								 base_dir,
								 dest_dir,
								 update,
								 password,
								 encrypt_header,
								 compression,
								 volume_size,
								 cancellable,
								 callback,
								 user_data);
	add_dropped_items (self->priv->dropped_items_data);
}


/* -- fr_command_update_open_files -- */


static void
process_ready_for_update_open_files (GObject      *source_object,
				     GAsyncResult *result,
				     gpointer      user_data)
{
	XferData *xfer_data = user_data;
	FrError  *error = NULL;

	if (! fr_process_execute_finish (FR_PROCESS (source_object), result, &error))
		g_simple_async_result_set_from_error (xfer_data->result, error->gerror);

	g_simple_async_result_complete_in_idle (xfer_data->result);

	fr_error_free (error);
	xfer_data_free (xfer_data);
}


static void
fr_command_update_open_files (FrArchive           *archive,
			      GList               *file_list,
			      GList               *dir_list,
			      const char          *password,
			      gboolean             encrypt_header,
			      FrCompression        compression,
			      guint                volume_size,
		   	      GCancellable        *cancellable,
		   	      GAsyncReadyCallback  callback,
		   	      gpointer             user_data)
{
	FrCommand *self = FR_COMMAND (archive);
	GList     *scan_file, *scan_dir;
	XferData  *xfer_data;

	fr_process_clear (self->process);

	for (scan_file = file_list, scan_dir = dir_list;
	     scan_file && scan_dir;
	     scan_file = scan_file->next, scan_dir = scan_dir->next)
	{
		char  *filepath = scan_file->data;
		char  *dirpath = scan_dir->data;
		GList *local_file_list;

		local_file_list = g_list_append (NULL, filepath);
		_fr_command_add (self,
				 local_file_list,
				 dirpath,
				 "/",
				 FALSE,
				 FALSE,
				 password,
				 encrypt_header,
				 compression,
				 volume_size,
				 cancellable,
				 NULL);

		g_list_free (local_file_list);
	}

	/**/

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (self);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_update_open_files);

	fr_process_execute (self->process,
			    cancellable,
			    process_ready_for_update_open_files,
			    xfer_data);
}


static void
fr_command_base_uncompress (FrCommand *command)
{
	/* void */
}


static void
fr_command_base_recompress (FrCommand *command)
{
	/* void */
}


static void
fr_command_class_init (FrCommandClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;

	fr_command_parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (FrCommandPrivate));

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->set_property = fr_command_set_property;
	gobject_class->get_property = fr_command_get_property;
	gobject_class->finalize = fr_command_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->load = fr_command_load;
	archive_class->add_files = fr_command_add_files;
	archive_class->remove_files = fr_command_remove_files;
	archive_class->extract_files = fr_command_extract_files;
	archive_class->test_integrity = fr_command_test_integrity;
	archive_class->rename = fr_command_rename;
	archive_class->paste_clipboard = fr_command_paste_clipboard;
	archive_class->add_dropped_items = fr_command_add_dropped_items;
	archive_class->update_open_files = fr_command_update_open_files;

	klass->list = NULL;
	klass->add = NULL;
	klass->delete = NULL;
	klass->extract = NULL;
	klass->test = NULL;
	klass->uncompress = fr_command_base_uncompress;
	klass->recompress = fr_command_base_recompress;
	klass->handle_error = NULL;

	/* properties */

	g_object_class_install_property (gobject_class,
					 PROP_PROCESS,
					 g_param_spec_object ("process",
							      "Process",
							      "The process object used by the command",
							      FR_TYPE_PROCESS,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_FILENAME,
					 g_param_spec_object ("filename",
							      "Filename",
							      "The path of the archive the command will use (can be different from local_copy)",
							      G_TYPE_FILE,
							      G_PARAM_READWRITE));
}


static GFile *
get_local_copy_for_file (GFile *remote_file)
{
	char  *temp_dir;
	GFile *local_copy = NULL;

	temp_dir = _g_path_get_temp_work_dir (NULL);
	if (temp_dir != NULL) {
		char  *archive_name;
		char  *local_path;

		archive_name = g_file_get_basename (remote_file);
		local_path = g_build_filename (temp_dir, archive_name, NULL);
		local_copy = g_file_new_for_path (local_path);

		g_free (local_path);
		g_free (archive_name);
	}
	g_free (temp_dir);

	return local_copy;
}


static void
archive_file_changed_cb (GObject    *object,
			 GParamSpec *spec,
			 gpointer    user_data)
{
	FrCommand *self = user_data;
	GFile     *file;

	/* FIXME: if multi_volume... */
	if (FR_ARCHIVE (self)->multi_volume)
		return;

	if ((self->priv->local_copy != NULL) && self->priv->is_remote) {
		GFile  *temp_folder;
		GError *err = NULL;

		g_file_delete (self->priv->local_copy, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete the local copy: %s", err->message);
			g_clear_error (&err);
		}

		temp_folder = g_file_get_parent (self->priv->local_copy);
		g_file_delete (temp_folder, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete temp folder: %s", err->message);
			g_clear_error (&err);
		}

		g_object_unref (temp_folder);
	}

	if (self->priv->local_copy != NULL) {
		g_object_unref (self->priv->local_copy);
		self->priv->local_copy = NULL;
	}

	file = fr_archive_get_file (FR_ARCHIVE (object));
	self->priv->is_remote = ! g_file_has_uri_scheme (file, "file");
	if (self->priv->is_remote)
		self->priv->local_copy = get_local_copy_for_file (file);
	else
		self->priv->local_copy = g_file_dup (file);

	_fr_command_set_filename_from_file (self, self->priv->local_copy);
}


static void
fr_command_init (FrCommand *self)
{
	FrProcess *process;

	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_COMMAND, FrCommandPrivate);

	self->priv->local_copy = NULL;
	self->priv->is_remote = FALSE;
	self->priv->temp_dir = NULL;
	self->priv->continue_adding_dropped_items = FALSE;
	self->priv->dropped_items_data = NULL;
	self->priv->temp_extraction_dir = NULL;
	self->priv->remote_extraction = FALSE;

	self->filename = NULL;
	self->e_filename = NULL;
	self->creating_archive = FALSE;

	process = fr_process_new ();
	_fr_command_set_process (self, process);
	g_object_unref (process);

	g_signal_connect (self,
			  "notify::file",
			  G_CALLBACK (archive_file_changed_cb),
			  self);
}


GList  *
fr_command_get_last_output (FrCommand *self)
{
	return (self->process->err.raw != NULL) ? self->process->err.raw : self->process->out.raw;
}
