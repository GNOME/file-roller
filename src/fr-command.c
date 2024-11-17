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
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include "fr-file-data.h"
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


/* -- XferData -- */


typedef struct {
	FrArchive          *archive;
	char               *uri;
	FrAction            action;
	GList              *file_list;
	GFile              *base_dir;
	char               *dest_dir;
	gboolean            update;
	gboolean            follow_links;
	GFile              *tmp_dir;
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
	_g_object_list_unref (data->file_list);
	_g_object_unref (data->base_dir);
	g_free (data->dest_dir);
	_g_object_unref (data->tmp_dir);
	_g_object_unref (data->cancellable);
	_g_object_unref (data->result);
	g_free (data);
}


/* -- FrCommand -- */


/* Properties */
enum {
        PROP_0,
        PROP_PROCESS,
        PROP_FILENAME
};


typedef struct {
	GFile     *local_copy;
	gboolean   is_remote;
	GFile     *temp_dir;
	GFile     *temp_extraction_dir;
	gboolean   remote_extraction;
} FrCommandPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrCommand, fr_command, FR_TYPE_ARCHIVE)


static void
_fr_command_remove_temp_work_dir (FrCommand *self)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	if (private->temp_dir == NULL)
		return;
	_g_file_remove_directory (private->temp_dir, NULL, NULL);
	_g_clear_object (&private->temp_dir);
}


static GFile *
_fr_command_get_temp_work_dir (FrCommand *self)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	_fr_command_remove_temp_work_dir (self);
	private->temp_dir = _g_file_get_temp_work_dir (NULL);
	return private->temp_dir;
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
	FrCommandPrivate *private = fr_command_get_instance_private (FR_COMMAND (xfer_data->archive));

	g_copy_file_async (private->local_copy,
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
copy_extracted_files_done (GError   *error,
			   gpointer  user_data)
{
	XferData  *xfer_data = user_data;
	FrCommand *self = FR_COMMAND (xfer_data->archive);
	FrCommandPrivate *private = fr_command_get_instance_private (self);

	if (error != NULL)
		g_simple_async_result_set_from_error (xfer_data->result, error);

	_g_file_remove_directory (private->temp_extraction_dir, NULL, NULL);
	_g_clear_object (&private->temp_extraction_dir);

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
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (archive);
	xfer_data->result = _g_object_ref (result);
	xfer_data->cancellable = _g_object_ref (cancellable);

	fr_archive_action_started (archive, FR_ACTION_COPYING_FILES_TO_REMOTE);

	g_directory_copy_async (private->temp_extraction_dir,
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
		GFile      *base_dir,
		gboolean    update,
		gboolean    follow_links)
{
	char *base_dir_path;

	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	base_dir_path = g_file_get_path (base_dir);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->add (self,
						     from_file,
						     file_list,
						     base_dir_path,
						     update,
						     follow_links);

	g_free (base_dir_path);
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
		    GFile      *destination,
		    gboolean    overwrite,
		    gboolean    skip_older,
		    gboolean    junk_paths)
{
	char *destination_path;

	fr_process_set_out_line_func (self->process, NULL, NULL);
	fr_process_set_err_line_func (self->process, NULL, NULL);

	destination_path = g_file_get_path (destination);
	g_return_if_fail (destination_path != NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (self))->extract (self,
							 from_file,
							 file_list,
							 destination_path,
							 overwrite,
							 skip_older,
							 junk_paths);

	g_free (destination_path);
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
				 GError       **error)
{
	FrError *process_error = NULL;

	self->process->restart = FALSE;

	fr_process_execute_finish (self->process, result, &process_error);

	if (process_error == NULL)
		process_error = fr_error_new (FR_ERROR_NONE, 0, NULL);

	if (g_error_matches (process_error->gerror, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (process_error->gerror);
		process_error->type = FR_ERROR_STOPPED;
		process_error->gerror = g_error_new_literal (FR_ERROR, FR_ERROR_STOPPED, "");
	}

	if (process_error->type != FR_ERROR_STOPPED)
		/* the command can change process_error->gerror or activate the
		 * 'restart' flag */
		FR_COMMAND_GET_CLASS (G_OBJECT (self))->handle_error (self, process_error);

	if ((error != NULL) && (process_error->gerror != NULL) && (process_error->type != FR_ERROR_NONE))
		*error = g_error_copy (process_error->gerror);
	fr_error_free (process_error);

	if (self->process->restart) {
		fr_process_restart (self->process);
		return FALSE;
	}

	return TRUE;
}


/* -- _fr_command_set_process -- */


static void
process_sticky_only_cb (FrProcess *process,
                        gpointer   user_data)
{
	FrArchive *archive = user_data;

	fr_archive_set_stoppable (archive, FALSE);
        return;
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
	g_signal_connect (FR_PROCESS (self->process),
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);

	_g_object_unref (self->process);
	_fr_command_remove_temp_work_dir (self);
	_g_clear_object (&private->temp_extraction_dir);

	if (G_OBJECT_CLASS (fr_command_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_parent_class)->finalize (object);
}


/* -- open -- */


static void
copy_remote_file_done (GError   *error,
		       gpointer  user_data)
{
	XferData *xfer_data = user_data;

	if (error != NULL)
		g_simple_async_result_set_from_error (xfer_data->result, error);
	g_simple_async_result_complete_in_idle (xfer_data->result);

	xfer_data_free (xfer_data);
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
	XferData *xfer_data = user_data;

	fr_archive_progress (xfer_data->archive, (double) current_num_bytes / total_num_bytes);
}


static void
fr_command_open (FrArchive           *archive,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	FrCommand *command = FR_COMMAND (archive);
	FrCommandPrivate *private = fr_command_get_instance_private (command);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = g_object_ref (archive);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_open);

	if (! private->is_remote) {
		GError *error = NULL;

		if (! g_file_query_exists (fr_archive_get_file (archive), cancellable)) {
			error = g_error_new (G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Archive not found"));
		}

		if (error != NULL)
			g_simple_async_result_set_from_error (xfer_data->result, error);
		g_simple_async_result_complete_in_idle (xfer_data->result);

		xfer_data_free (xfer_data);
		return;
	}

	fr_archive_action_started (archive, FR_ACTION_LOADING_ARCHIVE);
	g_copy_file_async (fr_archive_get_file (archive),
			   private->local_copy,
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   xfer_data->cancellable,
			   copy_remote_file_progress,
			   xfer_data,
			   copy_remote_file_done,
			   xfer_data);
}


/* -- list -- */


static void
_fr_command_load_complete (XferData *xfer_data,
			   GError   *error)
{
	if (error == NULL) {
		FrArchive *archive = xfer_data->archive;

		/* the name of the volumes are different from the
		 * original name */
		if (archive->multi_volume)
			fr_archive_change_name (archive, FR_COMMAND (archive)->filename);

		/* the header is encrypted if the load is successful and the password is not void */
		archive->encrypt_header = (xfer_data->password != NULL) && (*xfer_data->password != '\0');

		fr_archive_update_capabilities (archive);
	}
	else
		g_simple_async_result_set_from_error (xfer_data->result, error);
	g_simple_async_result_complete_in_idle (xfer_data->result);

	xfer_data_free (xfer_data);
}


static void
load_local_archive_list_ready_cb (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	XferData *xfer_data = user_data;
	GError   *error = NULL;

	if (! fr_command_handle_process_error (FR_COMMAND (xfer_data->archive), result, &error))
		/* command restarted */
		return;

	_fr_command_load_complete (xfer_data, error);

	_g_error_free (error);
}


static void
fr_command_list (FrArchive           *archive,
		 const char          *password,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	FrCommand *command = FR_COMMAND (archive);
	FrCommandPrivate *private = fr_command_get_instance_private (command);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->password = g_strdup (password);
	xfer_data->archive = g_object_ref (archive);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_list);

	fr_process_set_out_line_func (command->process, NULL, NULL);
	fr_process_set_err_line_func (command->process, NULL, NULL);
	fr_process_use_standard_locale (command->process, TRUE);
	archive->multi_volume = FALSE;
        g_object_set (archive,
                      "filename", private->local_copy,
                      "password", password,
                      NULL);

        fr_process_clear (command->process);
	if (FR_COMMAND_GET_CLASS (G_OBJECT (command))->list (command))
		fr_process_execute (command->process,
				    cancellable,
				    load_local_archive_list_ready_cb,
				    xfer_data);
	else
		_fr_command_load_complete (xfer_data, NULL);
}


/* -- add -- */


static GFile *
create_tmp_base_dir (GFile      *base_dir,
		     const char *destination)
{
	GFile  *temp_dir;
	char   *destination_parent;
	GFile  *parent_dir;
	GFile  *dir;
	char   *path;
	GError *error = NULL;

	if ((destination == NULL) || (*destination == '\0')|| (strcmp (destination, "/") == 0))
		return g_object_ref (base_dir);

	debug (DEBUG_INFO, "base_dir: %s\n", g_file_get_path (base_dir));
	debug (DEBUG_INFO, "dest_dir: %s\n", destination);

	temp_dir = _g_file_get_temp_work_dir (NULL);
	destination_parent = _g_path_remove_level (destination);
	parent_dir = _g_file_append_path (temp_dir, destination_parent, NULL);

	debug (DEBUG_INFO, "mkdir %s\n", g_file_get_path (parent_dir));
	_g_file_make_directory_tree (parent_dir, 0700, NULL);

	dir = _g_file_append_path (temp_dir, destination, NULL);

	path = g_file_get_path (base_dir);
	debug (DEBUG_INFO, "symlink %s --> %s\n", g_file_get_path (dir), path);
	if (! g_file_make_symbolic_link (dir, path, NULL, &error)) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}

	g_free (path);
	g_object_unref (dir);
	g_object_unref (parent_dir);
	g_free (destination_parent);

	return temp_dir;
}


static FrFileData *
find_file_in_archive (FrArchive *archive,
		      char      *path)
{
	int i;

	g_return_val_if_fail (path != NULL, NULL);

	i = fr_find_path_in_file_data_array (archive->files, path);
	if (i >= 0)
		return (FrFileData *) g_ptr_array_index (archive->files, i);
	else
		return NULL;
}


static void delete_from_archive (FrCommand *self,
				 GList     *file_list);


static GList *
newer_files_only (FrArchive  *archive,
		  GList      *file_list,
		  GFile      *base_dir)
{
	GList *newer_files = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char     *filename = scan->data;
		GFile    *file;
		FrFileData *fdata;

		fdata = find_file_in_archive (archive, filename);

		if (fdata == NULL) {
			newer_files = g_list_prepend (newer_files, g_strdup (filename));
			continue;
		}

		file = _g_file_append_path (base_dir, filename, NULL);
		if (fdata->modified >= _g_file_get_file_mtime (file)) {
			g_object_unref (file);
			continue;
		}
		g_object_unref (file);

		newer_files = g_list_prepend (newer_files, g_strdup (filename));
	}

	return newer_files;
}


static gboolean
save_list_to_temp_file (GList   *file_list,
		        char   **list_dirname,
		        char   **list_filename,
		        GError **error)
{
	gboolean           error_occurred = FALSE;
	GFile             *list_dir;
	GFile             *list_file;
	GFileOutputStream *ostream;

	list_dir = _g_file_get_temp_work_dir (NULL);
	list_file = g_file_get_child (list_dir, "file-list");
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
		_g_file_remove_directory (list_dir, NULL, NULL);
		*list_dirname = NULL;
		*list_filename = NULL;
	}
	else {
		*list_dirname = g_file_get_path (list_dir);
		*list_filename = g_file_get_path (list_file);
	}

	g_object_unref (list_dir);
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
		 GFile          *base_dir,
		 const char     *dest_dir,
		 gboolean        update,
		 gboolean        follow_links,
		 const char     *password,
		 gboolean        encrypt_header,
		 FrCompression   compression,
		 guint           volume_size,
		 GCancellable   *cancellable,
		 GError        **error)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	FrArchive *archive = FR_ARCHIVE (self);
	GList     *new_file_list = NULL;
	gboolean   base_dir_created = FALSE;
	GList     *scan;
	GFile     *tmp_base_dir = NULL;
	char      *tmp_archive_dir = NULL;
	char      *archive_filename = NULL;
	char      *tmp_archive_filename = NULL;
	gboolean   error_occurred = FALSE;
	int        new_file_list_length;
	gboolean   use_tmp_subdirectory;

	if (file_list == NULL)
		return FALSE;

	g_object_set (self,
		      "filename", private->local_copy,
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
		tmp_base_dir = g_object_ref (base_dir);
		new_file_list = _g_string_list_dup (file_list);
	}

	/* see fr-archive.h for an explanation of the following code */

	if (base_dir_created && ! archive->propAddCanFollowDirectoryLinksWithoutDereferencing)
		follow_links = TRUE;

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
			_g_file_remove_directory (tmp_base_dir, NULL, NULL);
		g_object_unref (tmp_base_dir);

		return FALSE;
	}

	self->creating_archive = ! g_file_query_exists (private->local_copy, cancellable);

	/* create the new archive in a temporary sub-directory, this allows
	 * to cancel the operation without losing the original archive and
	 * removing possible temporary files created by the command. */

	use_tmp_subdirectory = (volume_size == 0) || ! fr_archive_is_capable_of (FR_ARCHIVE (self), FR_ARCHIVE_CAN_CREATE_VOLUMES);

	if (use_tmp_subdirectory) {
		GFile *local_copy_parent;
		char  *archive_dir;
		GFile *tmp_file;

		/* create the new archive in a sub-folder of the original
		 * archive this way the 'mv' command is fast. */

		local_copy_parent = g_file_get_parent (private->local_copy);
		archive_dir = g_file_get_path (local_copy_parent);
		tmp_archive_dir = _g_path_get_temp_work_dir (archive_dir);
		archive_filename = g_file_get_path (private->local_copy);
		tmp_archive_filename = g_build_filename (tmp_archive_dir,
							 _g_path_get_basename (archive_filename),
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

	new_file_list_length = g_list_length (new_file_list);
	fr_archive_progress_set_total_files (archive, new_file_list_length);

	if (archive->propListFromFile && (new_file_list_length > LIST_LENGTH_TO_USE_FILE)) {
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
					follow_links);

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
					follow_links);
			g_list_free (chunk);
		}

		g_list_free (chunks);
	}

	_g_string_list_free (new_file_list);

	if (! error_occurred) {
		fr_command_recompress (self);

		/* move the new archive to the original position */

		if (use_tmp_subdirectory) {
			fr_process_begin_command (self->process, "mv");
			fr_process_add_arg (self->process, "-f");
			fr_process_add_arg (self->process, "--");
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
		}

		/* remove the archive dir */

		if (base_dir_created) {
			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir ());
			fr_process_set_sticky (self->process, TRUE);
			fr_process_add_arg (self->process, "-rf");
			fr_process_add_arg_file (self->process, tmp_base_dir);
			fr_process_end_command (self->process);
		}
	}

	g_free (tmp_archive_filename);
	g_free (archive_filename);
	g_free (tmp_archive_dir);
	g_object_unref (tmp_base_dir);

	return ! error_occurred;
}


/* -- fr_command_add_files -- */


static void
process_ready_after_changing_archive (GObject      *source_object,
				      GAsyncResult *result,
				      gpointer      user_data)
{
	XferData *xfer_data = user_data;
	GError   *error = NULL;

	if (! fr_command_handle_process_error (FR_COMMAND (xfer_data->archive), result, &error))
		/* command restarted */
		return;

	if (error != NULL) {
		g_simple_async_result_set_from_error (xfer_data->result, error);
	}
	else {
		FrArchive *archive = xfer_data->archive;
		FrCommand *self = FR_COMMAND (archive);

		if (g_simple_async_result_get_source_tag (xfer_data->result) == fr_archive_add_files) {
			_fr_command_remove_temp_work_dir (self);

			/* the name of the volumes are different from the
			 * original name */
			if (archive->multi_volume)
				fr_archive_change_name (archive, self->filename);
		}

		if (! g_file_has_uri_scheme (fr_archive_get_file (archive), "file")) {
			copy_archive_to_remote_location (xfer_data->archive,
							 xfer_data->result,
							 xfer_data->cancellable);

			xfer_data_free (xfer_data);
			return;
		}
	}

	g_simple_async_result_complete_in_idle (xfer_data->result);

	g_clear_error (&error);
	xfer_data_free (xfer_data);
}


static GList *
get_relative_path_list (GList *file_list,
		        GFile *base_dir)
{
	GList    *relative_file_list;
	GList    *scan;

	relative_file_list = NULL;
	for (scan = file_list; scan; scan = scan->next) {
		char *relative_path;

		relative_path = g_file_get_relative_path (base_dir, G_FILE (scan->data));
		if (relative_path != NULL)
			relative_file_list = g_list_prepend (relative_file_list, relative_path);
	}

	return g_list_reverse (relative_file_list);
}


static void
_fr_command_add_local_files (FrCommand           *self,
			     GList               *file_list,
			     GFile               *base_dir,
			     const char          *dest_dir,
			     gboolean             update,
			     gboolean             follow_links,
			     const char          *password,
			     gboolean             encrypt_header,
			     FrCompression        compression,
			     guint                volume_size,
			     GCancellable        *cancellable,
			     GSimpleAsyncResult  *command_result)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	GList    *relative_file_list;
	XferData *xfer_data;
	GError   *error = NULL;

	relative_file_list = get_relative_path_list (file_list, base_dir);

	g_object_set (self, "filename", private->local_copy, NULL);
	fr_process_clear (self->process);
	if (! _fr_command_add (self,
			       relative_file_list,
			       base_dir,
			       dest_dir,
			       update,
			       follow_links,
			       password,
			       encrypt_header,
			       compression,
			       volume_size,
			       cancellable,
			       &error))
	{
		_g_string_list_free (relative_file_list);
		if (error != NULL) {
			g_simple_async_result_set_from_error (command_result, error);
			g_error_free (error);
		}
		g_simple_async_result_complete_in_idle (command_result);
		return;
	}

	_g_string_list_free (relative_file_list);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (self);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = _g_object_ref (command_result);

	fr_process_execute (self->process,
			    xfer_data->cancellable,
			    process_ready_after_changing_archive,
			    xfer_data);
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
					     xfer_data->update,
					     xfer_data->follow_links,
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
		   GFile               *base_dir,
		   const char          *dest_dir,
		   gboolean             update,
		   gboolean             follow_links,
		   const char          *password,
		   gboolean             encrypt_header,
		   FrCompression        compression,
		   guint                volume_size,
		   GFile               *tmp_dir,
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
	created_folders = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, (GDestroyNotify) g_object_unref, NULL);
	for (scan = file_list; scan; scan = scan->next) {
		GFile *file = scan->data;
		char  *relative_path;
		GFile *local_file;
		GFile *local_folder;

		relative_path = g_file_get_relative_path (base_dir, file);
		local_file = _g_file_append_path (tmp_dir, relative_path, NULL);
		local_folder = g_file_get_parent (local_file);
		if (g_hash_table_lookup (created_folders, local_folder) == NULL) {
			GError *error = NULL;

			if (! _g_file_make_directory_tree (local_folder, 0755, &error)) {
				g_simple_async_result_set_from_error (result, error);
				g_simple_async_result_complete_in_idle (result);

				g_object_unref (result);
				g_clear_error (&error);
				g_object_unref (local_folder);
				g_object_unref (local_file);
				g_free (relative_path);
				_g_file_list_free (sources);
				_g_file_list_free (destinations);
				g_hash_table_destroy (created_folders);

				return;
			}

			g_hash_table_insert (created_folders, g_object_ref (local_folder), GINT_TO_POINTER (1));
		}

		sources = g_list_append (sources, g_object_ref (file));
		destinations = g_list_append (destinations, g_object_ref (local_file));

		g_object_unref (local_folder);
		g_object_unref (local_file);
		g_free (relative_path);
	}
	g_hash_table_destroy (created_folders);

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = g_object_ref (FR_ARCHIVE (self));
	xfer_data->file_list = _g_object_list_ref (file_list);
	xfer_data->base_dir = g_object_ref (base_dir);
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->update = update;
	xfer_data->follow_links = follow_links;
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->password = g_strdup (password);
	xfer_data->encrypt_header = encrypt_header;
	xfer_data->compression = compression;
	xfer_data->volume_size = volume_size;
	xfer_data->tmp_dir = g_object_ref (tmp_dir);
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
		      GFile               *base_dir,
		      const char          *dest_dir,
		      gboolean             update,
		      gboolean             follow_links,
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

	if (_g_file_is_local (base_dir))
		_fr_command_add_local_files (self,
					     file_list,
					     base_dir,
					     dest_dir,
					     update,
					     follow_links,
					     password,
					     encrypt_header,
					     compression,
					     volume_size,
					     cancellable,
					     result);
	else
		copy_remote_files (self,
				   file_list,
				   base_dir,
				   dest_dir,
				   update,
				   follow_links,
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
	int        tmp_file_list_length;

	/* file_list == NULL means delete all the files in the archive. */

	if (file_list == NULL) {
		for (guint i = 0; i < archive->files->len; i++) {
			FrFileData *fdata = g_ptr_array_index (archive->files, i);
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

	tmp_file_list_length = g_list_length (tmp_file_list);
	fr_archive_progress_set_total_files (archive, tmp_file_list_length);

	if (archive->propListFromFile && (tmp_file_list_length > LIST_LENGTH_TO_USE_FILE)) {
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
		FrCommandPrivate *private = fr_command_get_instance_private (self);

		/* create the new archive in a sub-folder of the original
		 * archive this way the 'mv' command is fast. */

		local_copy_parent = g_file_get_parent (private->local_copy);
		archive_dir = g_file_get_path (local_copy_parent);
		tmp_archive_dir = _g_path_get_temp_work_dir (archive_dir);
		archive_filename = g_file_get_path (private->local_copy);
		tmp_archive_filename = g_build_filename (tmp_archive_dir, _g_path_get_basename (archive_filename), NULL);
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

	/* move the new archive to the original position */

	fr_process_begin_command (self->process, "mv");
	fr_process_add_arg (self->process, "-f");
	fr_process_add_arg (self->process, "--");
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

	FrCommandPrivate *private = fr_command_get_instance_private (self);
	/* _fr_command_remove can change the filename, reset its value */
	g_object_set (archive,
	              "filename", private->local_copy,
		      NULL);

	g_free (tmp_archive_filename);
	g_free (archive_filename);
	g_free (tmp_archive_dir);
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	XferData  *xfer_data;

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (archive);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (self),
						       callback,
						       user_data,
						       fr_archive_remove);

	g_object_set (self, "filename", private->local_copy, NULL);
	fr_process_clear (self->process);
	_fr_command_remove (self, file_list, compression);

	fr_process_execute (self->process,
			    cancellable,
			    process_ready_after_changing_archive,
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
	fr_process_add_arg (self->process, "--");
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
move_files_in_chunks (FrCommand  *self,
		      GList      *file_list,
		      GFile      *temp_dir,
		      GFile      *destination,
		      gboolean    overwrite)
{
	GList *scan;
	char  *temp_dir_path;
	size_t temp_dir_l;
	char  *dest_dir;

	temp_dir_path = g_file_get_path (temp_dir);
	temp_dir_l = strlen (temp_dir_path);
	dest_dir = g_file_get_path (destination);

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
		move_files_to_dir (self, chunk_list, temp_dir_path, dest_dir, overwrite);
		prev->next = scan;
	}

	g_free (dest_dir);
	g_free (temp_dir_path);
}


static void
extract_from_archive (FrCommand  *self,
		      GList      *file_list,
		      GFile      *destination,
		      gboolean    overwrite,
		      gboolean    skip_older,
		      gboolean    junk_paths,
		      const char *password)
{
	GList *scan;

	fr_archive_set_password (FR_ARCHIVE (self), password);

	if (file_list == NULL) {
		fr_command_extract (self,
				    NULL,
				    file_list,
				    destination,
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
					    destination,
					    overwrite,
					    skip_older,
					    junk_paths);

			/* remove the temp dir */

			fr_process_begin_command (self->process, "rm");
			fr_process_set_working_dir (self->process, g_get_tmp_dir ());
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
					    destination,
					    overwrite,
					    skip_older,
					    junk_paths);
			prev->next = scan;
		}
	}
}


static char *
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
			new_path = g_strdup (_g_path_get_basename (path));
		else
			new_path = g_strdup (path);

		/*debug (DEBUG_INFO, "%s, %s --> %s\n", base_dir, path, new_path);*/

		return new_path;
	}

	if (path_len < base_dir_len)
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
	size_t dirname_l = strlen (dirname);
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
		     GFile      *destination,
		     const char *base_dir,
		     gboolean    skip_older,
		     gboolean    overwrite,
		     gboolean    junk_paths,
		     const char *password)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
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
	g_object_set (self, "filename", private->local_copy, NULL);

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
		file_list = NULL;
		for (guint i = 0; i < archive->files->len; i++) {
			FrFileData *fdata = g_ptr_array_index (archive->files, i);
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}
		file_list_created = TRUE;
	}

	if (extract_all && (file_list == NULL))
		fr_archive_progress_set_total_files (archive, archive->files->len);
	else
		fr_archive_progress_set_total_files (archive, g_list_length (file_list));

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
		file_list = NULL;
		for (guint i = 0; i < archive->files->len; i++) {
			FrFileData *fdata = g_ptr_array_index (archive->files, i);
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}

		file_list_created = TRUE;
	}

	filtered = NULL;
	for (scan = file_list; scan; scan = scan->next) {
		FrFileData *fdata;
		char       *archive_list_filename = scan->data;
		const char *filename;
		GFile      *destination_file;

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
			filename = _g_path_get_basename (archive_list_filename);

		destination_file = _g_file_append_path (destination, filename, NULL);

		/*debug (DEBUG_INFO, "-> %s\n", g_file_get_uri (destination_file));*/

		/**/

		if (! archive->propExtractCanSkipOlder
		    && skip_older
		    && g_file_query_exists (destination_file, NULL)
		    && (fdata->modified < _g_file_get_file_mtime (destination_file)))
		{
			continue;
		}

		if (! archive->propExtractCanAvoidOverwrite
		    && ! overwrite
		    && g_file_query_exists (destination_file, NULL))
		{
			continue;
		}

		filtered = g_list_prepend (filtered, fdata->original_path);

		g_object_unref (destination_file);
	}

	if (filtered == NULL) {
		/* all files got filtered, do nothing. */
		debug (DEBUG_INFO, "All files got filtered, nothing to do.\n");

		if (extract_all)
			_g_string_list_free (file_list);
		return;
	}

	if (move_to_dest_dir) {
		GFile *temp_dir;

		temp_dir = _g_file_get_temp_work_dir (destination);
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
		fr_process_add_arg_file (self->process, temp_dir);
		fr_process_end_command (self->process);

		g_object_unref (temp_dir);
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
	GError    *error = NULL;
	FrCommand *self = FR_COMMAND (xfer_data->archive);
	FrCommandPrivate *private = fr_command_get_instance_private (self);

	if (! fr_command_handle_process_error (FR_COMMAND (xfer_data->archive), result, &error))
		/* command restarted */
		return;

	if (error == NULL) {
		if (private->remote_extraction) {
			copy_extracted_files_to_destination (xfer_data->archive,
							     xfer_data->result,
							     xfer_data->cancellable);
			xfer_data_free (xfer_data);
			return;
		}
	}
	else {
		g_simple_async_result_set_from_error (xfer_data->result, error);

		/* if an error occurred during extraction remove the
		 * temp extraction dir, if used. */

		if ((private->remote_extraction) && (private->temp_extraction_dir != NULL)) {
			_g_file_remove_directory (private->temp_extraction_dir, NULL, NULL);
			_g_clear_object (&private->temp_extraction_dir);
		}
	}

	g_simple_async_result_complete_in_idle (xfer_data->result);

	_g_error_free (error);
	xfer_data_free (xfer_data);
}


static void
_fr_command_extract_to_local (FrCommand           *self,
			      GList               *file_list,
			      GFile               *destination,
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
			  GFile               *destination,
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);

	_g_clear_object (&private->temp_extraction_dir);

	private->remote_extraction = ! _g_file_is_local (destination);
	if (private->remote_extraction) {
		private->temp_extraction_dir = _g_file_get_temp_work_dir (NULL);
		_fr_command_extract_to_local (self,
					     file_list,
		                             private->temp_extraction_dir,
					     base_dir,
					     skip_older,
					     overwrite,
					     junk_paths,
					     password,
					     cancellable,
					     callback,
					     user_data);
	}
	else
		_fr_command_extract_to_local (self,
					     file_list,
					     destination,
					     base_dir,
					     skip_older,
					     overwrite,
					     junk_paths,
					     password,
					     cancellable,
					     callback,
					     user_data);
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	XferData  *xfer_data;

	fr_archive_set_stoppable (archive, TRUE);
	g_object_set (archive,
	              "filename", private->local_copy,
		      "password", password,
		      NULL);
	fr_archive_progress_set_total_files (archive, 0);

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
fr_command_rename__delete (FrArchive   *archive,
		   	   GList      **file_list,
		   	   gboolean     is_dir,
		   	   gboolean     dir_in_archive,
		   	   const char  *original_path)
{
	FrCommand *self = FR_COMMAND (archive);
	gboolean   added_dir;

	/* temporarily add the dir to rename to the list if it's stored in the
	 * archive, this way it will be removed from the archive... */
	added_dir = FALSE;
	if (is_dir && dir_in_archive && ! g_list_find_custom (*file_list, original_path, (GCompareFunc) strcmp)) {
		*file_list = g_list_prepend (*file_list, g_strdup (original_path));
		added_dir = TRUE;
	}

	_fr_command_remove (self, *file_list, archive->compression);

	/* ...and remove it from the list again */
	if (added_dir) {
		GList *tmp;

		tmp = *file_list;
		*file_list = g_list_remove_link (*file_list, tmp);

		_g_string_list_free (tmp);
	}
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	GFile     *tmp_dir;
	char      *new_dirname;
	GList     *new_file_list;
	GList     *scan;
	GError    *error = NULL;
	XferData  *xfer_data;

	fr_archive_set_stoppable (archive, TRUE);
	g_object_set (archive,
	              "filename", private->local_copy,
		      NULL);

	tmp_dir = _g_file_get_temp_work_dir (NULL);

	fr_process_clear (self->process);

	/* extract the files to rename */

	_fr_command_extract (self,
			     file_list,
			     tmp_dir,
			     NULL,
			     FALSE,
			     TRUE,
			     FALSE,
			     archive->password);

	/* if the command can delete all the files in the archive without
	 * deleting the archive itself ('rar' deletes the archive), delete the
	 * files here, that is before adding the renamed files to the archive,
	 * to make the operation faster. */

	if (archive->propCanDeleteAllFiles)
		fr_command_rename__delete (archive,
					   &file_list,
					   is_dir,
					   dir_in_archive,
					   original_path);

	/* rename the files. */

	new_dirname = g_build_filename (current_dir + 1, new_name, "/", NULL);
	new_file_list = NULL;
	if (is_dir) {
		GFile *old_file;
		GFile *new_file;

		old_file = _g_file_append_path (tmp_dir, current_dir, old_name, NULL);
		new_file = _g_file_append_path (tmp_dir, current_dir, new_name, NULL);

		fr_process_begin_command (self->process, "mv");
		fr_process_add_arg (self->process, "-f");
		fr_process_add_arg (self->process, "--");
		fr_process_add_arg_file (self->process, old_file);
		fr_process_add_arg_file (self->process, new_file);
		fr_process_end_command (self->process);

		g_object_unref (old_file);
		g_object_unref (new_file);
	}

	for (scan = file_list; scan; scan = scan->next) {
		const char *current_dir_relative = current_dir + 1;
		const char *filename = (char*) scan->data;
		GFile      *old_file = NULL;
		GFile      *new_file = NULL;
		char       *common = NULL;
		char       *new_filename;

		old_file = _g_file_append_path (tmp_dir, filename, NULL);

		if (strlen (filename) > (strlen (current_dir) + strlen (old_name)))
			common = g_strdup (filename + strlen (current_dir) + strlen (old_name));
		new_file = _g_file_append_path (tmp_dir, current_dir, new_name, common, NULL);

		if (! is_dir) {
			fr_process_begin_command (self->process, "mv");
			fr_process_add_arg (self->process, "-f");
			fr_process_add_arg (self->process, "--");
			fr_process_add_arg_file (self->process, old_file);
			fr_process_add_arg_file (self->process, new_file);
			fr_process_end_command (self->process);
		}

		new_filename = g_build_filename (current_dir_relative, new_name, common, NULL);
		new_file_list = g_list_prepend (new_file_list, new_filename);

		g_free (common);
		g_object_unref (new_file);
		g_object_unref (old_file);
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

		g_file_delete (tmp_dir, NULL, NULL);

		g_object_unref (result);
		g_error_free (error);
		g_object_unref (tmp_dir);

		return;
	}

	g_free (new_dirname);
	_g_string_list_free (new_file_list);

	/* if the command cannot delete all the files in the archive without
	 * deleting the archive itself ('rar' deletes the archive), delete
	 * the old files here to avoid a potential error. */

	if (! archive->propCanDeleteAllFiles)
		fr_command_rename__delete (archive,
					   &file_list,
					   is_dir,
					   dir_in_archive,
					   original_path);

	/* remove the tmp dir */

	fr_process_begin_command (self->process, "rm");
	fr_process_set_working_dir (self->process, g_get_tmp_dir ());
	fr_process_set_sticky (self->process, TRUE);
	fr_process_add_arg (self->process, "-rf");
	fr_process_add_arg_file (self->process, tmp_dir);
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
			    process_ready_after_changing_archive,
			    xfer_data);

	g_object_unref (tmp_dir);
}


/* -- fr_command_paste_clipboard -- */


static void
fr_command_paste_clipboard (FrArchive           *archive,
		    	    GFile               *archive_file,
		    	    char                *password,
		    	    gboolean             encrypt_header,
		    	    FrCompression        compression,
		    	    guint                volume_size,
		    	    FrClipboardOp        op,
		    	    char                *base_dir,
		    	    GList               *files,
		    	    GFile               *tmp_dir,
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
			fr_process_set_ignore_error (command->process, TRUE);
			fr_process_set_working_dir_file (command->process, tmp_dir);
			fr_process_add_arg (command->process, "-f");
			fr_process_add_arg (command->process, "--");
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
	fr_process_add_arg_file (command->process, tmp_dir);
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
			    process_ready_after_changing_archive,
			    xfer_data);
}


/* -- fr_command_add_dropped_files -- */


static void
fr_command_add_dropped_files (FrArchive           *archive,
		   	      GList               *file_list,
		   	      const char          *dest_dir,
		   	      const char          *password,
		   	      gboolean             encrypt_header,
		   	      FrCompression        compression,
		   	      guint                volume_size,
		   	      GCancellable        *cancellable,
		   	      GAsyncReadyCallback  callback,
		   	      gpointer             user_data)
{
	FrCommand *command = FR_COMMAND (archive);
	FrCommandPrivate *private = fr_command_get_instance_private (command);
	GList     *scan;
	XferData  *xfer_data;

	/* FIXME: doesn't work with remote files */

	fr_archive_set_stoppable (FR_ARCHIVE (command), TRUE);
	command->creating_archive = ! g_file_query_exists (private->local_copy, cancellable);
	g_object_set (command,
	              "filename", private->local_copy,
		      "password", password,
		      "encrypt-header", encrypt_header,
		      "compression", compression,
		      "volume-size", volume_size,
		      NULL);

	fr_process_clear (command->process);
	fr_command_uncompress (command);
	for (scan = file_list; scan; scan = scan->next) {
		GFile *file = G_FILE (scan->data);
		GFile *parent;
		GList *singleton;

		parent = g_file_get_parent (file);
		singleton = g_list_prepend (NULL, g_file_get_basename (file));
		fr_command_add (command,
				NULL,
				singleton,
				parent,
				FALSE,
				FALSE);

		_g_string_list_free (singleton);
		g_object_ref (parent);
	}
	fr_command_recompress (command);

	/**/

	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = _g_object_ref (command);
	xfer_data->cancellable = _g_object_ref (cancellable);
	xfer_data->result = g_simple_async_result_new (G_OBJECT (command),
						       callback,
						       user_data,
						       fr_archive_add_dropped_items);

	fr_process_execute (command->process,
			    cancellable,
			    process_ready_after_changing_archive,
			    xfer_data);
}


/* -- fr_command_update_open_files -- */


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
		GFile *file = G_FILE (scan_file->data);
		GFile *folder = G_FILE (scan_dir->data);
		GList *singleton;

		singleton = g_list_append (NULL, g_file_get_relative_path (folder, file));
		_fr_command_add (self,
				 singleton,
			 	 folder,
				 "/",
				 FALSE,
				 FALSE,
				 password,
				 encrypt_header,
				 compression,
				 volume_size,
				 cancellable,
				 NULL);

		_g_string_list_free (singleton);
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
			    process_ready_after_changing_archive,
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
fr_command_base_handle_error (FrCommand *command,
			      FrError   *error)
{
	/* void */
}


static void
fr_command_class_init (FrCommandClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;

	fr_command_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->set_property = fr_command_set_property;
	gobject_class->get_property = fr_command_get_property;
	gobject_class->finalize = fr_command_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->open = fr_command_open;
	archive_class->list = fr_command_list;
	archive_class->add_files = fr_command_add_files;
	archive_class->remove_files = fr_command_remove_files;
	archive_class->extract_files = fr_command_extract_files;
	archive_class->test_integrity = fr_command_test_integrity;
	archive_class->rename = fr_command_rename;
	archive_class->paste_clipboard = fr_command_paste_clipboard;
	archive_class->add_dropped_files = fr_command_add_dropped_files;
	archive_class->update_open_files = fr_command_update_open_files;

	klass->list = NULL;
	klass->add = NULL;
	klass->delete = NULL;
	klass->extract = NULL;
	klass->test = NULL;
	klass->uncompress = fr_command_base_uncompress;
	klass->recompress = fr_command_base_recompress;
	klass->handle_error = fr_command_base_handle_error;

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

		archive_name = _g_file_get_display_name (remote_file);
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
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	GFile     *file;

	/* we cannot change the local copy if the archive is a multi-volume and
	 * stored on a remote location, to do that we should copy all the
	 * remote parts to a local temporary directory. */
	if (FR_ARCHIVE (self)->multi_volume && private->is_remote)
		return;

	if ((private->local_copy != NULL) && private->is_remote) {
		GFile  *temp_folder;
		GError *err = NULL;

		g_file_delete (private->local_copy, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete the local copy: %s", err->message);
			g_clear_error (&err);
		}

		temp_folder = g_file_get_parent (private->local_copy);
		g_file_delete (temp_folder, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete temp folder: %s", err->message);
			g_clear_error (&err);
		}

		g_object_unref (temp_folder);
	}

	if (private->local_copy != NULL) {
		g_object_unref (private->local_copy);
		private->local_copy = NULL;
	}

	file = fr_archive_get_file (FR_ARCHIVE (object));
	private->is_remote = ! g_file_has_uri_scheme (file, "file");
	if (private->is_remote)
		private->local_copy = get_local_copy_for_file (file);
	else
		private->local_copy = g_file_dup (file);

	_fr_command_set_filename_from_file (self, private->local_copy);
}


static void
fr_command_init (FrCommand *self)
{
	FrCommandPrivate *private = fr_command_get_instance_private (self);
	FrProcess *process;

	private->local_copy = NULL;
	private->is_remote = FALSE;
	private->temp_dir = NULL;
	private->temp_extraction_dir = NULL;
	private->remote_extraction = FALSE;

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
