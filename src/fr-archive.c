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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
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
#include "glib-utils.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "file-data.h"
#include "fr-archive.h"
#include "fr-command.h"
#include "fr-command-ace.h"
#include "fr-command-ar.h"
#include "fr-command-arj.h"
#include "fr-command-cfile.h"
#include "fr-command-cpio.h"
#include "fr-command-iso.h"
#include "fr-command-jar.h"
#include "fr-command-lha.h"
#include "fr-command-rar.h"
#include "fr-command-rpm.h"
#include "fr-command-tar.h"
#include "fr-command-unstuff.h"
#include "fr-command-zip.h"
#include "fr-command-zoo.h"
#include "fr-command-7z.h"
#include "fr-error.h"
#include "fr-marshal.h"
#include "fr-process.h"
#include "utf8-fnmatch.h"

#ifndef NCARGS
#define NCARGS _POSIX_ARG_MAX
#endif


/* -- DroppedItemsData -- */


typedef struct {
	FrArchive     *archive;
	GList         *item_list;
	char          *base_dir;
	char          *dest_dir;
	gboolean       update;
	char          *password;
	FRCompression  compression;
} DroppedItemsData;


static DroppedItemsData *
dropped_items_data_new (FrArchive     *archive,
			GList         *item_list,
			const char    *base_dir,
			const char    *dest_dir,
			gboolean       update,
			const char    *password,
			FRCompression  compression)
{
	DroppedItemsData *data;

	data = g_new0 (DroppedItemsData, 1);
	data->archive = archive;
	data->item_list = path_list_dup (item_list);
	if (base_dir != NULL)
		data->base_dir = g_strdup (base_dir);
	if (dest_dir != NULL)
		data->dest_dir = g_strdup (dest_dir);
	data->update = update;
	if (password != NULL)
		data->password = g_strdup (password);
	data->compression = compression;

	return data;
}


static void
dropped_items_data_free (DroppedItemsData *data)
{
	if (data == NULL)
		return;
	path_list_free (data->item_list);
	g_free (data->base_dir);
	g_free (data->dest_dir);
	g_free (data->password);
	g_free (data);
}


struct _FRArchivePrivData {
	FakeLoadFunc         fake_load_func;                /* If returns TRUE, archives are not read when
							     * fr_archive_load is invoked, used
							     * in batch mode. */
	gpointer             fake_load_data;
	FakeLoadFunc         add_is_stoppable_func;         /* Returns whether the add operation is
							     * stoppable. */
	gpointer             add_is_stoppable_data;
  	GCancellable        *cancellable;
	char                *temp_dir;
	gboolean             continue_adding_dropped_items;
	DroppedItemsData    *dropped_items_data;

	char                *temp_extraction_dir;
	char                *extraction_destination;
	gboolean             remote_extraction;
	gboolean             extract_here;
};


typedef struct {
	FrArchive      *archive;
	char           *uri;
	char           *password;
	FRAction        action;
	GList          *file_list;
	char           *base_uri;
	char           *dest_dir;
	gboolean        update;
	FRCompression   compression;
	char           *tmp_dir;
	guint           source_id;
} XferData;


static void
xfer_data_free (XferData *data)
{
	if (data == NULL)
		return;

	g_free (data->uri);
	g_free (data->password);
	path_list_free (data->file_list);
	g_free (data->base_uri);
	g_free (data->dest_dir);
	g_free (data->tmp_dir);
	g_free (data);
}


#define MAX_CHUNK_LEN (NCARGS * 2 / 3) /* Max command line length */
#define UNKNOWN_TYPE "application/octet-stream"
#define SAME_FS (FALSE)
#define NO_BACKUP_FILES (FALSE)
#define NO_DOT_FILES (FALSE)
#define IGNORE_CASE (FALSE)


enum {
	START,
	DONE,
	PROGRESS,
	MESSAGE,
	STOPPABLE,
	LAST_SIGNAL
};

static GObjectClass *parent_class;
static guint fr_archive_signals[LAST_SIGNAL] = { 0 };

static void fr_archive_class_init (FrArchiveClass *class);
static void fr_archive_init       (FrArchive *archive);
static void fr_archive_finalize   (GObject *object);


GType
fr_archive_get_type (void)
{
	static GType type = 0;

	if (! type) {
		static const GTypeInfo type_info = {
			sizeof (FrArchiveClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_archive_class_init,
			NULL,
			NULL,
			sizeof (FrArchive),
			0,
			(GInstanceInitFunc) fr_archive_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "FrArchive",
					       &type_info,
					       0);
	}

	return type;
}


static void
fr_archive_class_init (FrArchiveClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	fr_archive_signals[START] =
		g_signal_new ("start",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);
	fr_archive_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, done),
			      NULL, NULL,
			      fr_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT,
			      G_TYPE_POINTER);
	fr_archive_signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, progress),
			      NULL, NULL,
			      fr_marshal_VOID__DOUBLE,
			      G_TYPE_NONE, 1,
			      G_TYPE_DOUBLE);
	fr_archive_signals[MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, message),
			      NULL, NULL,
			      fr_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	fr_archive_signals[STOPPABLE] =
		g_signal_new ("stoppable",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, stoppable),
			      NULL, NULL,
			      fr_marshal_VOID__BOOL,
			      G_TYPE_NONE,
			      1, G_TYPE_BOOLEAN);

	gobject_class->finalize = fr_archive_finalize;
	class->start = NULL;
	class->done = NULL;
	class->progress = NULL;
	class->message = NULL;
}


void
fr_archive_stoppable (FrArchive *archive,
		      gboolean   stoppable)
{
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[STOPPABLE],
		       0,
		       stoppable);
}


void
fr_archive_stop (FrArchive *archive)
{
	if (archive->process != NULL) {
		fr_process_stop (archive->process);
		return;
	}
	
	if (! g_cancellable_is_cancelled (archive->priv->cancellable)) 
		g_cancellable_cancel (archive->priv->cancellable);
}


void
fr_archive_action_completed (FrArchive       *archive,
			     FRAction         action,
			     FRProcErrorType  error_type,
			     const char      *error_details)
{
	archive->error.type = error_type;
	archive->error.status = 0;
	g_clear_error (&archive->error.gerror);
	if (error_details != NULL)
		archive->error.gerror = g_error_new (fr_error_quark (),
						     0,
						     error_details);
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[DONE],
		       0,
		       action,
		       &archive->error);
}


static gboolean
fr_archive_add_is_stoppable (FrArchive *archive)
{
	if (archive->priv->add_is_stoppable_func != NULL)
		return (*archive->priv->add_is_stoppable_func) (archive, archive->priv->add_is_stoppable_data);
	else
		return FALSE;
}


static gboolean
archive_sticky_only_cb (FrProcess *process,
			FrArchive *archive)
{
	fr_archive_stoppable (archive, fr_archive_add_is_stoppable (archive));
	return TRUE;
}


static void
fr_archive_init (FrArchive *archive)
{
	archive->file = NULL;
	archive->local_copy = NULL;
	archive->is_remote = FALSE;
	archive->command = NULL;
	archive->is_compressed_file = FALSE;
	archive->can_create_compressed_file = FALSE;

	archive->priv = g_new0 (FrArchivePrivData, 1);
	archive->priv->fake_load_func = NULL;
	archive->priv->fake_load_data = NULL;
	archive->priv->add_is_stoppable_func = NULL;
	archive->priv->add_is_stoppable_data = NULL;

	archive->priv->extraction_destination = NULL;
	archive->priv->temp_extraction_dir = NULL;
	archive->priv->cancellable = g_cancellable_new ();
	
	archive->process = fr_process_new ();
	g_signal_connect (G_OBJECT (archive->process),
			  "sticky_only",
			  G_CALLBACK (archive_sticky_only_cb),
			  archive);
}


FrArchive *
fr_archive_new (void)
{
	return FR_ARCHIVE (g_object_new (FR_TYPE_ARCHIVE, NULL));
}


static GFile *
get_local_copy_for_file (GFile *remote_file)
{
	char  *temp_dir;
	GFile *local_copy = NULL;

	temp_dir = get_temp_work_dir ();
	if (temp_dir != NULL) {
		char  *archive_name;
		char  *local_uri;

		archive_name = g_file_get_basename (remote_file);
		local_uri = g_build_filename (temp_dir, archive_name, NULL);
		local_copy = g_file_new_for_uri (local_uri);
	
		g_free (local_uri);
		g_free (archive_name);
	}
	g_free (temp_dir);

	return local_copy;
}


static void
fr_archive_set_uri (FrArchive  *archive,
		    const char *uri)
{
	if ((archive->local_copy != NULL) && archive->is_remote) {
		GFile  *temp_folder;
		GError *err = NULL;

		g_file_delete (archive->local_copy, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete the local copy: %s", err->message);
			g_clear_error (&err);
		}
		
		temp_folder = g_file_get_parent (archive->local_copy);
		g_file_delete (temp_folder, NULL, &err);
		if (err != NULL) {
			g_warning ("Failed to delete temp folder: %s", err->message);
			g_clear_error (&err);
		}
		
		g_object_unref (temp_folder);
	}

	if (archive->file != NULL) {
		g_object_unref (archive->file);
		archive->file = NULL;
	}
	if (archive->local_copy != NULL) {
		g_object_unref (archive->local_copy);
		archive->local_copy = NULL;
	}
	archive->content_type = NULL;

	if (uri == NULL)
		return;

	archive->file = g_file_new_for_uri (uri);
	archive->is_remote = ! g_file_has_uri_scheme (archive->file, "file");
	if (archive->is_remote)
		archive->local_copy = get_local_copy_for_file (archive->file);
	else
		archive->local_copy = g_file_dup (archive->file);
}


static void
fr_archive_remove_temp_work_dir (FrArchive *archive)
{
	if (archive->priv->temp_dir == NULL)
		return;
	remove_local_directory (archive->priv->temp_dir);
	g_free (archive->priv->temp_dir);
	archive->priv->temp_dir = NULL;
}


static void
fr_archive_finalize (GObject *object)
{
	FrArchive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_ARCHIVE (object));

	archive = FR_ARCHIVE (object);

	fr_archive_set_uri (archive, NULL);
	fr_archive_remove_temp_work_dir (archive);
	if (archive->command != NULL)
		g_object_unref (archive->command);
	g_object_unref (archive->process);
	if (archive->priv->dropped_items_data != NULL) {
		dropped_items_data_free (archive->priv->dropped_items_data);
		archive->priv->dropped_items_data = NULL;
	}
	g_free (archive->priv->temp_extraction_dir);
	g_free (archive->priv->extraction_destination);
	g_free (archive->priv);

	/* Chain up */

	if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* filename must not be escaped. */
static gboolean
create_command_from_mime_type (FrArchive  *archive,
			       const char *filename,
			       const char *mime_type)
{
	archive->is_compressed_file = FALSE;

	if (is_mime_type (mime_type, "application/x-tar")) {
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_NONE);
	} else if (is_mime_type (mime_type, "application/x-compressed-tar")) {
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_GZIP);
	} else if (is_mime_type (mime_type, "application/x-bzip-compressed-tar")) {
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_BZIP2);
	} else if (is_mime_type (mime_type, "application/zip") ||
		   is_mime_type (mime_type, "application/x-zip") ||
		   is_mime_type (mime_type, "application/octet-stream")) {
		archive->command = fr_command_zip_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-zoo")) {
		archive->command = fr_command_zoo_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-rar")) {
		archive->command = fr_command_rar_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-arj")) {
		archive->command = fr_command_arj_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-stuffit")) {
		archive->command = fr_command_unstuff_new (archive->process,
							   filename);
	} else if (is_mime_type (mime_type, "application/x-rpm")) {
		archive->command = fr_command_rpm_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-cd-image")) {
		archive->command = fr_command_iso_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-deb") ||
		   is_mime_type (mime_type, "application/x-ar")) {
		archive->command = fr_command_ar_new (archive->process,
						      filename);
	} else if (is_mime_type (mime_type, "application/x-ace")) {
		archive->command = fr_command_ace_new (archive->process,
						       filename);
	} else if (is_mime_type (mime_type, "application/x-7zip")) {
		archive->command = fr_command_7z_new (archive->process,
						      filename);
	} else if (is_mime_type (mime_type, "application/x-cpio")) {
		archive->command = fr_command_cpio_new (archive->process,
							filename);
	} else
		return FALSE;

	return (archive->command != NULL);
}


/* filename must not be escaped. */
static const char *
get_mime_type_from_content (const char *filename)
{
	const char *mime_type;

	mime_type = get_file_mime_type (filename, FALSE);
	if (g_content_type_is_unknown (mime_type))
		return NULL;

	return mime_type;
}


static gboolean
hexcmp (const char *first_bytes,
	const char *buffer,
	int         len)
{
	int i;

	for (i = 0; i < len; i++)
		if (first_bytes[i] != buffer[i])
			return FALSE;

	return TRUE;
}


/* filename must not be escaped. */
static const char *
get_mime_type_from_sniffer (const char *filename)
{
	static struct {
		const char *mime_type;
		const char *first_bytes;
		int         len;
	} sniffer_data [] = {
		{"application/zip",                   "\x50\x4B\x03\x04", 4},
		/* FIXME
		   {"application/x-compressed-tar",      "\x1F\x8B\x08\x08", 4},
		   {"application/x-bzip-compressed-tar", "\x42\x5A\x68\x39", 4},
		 */
		{ NULL, NULL, 0 }
	};
	FILE        *file;
	char         buffer[5];
	int          n, i;

	file = fopen (filename, "rb");

	if (file == NULL)
		return NULL;

	if (file_extension_is (filename, ".jar"))
		return NULL;

	n = fread (buffer, sizeof (char), sizeof (buffer) - 1, file);
	buffer[n] = 0;

	fclose (file);

	for (i = 0; sniffer_data[i].mime_type != NULL; i++) {
		const char *first_bytes = sniffer_data[i].first_bytes;
		int         len         = sniffer_data[i].len;

		if (hexcmp (first_bytes, buffer, len))
			return sniffer_data[i].mime_type;
	}

	return NULL;
}


/* filename must not be escaped. */
static gboolean
create_command_from_filename (FrArchive *archive,
			      gboolean   loading)
{
	char *filename;
	
	filename = g_file_get_path (archive->local_copy);
	archive->is_compressed_file = FALSE;
	
	if (file_extension_is (filename, ".tar.gz")
	    || file_extension_is (filename, ".tgz")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_GZIP);
	}
	else if (file_extension_is (filename, ".tar.bz2")
	         || file_extension_is (filename, ".tbz2")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_BZIP2);
	}
	else if (file_extension_is (filename, ".tar.bz")
	         || file_extension_is (filename, ".tbz")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_BZIP);
	}
	else if (file_extension_is (filename, ".tar.Z")
	         || file_extension_is (filename, ".taz")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_COMPRESS);
	}
	else if (file_extension_is (filename, ".tar.lzma")
	         || file_extension_is (filename, ".tzma")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_LZMA);
	}
	else if (file_extension_is (filename, ".tar.lzo")
		 || file_extension_is (filename, ".tzo")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_LZOP);
	}
	else if (file_extension_is (filename, ".tar")) 
	{
		archive->command = fr_command_tar_new (archive->process,
						       filename,
						       FR_COMPRESS_PROGRAM_NONE);
	}
	else if (file_extension_is (filename, ".zip")
	         || file_extension_is (filename, ".ear")
	         || file_extension_is (filename, ".war")
	         || file_extension_is (filename, ".exe")) 
	{
		archive->command = fr_command_zip_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".jar")) {
		archive->command = fr_command_jar_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".zoo")) {
		archive->command = fr_command_zoo_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".lzh")
	         || file_extension_is (filename, ".lha")) 
	{
		archive->command = fr_command_lha_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".rar")) {
		archive->command = fr_command_rar_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".arj")) {
		archive->command = fr_command_arj_new (archive->process,
						       filename);
	}
	else if (file_extension_is (filename, ".ar")) {
		archive->command = fr_command_ar_new (archive->process,
						      filename);
	}
	else if (file_extension_is (filename, ".7z")) {
		archive->command = fr_command_7z_new (archive->process,
						      filename);
	}
	else if (loading || archive->can_create_compressed_file) {
		if (file_extension_is (filename, ".gz")
		    || file_extension_is (filename, ".z")
		    || file_extension_is (filename, ".Z")) 
		{
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_GZIP);
			archive->is_compressed_file = TRUE;
		}
		else if (file_extension_is (filename, ".bz")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_BZIP);
			archive->is_compressed_file = TRUE;
		}
		else if (file_extension_is (filename, ".bz2")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_BZIP2);
			archive->is_compressed_file = TRUE;
		}
		else if (file_extension_is (filename, ".lzma")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_LZMA);
			archive->is_compressed_file = TRUE;
		}
		else if (file_extension_is (filename, ".lzo")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_LZOP);
			archive->is_compressed_file = TRUE;
		}
		else if (file_extension_is (filename, ".cpio")) {
			archive->command = fr_command_cpio_new (archive->process,
								filename);
		}
		else if (! archive->can_create_compressed_file) {
			if (file_extension_is (filename, ".bin")
		            || file_extension_is (filename, ".sit")) 
		        {
				archive->command = fr_command_unstuff_new (archive->process,
								           filename);
			}
			else if (file_extension_is (filename, ".rpm")) {
				archive->command = fr_command_rpm_new (archive->process,
								       filename);
			}
			else if (file_extension_is (filename, ".iso")) {
				archive->command = fr_command_iso_new (archive->process,
							      	       filename);
			} 
			else if (file_extension_is (filename, ".deb")) {
				archive->command = fr_command_ar_new (archive->process,
							              filename);
			}
			else if (file_extension_is (filename, ".ace")) {
				archive->command = fr_command_ace_new (archive->process,
							       	       filename);
			}
		}
	}
	
	g_free (filename);
	
	return (archive->command != NULL);
}


static void
action_started (FrCommand *command,
		FRAction   action,
		FrArchive *archive)
{
#ifdef DEBUG
	debug (DEBUG_INFO, "%s [START] (FR::Archive)\n", action_names[action]);
#endif

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       action);
}


/* -- copy_to_remote_location -- */


static void
fr_archive_copy_done (FrArchive *archive,
		      FRAction   action,
		      GError    *error)
{
	FRProcErrorType  error_type = FR_PROC_ERROR_NONE;
	const char      *error_details = NULL;

	if (error != NULL) {
		error_type = (g_error_matches (error, G_FILE_ERROR, G_IO_ERROR_CANCELLED) ? FR_PROC_ERROR_STOPPED : FR_PROC_ERROR_GENERIC);
		error_details = error->message;
	}
	fr_archive_action_completed (archive, action, error_type, error_details);
}

                                  
static void
copy_to_remote_location_done (GError   *error,
			      gpointer  user_data)
{
	XferData *xfer_data = user_data;

	fr_archive_copy_done (xfer_data->archive, xfer_data->action, error);
	xfer_data_free (xfer_data);
}


static void 
copy_to_remote_location_progress (goffset   current_file,
                                  goffset   total_files,
                                  GFile    *source,
                                  GFile    *destination,
                                  goffset   current_num_bytes,
                                  goffset   total_num_bytes,
                                  gpointer  user_data)
{
	XferData *xfer_data = user_data;
	
	g_signal_emit (G_OBJECT (xfer_data->archive),
		       fr_archive_signals[PROGRESS],
		       0,
		       (double) current_num_bytes / total_num_bytes);
}
				      

static void
copy_to_remote_location (FrArchive  *archive,
			 FRAction    action)
{
	XferData *xfer_data;
	
	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = archive;
	xfer_data->action = action;
	
	g_copy_file_async (archive->local_copy,
			   archive->file,
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   archive->priv->cancellable,
			   copy_to_remote_location_progress,
			   xfer_data,
			   copy_to_remote_location_done,
			   xfer_data);
}


/* -- copy_extracted_files_to_destination -- */


static void
move_here (FrArchive *archive) 
{
	char   *content_uri;
	char   *parent;
	char   *parent_parent;
	char   *new_content_uri;
	GFile  *source, *destination, *parent_file;
	GError *error;

	content_uri = get_dir_content_if_unique (archive->priv->extraction_destination);
	if (content_uri == NULL)
		return;	

	parent = remove_level_from_path (content_uri);
		
	if (uricmp (parent, archive->priv->extraction_destination) == 0) {
		char *new_uri;
		
		new_uri = get_alternative_uri_for_uri (archive->priv->extraction_destination);
		 
		source = g_file_new_for_uri (archive->priv->extraction_destination);
		destination = g_file_new_for_uri (new_uri);
		if (! g_file_move (source, destination, 0, NULL, NULL, NULL, &error)) {
			g_warning ("could not rename %s to %s: %s", archive->priv->extraction_destination, new_uri, error->message);
			g_clear_error (&error);
		}
		g_object_unref (source);
		g_object_unref (destination);
				
		g_free (archive->priv->extraction_destination);
		archive->priv->extraction_destination = new_uri;

		g_free (parent);
		
		content_uri = get_dir_content_if_unique (archive->priv->extraction_destination);
		if (content_uri == NULL)
			return;	

		parent = remove_level_from_path (content_uri);
	}

	parent_parent = remove_level_from_path (parent);
	new_content_uri = get_alternative_uri (parent_parent, file_name_from_path (content_uri));
	
	source = g_file_new_for_uri (content_uri);
	destination = g_file_new_for_uri (new_content_uri);
	if (! g_file_move (source, destination, 0, NULL, NULL, NULL, &error)) {
		g_warning ("could not rename %s to %s: %s", content_uri, new_content_uri, error->message);
		g_clear_error (&error);
	}
	
	parent_file = g_file_new_for_uri (parent);
	if (! g_file_delete (parent_file, NULL, &error)) {
		g_warning ("could not remove directory %s: %s", parent, error->message);
		g_clear_error (&error);
	}
	g_object_unref (parent_file);
	
	g_free (archive->priv->extraction_destination);
	archive->priv->extraction_destination = new_content_uri;
	
	g_free (parent_parent);
	g_free (parent);
	g_free (content_uri);
}


static void
copy_extracted_files_done (GError   *error,
			   gpointer  user_data)
{
	FrArchive *archive = user_data;
	
	remove_local_directory (archive->priv->temp_extraction_dir);
	g_free (archive->priv->temp_extraction_dir);
	archive->priv->temp_extraction_dir = NULL;
	
	fr_archive_action_completed (archive,
				     FR_ACTION_COPYING_FILES_TO_REMOTE,
				     FR_PROC_ERROR_NONE,
				     NULL);
 
	if ((error == NULL) && (archive->priv->extract_here))
		move_here (archive); 
	
	fr_archive_copy_done (archive, FR_ACTION_EXTRACTING_FILES, error);	
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
	
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[PROGRESS],
		       0,
		       (double) current_file / (total_files + 1));
}


static void
copy_extracted_files_to_destination (FrArchive *archive)
{
	char *temp_extraction_dir;
	
	temp_extraction_dir = get_uri_from_local_path (archive->priv->temp_extraction_dir);
	g_directory_copy_async (temp_extraction_dir,
				archive->priv->extraction_destination,
				G_FILE_COPY_OVERWRITE,
				G_PRIORITY_DEFAULT,
				archive->priv->cancellable,
				copy_extracted_files_progress,
				archive,
				copy_extracted_files_done,
				archive);
	g_free (temp_extraction_dir);
}


static void add_dropped_items (DroppedItemsData *data);


static void
action_performed (FrCommand   *command,
		  FRAction     action,
		  FRProcError *error,
		  FrArchive   *archive)
{
#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Archive)\n", action_names[action]);
#endif

	switch (action) {
	case FR_ACTION_DELETING_FILES:
		if (error->type == FR_PROC_ERROR_NONE) {
			copy_to_remote_location (archive, action);
			return;
		}
		break;
		
	case FR_ACTION_ADDING_FILES:
		if (error->type == FR_PROC_ERROR_NONE) {
			fr_archive_remove_temp_work_dir (archive);
			if (archive->priv->continue_adding_dropped_items) {
				add_dropped_items (archive->priv->dropped_items_data);
				return;
			}
			if (archive->priv->dropped_items_data != NULL) {
				dropped_items_data_free (archive->priv->dropped_items_data);
				archive->priv->dropped_items_data = NULL;
			}
			if (! g_file_has_uri_scheme (archive->file, "file")) {
				copy_to_remote_location (archive, action);
				return;
			}
		}
		break;
		
	case FR_ACTION_EXTRACTING_FILES:
		if (error->type == FR_PROC_ERROR_NONE) {
			if  (archive->priv->remote_extraction) {
				copy_extracted_files_to_destination (archive);
				return;
			}
			else if (archive->priv->extract_here)
				move_here (archive); 
		}
		else {
			/* if an error occurred during extraction remove the 
			 * temp extraction dir, if used. */
			
			if ((archive->priv->remote_extraction) && (archive->priv->temp_extraction_dir != NULL)) {
				remove_local_directory (archive->priv->temp_extraction_dir);
				g_free (archive->priv->temp_extraction_dir);
				archive->priv->temp_extraction_dir = NULL;
			}
			
			if (archive->priv->extract_here) 
				remove_directory (archive->priv->extraction_destination);
		}	
		break;
		
	default:
		/* nothing */
		break;
	}

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[DONE],
		       0,
		       action,
		       error);
}


static gboolean
archive_progress_cb (FrCommand *command,
		     double     fraction,
		     FrArchive *archive)
{
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[PROGRESS],
		       0,
		       fraction);
	return TRUE;
}


static gboolean
archive_message_cb  (FrCommand  *command,
		     const char *msg,
		     FrArchive  *archive)
{
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[MESSAGE],
		       0,
		       msg);
	return TRUE;
}


/* filename must not be escaped. */
gboolean
fr_archive_create (FrArchive  *archive,
		   const char *uri)
{
	FrCommand *tmp_command;

	if (uri == NULL)
		return FALSE;

	fr_archive_set_uri (archive, uri);

	tmp_command = archive->command;
	if (! create_command_from_filename (archive, FALSE)) {
		archive->command = tmp_command;
		return FALSE;
	}

	if (tmp_command != NULL)
		g_object_unref (G_OBJECT (tmp_command));

	archive->read_only = FALSE;

	g_signal_connect (G_OBJECT (archive->command),
			  "start",
			  G_CALLBACK (action_started),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "done",
			  G_CALLBACK (action_performed),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "progress",
			  G_CALLBACK (archive_progress_cb),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "message",
			  G_CALLBACK (archive_message_cb),
			  archive);

	return TRUE;
}


void
fr_archive_set_fake_load_func (FrArchive    *archive,
			       FakeLoadFunc  func,
			       gpointer      data)
{
	archive->priv->fake_load_func = func;
	archive->priv->fake_load_data = data;
}


gboolean
fr_archive_fake_load (FrArchive *archive)
{
	if (archive->priv->fake_load_func != NULL)
		return (*archive->priv->fake_load_func) (archive, archive->priv->fake_load_data);
	else
		return FALSE;
}


/* -- fr_archive_load -- */


static void
load_local_archive (FrArchive  *archive,
		    const char *uri,
		    const char *password)
{
	FrCommand  *tmp_command;
	char       *filename;
	const char *mime_type = NULL;

	archive->read_only = ! check_permissions (uri, W_OK);

	/* find mime type */

	tmp_command = archive->command;

	filename = g_file_get_path (archive->local_copy);
	mime_type = get_mime_type_from_sniffer (filename);
	if (mime_type == NULL)
		mime_type = get_mime_type_from_content (filename);
	if ((mime_type == NULL) || ! create_command_from_mime_type (archive, filename, mime_type)) {
		if (! create_command_from_filename (archive, TRUE)) {
			archive->command = tmp_command;
			fr_archive_action_completed (archive,
						     FR_ACTION_LOADING_ARCHIVE, 
						     FR_PROC_ERROR_GENERIC,
						     _("Archive type not supported."));
			g_free (filename);
			return;
		}
	}
	g_free (filename);

	archive->content_type = mime_type;

	if (tmp_command != NULL) {
		g_signal_handlers_disconnect_by_data (tmp_command, archive);
		g_object_unref (tmp_command);
	}

	if ((archive->command->file_type == FR_FILE_TYPE_ZIP)
	    && (! is_program_in_path ("zip")))
		archive->read_only = TRUE;

	g_signal_connect (G_OBJECT (archive->command),
			  "start",
			  G_CALLBACK (action_started),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "done",
			  G_CALLBACK (action_performed),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "progress",
			  G_CALLBACK (archive_progress_cb),
			  archive);
	g_signal_connect (G_OBJECT (archive->command),
			  "message",
			  G_CALLBACK (archive_message_cb),
			  archive);

	fr_archive_stoppable (archive, TRUE);
	archive->command->fake_load = fr_archive_fake_load (archive);

	fr_archive_action_completed (archive,
				     FR_ACTION_LOADING_ARCHIVE, 
				     FR_PROC_ERROR_NONE, 
				     NULL);

	/**/

	fr_process_clear (archive->process);
	fr_command_list (archive->command, password);
	fr_process_start (archive->process);	
}


static void
copy_remote_file_done (GError   *error,
		       gpointer  user_data)
{
	XferData *xfer_data = user_data;

	if (error != NULL)
		fr_archive_copy_done (xfer_data->archive, FR_ACTION_LOADING_ARCHIVE, error);
	else	
		load_local_archive (xfer_data->archive, xfer_data->uri, xfer_data->password);
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
	
	g_signal_emit (G_OBJECT (xfer_data->archive),
		       fr_archive_signals[PROGRESS],
		       0,
		       (double) current_num_bytes / total_num_bytes);
}


static gboolean
copy_remote_file_done_cb (gpointer user_data)
{
	XferData *xfer_data = user_data;
	
	g_source_remove (xfer_data->source_id);
	copy_remote_file_done (NULL, (XferData *)user_data);
	return FALSE;
}


static void
copy_remote_file (FrArchive  *archive,
		  const char *password)
{
	XferData *xfer_data;
	
	xfer_data = g_new0 (XferData, 1);
	xfer_data->archive = archive;
	xfer_data->uri = g_file_get_uri (archive->file);
	if (password != NULL)
		xfer_data->password = g_strdup (password);
	
	if (! archive->is_remote) {
		xfer_data->source_id = g_idle_add (copy_remote_file_done_cb, xfer_data);
		return;
	}
	
	g_copy_file_async (archive->file,
			   archive->local_copy,
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   archive->priv->cancellable,
			   copy_remote_file_progress,
			   xfer_data,
			   copy_remote_file_done,
			   xfer_data);
}


/* filename must not be escaped. */
gboolean
fr_archive_load (FrArchive  *archive,
		 const char *uri,
		 const char *password)
{
	g_return_val_if_fail (archive != NULL, FALSE);

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_LOADING_ARCHIVE);

	fr_archive_set_uri (archive, uri);
	copy_remote_file (archive, password);

	return TRUE;
}


gboolean
fr_archive_load_local (FrArchive  *archive,
		       const char *uri,
		       const char *password)
{
	g_return_val_if_fail (archive != NULL, FALSE);

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_LOADING_ARCHIVE);

	fr_archive_set_uri (archive, uri);
	load_local_archive (archive, uri, password);
	
	return TRUE;	
}


void
fr_archive_reload (FrArchive  *archive,
		   const char *password)
{
	char *uri;
	
	g_return_if_fail (archive != NULL);
	g_return_if_fail (archive->file != NULL);

	fr_archive_stoppable (archive, TRUE);
	archive->command->fake_load = fr_archive_fake_load (archive);

	uri = g_file_get_uri (archive->file);
	fr_archive_load (archive, uri, password);
	g_free (uri);
}


/* filename must not be escaped. */
void
fr_archive_rename (FrArchive  *archive,
		   const char *filename)
{
	g_return_if_fail (archive != NULL);

	if (archive->is_compressed_file)
		/* If the archive is a compressed file we have to reload it,
		 * because in this case the 'content' of the archive changes
		 * too. */
		fr_archive_load (archive, filename, NULL);

	else {
		g_object_unref (archive->file);
		archive->file = g_file_new_for_path (filename);

		fr_command_set_filename (archive->command, filename);
	}
}


/* -- add -- */


static char *
create_tmp_base_dir (const char *base_dir,
		     const char *dest_path)
{
	char *dest_dir;
	char *temp_dir;
	char *tmp;
	char *parent_dir, *dir;

	if ((dest_path == NULL)
	    || (*dest_path == '\0')
	    || (strcmp (dest_path, "/") == 0))
		return g_strdup (base_dir);

	dest_dir = g_strdup (dest_path);
	if (dest_dir[strlen (dest_dir) - 1] == G_DIR_SEPARATOR)
		dest_dir[strlen (dest_dir) - 1] = 0;

	debug (DEBUG_INFO, "base_dir: %s\n", base_dir);
	debug (DEBUG_INFO, "dest_dir: %s\n", dest_dir);

	temp_dir = get_temp_work_dir ();
	tmp = remove_level_from_path (dest_dir);
	parent_dir =  g_build_filename (temp_dir, tmp, NULL);
	g_free (tmp);

	ensure_dir_exists (parent_dir, 0700, NULL);

	debug (DEBUG_INFO, "mkdir %s\n", parent_dir);

	g_free (parent_dir);

	dir = g_build_filename (temp_dir, "/", dest_dir, NULL);
	symlink (base_dir, dir);

	debug (DEBUG_INFO, "symlink %s --> %s\n", dir, base_dir);

	g_free (dir);
	g_free (dest_dir);

	return temp_dir;
}


/* Note: all paths unescaped. */
static FileData *
find_file_in_archive (FrArchive *archive,
		      char      *path)
{
	int i;

	g_return_val_if_fail (path != NULL, NULL);

	for (i = 0; i < archive->command->files->len; i++) {
		FileData *fdata = g_ptr_array_index (archive->command->files, i);
		if (strcmp (path, fdata->original_path) == 0)
			return fdata;
	}

	return NULL;
}


static void archive_remove (FrArchive *archive, GList *file_list);


static GList *
escape_file_list (FrCommand *command,
		  GList     *file_list)
{
	GList *e_file_list = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;
		char *escape = fr_command_escape (command, filename);
		e_file_list = g_list_prepend (e_file_list, escape);
	}

	return e_file_list;
}


static GList *
shell_escape_file_list (FrCommand *command,
			GList     *file_list)
{
	GList *e_file_list = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;
		char *escape = shell_escape (filename);
		e_file_list = g_list_prepend (e_file_list, escape);
	}

	return e_file_list;
}


/* Note: all paths unescaped. */
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
		FileData *fdata;

		fdata = find_file_in_archive (archive, filename);

		if (fdata == NULL) {
			newer_files = g_list_prepend (newer_files, scan->data);
			continue;
		}

		fullpath = g_strconcat (base_dir, "/", filename, NULL);

		if (uri_exists (fullpath)
		    && (fdata->modified >= get_file_mtime (fullpath))) {
			g_free (fullpath);
			continue;
		}

		newer_files = g_list_prepend (newer_files, scan->data);
		g_free (fullpath);
	}

	return newer_files;
}


void
fr_archive_set_add_is_stoppable_func (FrArchive     *archive,
				      FakeLoadFunc   func,
				      gpointer       data)
{
	archive->priv->add_is_stoppable_func = func;
	archive->priv->add_is_stoppable_data = data;
}


static GList *
convert_to_local_file_list (GList *file_list)
{
	GList *local_file_list = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char *uri = scan->data;
		char *local_filename;
		
		local_filename = get_local_path_from_uri (uri);
		if (local_filename != NULL)
			local_file_list = g_list_prepend (local_file_list, local_filename);
	}

	return local_file_list;
}


/* Note: all paths unescaped. */
void
fr_archive_add (FrArchive     *archive,
		GList         *file_list,
		const char    *base_dir,
		const char    *dest_dir,
		gboolean       update,
		const char    *password,
		FRCompression  compression)
{
	GList    *new_file_list = NULL;
	gboolean  base_dir_created = FALSE;
	GList    *e_file_list;
	GList    *scan;
	char     *tmp_base_dir = NULL;

	if (file_list == NULL)
		return;

	if (archive->read_only)
		return;

	fr_archive_stoppable (archive, fr_archive_add_is_stoppable (archive));

	file_list = convert_to_local_file_list (file_list);
	tmp_base_dir = g_strdup (base_dir);

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
		path_list_free (file_list);
	}
	else
		new_file_list = file_list;

	/* if the command cannot update,  get the list of files that are
	 * newer than the ones in the archive. */

	if (update && ! archive->command->propAddCanUpdate) {
		GList *tmp_file_list;

		tmp_file_list = new_file_list;
		new_file_list = newer_files_only (archive, tmp_file_list, tmp_base_dir);
		path_list_free (tmp_file_list);
	}

	if (new_file_list == NULL) {
		debug (DEBUG_INFO, "nothing to update.\n");

		if (base_dir_created)
			remove_local_directory (tmp_base_dir);
		g_free (tmp_base_dir);

		archive->process->error.type = FR_PROC_ERROR_NONE;
		g_signal_emit_by_name (G_OBJECT (archive->process),
				       "done",
				       FR_ACTION_ADDING_FILES,
				       &archive->process->error);
		return;
	}

	fr_command_uncompress (archive->command);

	/* when files are already present in a tar archive and are added
	 * again, they are not replaced, so we have to delete them first. */

	/* if we are adding (== ! update) and 'add' cannot replace or
	 * if we are updating and 'add' cannot update,
	 * delete the files first. */

	if ((! update && ! archive->command->propAddCanReplace)
	    || (update && ! archive->command->propAddCanUpdate)) {
		GList *del_list = NULL;

		for (scan = new_file_list; scan != NULL; scan = scan->next) {
			char *filename = scan->data;
			if (find_file_in_archive (archive, filename))
				del_list = g_list_prepend (del_list, filename);
		}

		/* delete */

		if (del_list != NULL) {
			archive_remove (archive, del_list);
			fr_process_set_ignore_error (archive->process, TRUE);
			g_list_free (del_list);
		}
	}

	/* add now. */

	e_file_list = shell_escape_file_list (archive->command, new_file_list);
	fr_command_set_n_files (archive->command, g_list_length (e_file_list));

	for (scan = e_file_list; scan != NULL; ) {
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
		fr_command_add (archive->command,
				chunk_list,
				tmp_base_dir,
				update,
				password,
				compression);
		prev->next = scan;
	}

	path_list_free (e_file_list);
	g_list_free (new_file_list);

	fr_command_recompress (archive->command, compression);

	if (base_dir_created) { /* remove the temp dir */
		fr_process_begin_command (archive->process, "rm");
		fr_process_set_working_dir (archive->process, g_get_tmp_dir());
		fr_process_set_sticky (archive->process, TRUE);
		fr_process_add_arg (archive->process, "-rf");
		fr_process_add_arg (archive->process, tmp_base_dir);
		fr_process_end_command (archive->process);
	}

	g_free (tmp_base_dir);
}


/* Note: all paths unescaped. */
static void
fr_archive_add_local_files (FrArchive     *archive,
			    GList         *file_list,
			    const char    *base_dir,
			    const char    *dest_dir,
			    gboolean       update,
			    const char    *password,
			    FRCompression  compression)
{
	fr_archive_stoppable (archive, TRUE);
	fr_process_clear (archive->process);
	fr_archive_add (archive,
			file_list,
			base_dir,
			dest_dir,
			update,
			password,
			compression);
	fr_process_start (archive->process);
}


static void
copy_remote_files_done (GError   *error,
			gpointer  user_data)
{
	XferData *xfer_data = user_data;
	
	fr_archive_copy_done (xfer_data->archive, FR_ACTION_COPYING_FILES_FROM_REMOTE, error);
	
	if (error == NULL) 		
		fr_archive_add_local_files (xfer_data->archive,
					    xfer_data->file_list,
					    xfer_data->tmp_dir,
					    xfer_data->dest_dir,
					    FALSE,
					    xfer_data->password,
					    xfer_data->compression);
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
	
	g_signal_emit (G_OBJECT (xfer_data->archive),
		       fr_archive_signals[PROGRESS],
		       0,
		       (double) current_file / (total_files + 1));
}


static void
copy_remote_files (FrArchive     *archive,
		   GList         *file_list,
		   const char    *base_uri,
		   const char    *dest_dir,
		   gboolean       update,
		   const char    *password,
		   FRCompression  compression,
		   const char    *tmp_dir)
{
	GList      *sources = NULL, *destinations = NULL;
	GHashTable *created_folders;
	GList      *scan;
	XferData   *xfer_data;
	
	created_folders = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
	for (scan = file_list; scan; scan = scan->next) {
		char  *partial_filename = scan->data;
		char  *local_uri;
		char  *local_folder_uri;
		char  *remote_uri;
		
		local_uri = g_strconcat ("file://", tmp_dir, "/", partial_filename, NULL);
		local_folder_uri = remove_level_from_path (local_uri);
		if (g_hash_table_lookup (created_folders, local_folder_uri) == NULL) {
			GError *error;
			if (! ensure_dir_exists (local_uri, 0755, &error)) {
				g_free (local_folder_uri);
				g_free (local_uri);
				gio_file_list_free (sources);
				gio_file_list_free (destinations);
				g_hash_table_destroy (created_folders);

				fr_archive_action_completed (archive,
							     FR_ACTION_COPYING_FILES_FROM_REMOTE,
							     FR_PROC_ERROR_GENERIC,
							     error->message);
				g_clear_error (&error);
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
	xfer_data->archive = archive;
	xfer_data->file_list = path_list_dup (file_list);
	xfer_data->base_uri = g_strdup (base_uri);
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->update = update;
	xfer_data->dest_dir = g_strdup (dest_dir);
	xfer_data->password = g_strdup (password);
	xfer_data->compression = compression;
	xfer_data->tmp_dir = g_strdup (tmp_dir);

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_COPYING_FILES_FROM_REMOTE);
	
	g_copy_files_async (sources,
			    destinations,
			    G_FILE_COPY_OVERWRITE,
			    G_PRIORITY_DEFAULT,
			    archive->priv->cancellable,
			    copy_remote_files_progress,
			    xfer_data,
			    copy_remote_files_done,
			    xfer_data);
	
	gio_file_list_free (sources);
	gio_file_list_free (destinations);
}


static char *
fr_archive_get_temp_work_dir (FrArchive *archive)
{
	fr_archive_remove_temp_work_dir (archive);
	archive->priv->temp_dir = get_temp_work_dir ();
	return archive->priv->temp_dir;
}


/* Note: all paths unescaped. */
void
fr_archive_add_files (FrArchive     *archive,
		      GList         *file_list,
		      const char    *base_dir,
		      const char    *dest_dir,
		      gboolean       update,
		      const char    *password,
		      FRCompression  compression)
{
	if (uri_is_local (base_dir)) {
		char *local_dir = get_local_path_from_uri (base_dir);
		fr_archive_add_local_files (archive,
					    file_list, 
					    local_dir, 
					    dest_dir, 
					    update, 
					    password, 
					    compression);
		g_free (local_dir);
	}
	else
		copy_remote_files (archive,
				   file_list, 
				   base_dir, 
				   dest_dir, 
				   update, 
				   password, 
				   compression,
				   fr_archive_get_temp_work_dir (archive));
}


/* -- add with wildcard -- */


typedef struct {
	FrArchive     *archive;
	char          *source_dir;
	char          *dest_dir;
	gboolean       update;
	char          *password;
	FRCompression  compression;
} AddWithWildcardData;


static void
add_with_wildcard_data_free (AddWithWildcardData *aww_data)
{
	g_free (aww_data->source_dir);
	g_free (aww_data->dest_dir);
	g_free (aww_data->password);
	g_free (aww_data);
}


static void
add_with_wildcard__step2 (GList    *file_list,
			  GList    *dirs_list,
			  GError   *error,
			  gpointer  data)
{
	AddWithWildcardData *aww_data = data;
	FrArchive           *archive = aww_data->archive;

	if (error != NULL) {
		fr_archive_action_completed (archive,
					     FR_ACTION_GETTING_FILE_LIST, 
					     (g_error_matches (error, G_FILE_ERROR, G_IO_ERROR_CANCELLED) ? FR_PROC_ERROR_STOPPED : FR_PROC_ERROR_GENERIC),
					     error->message);
		return;
	}
	
	fr_archive_action_completed (archive,
				     FR_ACTION_GETTING_FILE_LIST, 
				     FR_PROC_ERROR_NONE,
				     NULL);

/* FIXME
	visit_dir_handle_free (archive->priv->vd_handle);
	archive->priv->vd_handle = NULL;
*/

	if (file_list != NULL) {
		fr_archive_add_files (aww_data->archive,
				      file_list,
				      aww_data->source_dir,
				      aww_data->dest_dir,
				      aww_data->update,
				      aww_data->password,
				      aww_data->compression);
		path_list_free (file_list);
	}

	add_with_wildcard_data_free (aww_data);
}


/* Note: all paths unescaped. */
void
fr_archive_add_with_wildcard (FrArchive     *archive,
			      const char    *include_files,
			      const char    *exclude_files,
			      const char    *source_dir,
			      const char    *dest_dir,
			      gboolean       update,
			      gboolean       recursive,
			      gboolean       follow_links,
			      const char    *password,
			      FRCompression  compression)
{
	AddWithWildcardData *aww_data;

	/* FIXME: g_return_if_fail (archive->priv->vd_handle == NULL); */
	g_return_if_fail (! archive->read_only);

	aww_data = g_new0 (AddWithWildcardData, 1);
	aww_data->archive = archive;
	aww_data->source_dir = g_strdup (source_dir);
	aww_data->dest_dir = g_strdup (dest_dir);
	aww_data->update = update;
	aww_data->password = g_strdup (password);
	aww_data->compression = compression;

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_GETTING_FILE_LIST);

/*
	archive->priv->vd_handle = get_wildcard_file_list_async (
					source_dir,
					include_files,
					recursive,
					follow_links,
					SAME_FS,
					NO_BACKUP_FILES,
					NO_DOT_FILES,
					IGNORE_CASE,
					add_with_wildcard__step2,
					aww_data);
*/
					
	g_directory_list_async (source_dir, 
				source_dir,
				recursive,
				follow_links,
				NO_BACKUP_FILES,
				NO_DOT_FILES,
				include_files,
				exclude_files,
				IGNORE_CASE,
				archive->priv->cancellable,
				add_with_wildcard__step2,
				aww_data);
}


/* -- fr_archive_add_directory -- */


typedef struct {
	FrArchive     *archive;
	char          *base_dir;
	char          *dest_dir;
	gboolean       update;
	char          *password;
	FRCompression  compression;
} AddDirectoryData;


static void
add_directory_data_free (AddDirectoryData *ad_data)
{
	g_free (ad_data->base_dir);
	g_free (ad_data->dest_dir);
	g_free (ad_data->password);
	g_free (ad_data);
}


static void
add_directory__step2 (GList    *file_list,
		      GList    *dir_list,
		      GError   *error,
		      gpointer  data)
{
	AddDirectoryData *ad_data = data;
	FrArchive        *archive = ad_data->archive;

/* FIXME:
	visit_dir_handle_free (archive->priv->vd_handle);
	archive->priv->vd_handle = NULL;
*/

	if (error != NULL) {
		fr_archive_action_completed (archive,
					     FR_ACTION_GETTING_FILE_LIST,
					     (g_error_matches (error, G_FILE_ERROR, G_IO_ERROR_CANCELLED) ? FR_PROC_ERROR_STOPPED : FR_PROC_ERROR_GENERIC),
					     error->message);
		return;
	}

	fr_archive_action_completed (archive,
				     FR_ACTION_GETTING_FILE_LIST, 
				     FR_PROC_ERROR_NONE,
				     NULL);

	if (archive->command->propAddCanStoreFolders) 
		file_list = g_list_concat (file_list, dir_list);
	else
		path_list_free (dir_list);
		
	if (file_list != NULL) {
		fr_archive_add_files (ad_data->archive,
				      file_list,
				      ad_data->base_dir,
				      ad_data->dest_dir,
				      ad_data->update,
				      ad_data->password,
				      ad_data->compression);
		path_list_free (file_list);
	}

	add_directory_data_free (ad_data);
}


/* Note: all paths unescaped. */
void
fr_archive_add_directory (FrArchive     *archive,
			  const char    *directory,
			  const char    *base_dir,
			  const char    *dest_dir,
			  gboolean       update,
			  const char    *password,
			  FRCompression  compression)

{
	AddDirectoryData *ad_data;

/* FIXME
  	g_return_if_fail (archive->priv->vd_handle == NULL); */
	g_return_if_fail (! archive->read_only);

	ad_data = g_new0 (AddDirectoryData, 1);
	ad_data->archive = archive;
	ad_data->base_dir = g_strdup (base_dir);
	ad_data->dest_dir = g_strdup (dest_dir);
	ad_data->update = update;
	ad_data->password = g_strdup (password);
	ad_data->compression = compression;

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_GETTING_FILE_LIST);

/* FIXME:
	archive->priv->vd_handle = get_items_file_list_async (
					ad_data->dir_list,
					base_dir,
					add_directory__step2,
					ad_data);
*/
			    
	g_directory_list_all_async (directory, 
				    base_dir,
				    TRUE,
				    archive->priv->cancellable,
				    add_directory__step2,
				    ad_data);
}


/* Note: all paths unescaped. */
void
fr_archive_add_items (FrArchive     *archive,
		      GList         *item_list,
		      const char    *base_dir,
		      const char    *dest_dir,
		      gboolean       update,
		      const char    *password,
		      FRCompression  compression)

{
	AddDirectoryData *ad_data;

/* FIXME
 	g_return_if_fail (archive->priv->vd_handle == NULL); */
	g_return_if_fail (! archive->read_only);

	ad_data = g_new0 (AddDirectoryData, 1);
	ad_data->archive = archive;
	ad_data->base_dir = g_strdup (base_dir);
	ad_data->dest_dir = g_strdup (dest_dir);
	ad_data->update = update;
	ad_data->password = g_strdup (password);
	ad_data->compression = compression;

	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[START],
		       0,
		       FR_ACTION_GETTING_FILE_LIST);

/* FIXME:
	archive->priv->vd_handle = get_items_file_list_async (
					ad_data->dir_list,
					base_dir,
					add_directory__step2,
					ad_data);
*/
				
	g_list_items_async (item_list,
			    base_dir,
			    archive->priv->cancellable,
			    add_directory__step2,
			    ad_data);
}


/* -- fr_archive_add_dropped_items -- */


static gboolean
all_files_in_same_dir (GList *list)
{
	gboolean  same_dir = TRUE;
	char     *first_basedir;
	GList    *scan;

	if (list == NULL)
		return FALSE;

	first_basedir = remove_level_from_path (list->data);
	if (first_basedir == NULL)
		return TRUE;

	for (scan = list->next; scan; scan = scan->next) {
		char *path = scan->data;
		char *basedir;

		basedir = remove_level_from_path (path);
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
	FrArchive *archive = data->archive;
	GList     *list = data->item_list;
	GList     *scan;

	if (list == NULL) {
		dropped_items_data_free (archive->priv->dropped_items_data);
		archive->priv->dropped_items_data = NULL;
		fr_archive_action_completed (archive,
					     FR_ACTION_ADDING_FILES,
					     FR_PROC_ERROR_NONE,
					     NULL);
		return;
	}

	/* if all files/dirs are in the same directory call fr_archive_add_items... */

	if (all_files_in_same_dir (list)) {
		char *first_base_dir;

		first_base_dir = remove_level_from_path (list->data);
		fr_archive_add_items (data->archive,
				      list,
				      first_base_dir,
				      NULL,
				      data->update,
				      data->password,
				      data->compression);
		g_free (first_base_dir);

		dropped_items_data_free (archive->priv->dropped_items_data);
		archive->priv->dropped_items_data = NULL;

		return;
	}

	/* ...else add a directory at a time. */

	for (scan = list; scan; scan = scan->next) {
		char *path = scan->data;
		char *base_dir;

		if (! uri_is_dir (path))
			continue;

		data->item_list = g_list_remove_link (list, scan);
		if (data->item_list != NULL)
			archive->priv->continue_adding_dropped_items = TRUE;
		base_dir = remove_level_from_path (path);

		fr_archive_add_directory (archive,
					  file_name_from_path (path),
					  base_dir,
					  NULL,
					  data->update,
					  data->password,
					  data->compression);

		g_free (base_dir);
		g_free (path);

		return;
	}

	/* if all files are in the same directory call fr_archive_add_files. */

	if (all_files_in_same_dir (list)) {
		char  *first_basedir;
		GList *only_names_list = NULL;

		first_basedir = remove_level_from_path (list->data);

		for (scan = list; scan; scan = scan->next)
			only_names_list = g_list_prepend (only_names_list, (gpointer) file_name_from_path (scan->data));

		fr_archive_add_files (archive,
				      only_names_list,
				      first_basedir,
				      data->base_dir,
				      data->update,
				      data->password,
				      data->compression);

		g_list_free (only_names_list);
		g_free (first_basedir);

		return;
	}

	/* ...else call fr_command_add for each file.  This is needed to add
	 * files without path info. FIXME: doesn't work with remote files. */

	fr_archive_stoppable (archive, FALSE);

	fr_process_clear (archive->process);
	fr_command_uncompress (archive->command);
	for (scan = list; scan; scan = scan->next) {
		char  *fullpath = scan->data;
		char  *basedir;
		GList *singleton;

		basedir = remove_level_from_path (fullpath);
		singleton = g_list_prepend (NULL, shell_escape (file_name_from_path (fullpath)));
		fr_command_add (archive->command,
				singleton,
				basedir,
				data->update,
				data->password,
				data->compression);
		path_list_free (singleton);
		g_free (basedir);
	}
	fr_command_recompress (archive->command, data->compression);
	fr_process_start (archive->process);

	path_list_free (data->item_list);
	data->item_list = NULL;
}


void
fr_archive_add_dropped_items (FrArchive     *archive,
			      GList         *item_list,
			      const char    *base_dir,
			      const char    *dest_dir,
			      gboolean       update,
			      const char    *password,
			      FRCompression  compression)
{
	GList *scan;
	char  *archive_uri;
	
	if (archive->read_only) {
		fr_archive_action_completed (archive,
					     FR_ACTION_ADDING_FILES, 
					     FR_PROC_ERROR_GENERIC, 
					     _("You don't have the right permissions."));
		return;
	}

	/* FIXME: make this check for all the add actions */
	archive_uri = g_file_get_uri (archive->file);
	for (scan = item_list; scan; scan = scan->next) {
		if (uricmp (scan->data, archive_uri) == 0) {
			g_free (archive_uri);
			fr_archive_action_completed (archive,
						     FR_ACTION_ADDING_FILES,
						     FR_PROC_ERROR_GENERIC,
						     _("You can't add an archive to itself."));
			return;
		}
	}
	g_free (archive_uri);

	if (archive->priv->dropped_items_data != NULL)
		dropped_items_data_free (archive->priv->dropped_items_data);
	archive->priv->dropped_items_data = dropped_items_data_new (
						archive,
				       		item_list,
				       		base_dir,
				       		dest_dir,
				       		update,
				       		password,
				       		compression);
	add_dropped_items (archive->priv->dropped_items_data);
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

		if (path_in_path (folder_in_list, filename))
			return TRUE;
	}

	return FALSE;
}


static gboolean
archive_type_has_issues_deleting_non_empty_folders (FrArchive *archive)
{
	/*if ((archive->command->files == NULL) || (archive->command->files->len == 0))
		return FALSE;  FIXME: test with extract_here */
		
	return ((archive->command->file_type == FR_FILE_TYPE_TAR)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_BZ)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_BZ2)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_GZ)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_LZMA)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_LZOP)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_COMPRESS));
}


/* Note: all paths unescaped. */
static void
archive_remove (FrArchive *archive,
		GList     *file_list)
{
	gboolean  file_list_created = FALSE;
	GList    *tmp_file_list = NULL;
	gboolean  tmp_file_list_created = FALSE;
	GList    *e_file_list;
	GList    *scan;
	
	/* file_list == NULL means delete all the files in the archive. */

	if (file_list == NULL) {
		int i;

		for (i = 0; i < archive->command->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->command->files, i);
			file_list = g_list_prepend (file_list, fdata->original_path);
		}

		file_list_created = TRUE;
	}

	if (archive_type_has_issues_deleting_non_empty_folders (archive)) {
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

	/* shell-escape the file list, and split in chunks to avoid
	 * command line overflow */

	e_file_list = escape_file_list (archive->command, tmp_file_list);
	g_list_free (tmp_file_list);

	fr_command_set_n_files (archive->command, g_list_length (e_file_list));
	for (scan = e_file_list; scan != NULL; ) {
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
		fr_command_delete (archive->command, chunk_list);
		prev->next = scan;
	}
	path_list_free (e_file_list);
}


/* Note: all paths unescaped. */
void
fr_archive_remove (FrArchive     *archive,
		   GList         *file_list,
		   FRCompression  compression)
{
	g_return_if_fail (archive != NULL);

	if (archive->read_only)
		return;

	fr_archive_stoppable (archive, FALSE);

	fr_command_uncompress (archive->command);
	archive_remove (archive, file_list);
	fr_command_recompress (archive->command, compression);
}


/* -- extract -- */


/* Note: all paths escaped, source_dir and dest_dir escaped. */
static void
move_files_to_dir (FrArchive  *archive,
		   GList      *file_list,
		   const char *source_dir,
		   const char *dest_dir)
{
	GList *scan;

	fr_process_begin_command (archive->process, "mv");
	fr_process_add_arg (archive->process, "-f");
	for (scan = file_list; scan; scan = scan->next) {
		char  path[4096]; /* FIXME : 4096 ? */
		char *e_filename = shell_escape (scan->data);

		if (e_filename[0] == '/')
			sprintf (path, "%s%s", source_dir, e_filename);
		else
			sprintf (path, "%s/%s", source_dir, e_filename);

		fr_process_add_arg (archive->process, path);

		g_free (e_filename);
	}
	fr_process_add_arg (archive->process, dest_dir);
	fr_process_end_command (archive->process);
}


/* Note: all paths unescaped, temp_dir and dest_dir unescaped. */
static void
move_files_in_chunks (FrArchive  *archive,
		      GList      *file_list,
		      const char *temp_dir,
		      const char *dest_dir)
{
	GList *scan;
	int    e_temp_dir_l;
	char  *e_temp_dir;
	char  *e_dest_dir;

	e_temp_dir = shell_escape (temp_dir);
	e_dest_dir = shell_escape (dest_dir);
	e_temp_dir_l = strlen (e_temp_dir);

	for (scan = file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;

		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = e_temp_dir_l + 1 + strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += e_temp_dir_l + 1 + strlen (scan->data);
		}

		prev->next = NULL;
		move_files_to_dir (archive,
				   chunk_list,
				   e_temp_dir,
				   e_dest_dir);
		prev->next = scan;
	}

	g_free (e_temp_dir);
	g_free (e_dest_dir);
}


/* Note: all paths escaped, dest_dir unescaped. */
static void
extract_in_chunks (FrCommand  *command,
		   GList      *file_list,
		   const char *dest_dir,
		   gboolean    overwrite,
		   gboolean    skip_older,
		   gboolean    junk_paths,
		   const char *password)
{
	GList *scan;

	fr_command_set_n_files (command, g_list_length (file_list));

	if (file_list == NULL) {
		fr_command_extract (command,
				    file_list,
				    dest_dir,
				    overwrite,
				    skip_older,
				    junk_paths,
				    password);
		return;
	}

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
		fr_command_extract (command,
				    chunk_list,
				    dest_dir,
				    overwrite,
				    skip_older,
				    junk_paths,
				    password);
		prev->next = scan;
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
			new_path = g_strdup (file_name_from_path (path));
		else
			new_path = g_strdup (path);
		debug (DEBUG_INFO, "%s, %s --> %s\n", base_dir, path, new_path);
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

	debug (DEBUG_INFO, "%s, %s --> %s\n", base_dir, path, new_path);

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
archive_type_has_issues_extracting_non_empty_folders (FrArchive *archive)
{
	/*if ((archive->command->files == NULL) || (archive->command->files->len == 0))
		return FALSE;  FIXME: test with extract_here */
		
	return ((archive->command->file_type == FR_FILE_TYPE_TAR)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_BZ)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_BZ2)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_GZ)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_LZMA)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_LZOP)
		|| (archive->command->file_type == FR_FILE_TYPE_TAR_COMPRESS));
}


static gboolean
file_list_contains_files_in_this_dir (GList      *file_list,
				      const char *dirname)
{
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;

		if (path_in_path (dirname, filename))
			return TRUE;
	}

	return FALSE;
}


static GList*
remove_files_contained_in_this_dir (GList      *file_list,
				    const char *dirname, 
				    gboolean   *changed)
{
	GList *scan;

	*changed = FALSE;
	
	for (scan = file_list; scan; /* empty */) {
		char *filename = scan->data;

		if (path_in_path (dirname, filename)) {
			GList *next = scan->next;
			
			file_list = g_list_remove_link (file_list, scan);
			g_list_free (scan);
			*changed = TRUE;
			
			scan = next;
		}
		else
			scan = scan->next;
	}
	
	return file_list;
}


/* Note : All paths unescaped.
 * Note2: Do not escape dest_dir it will escaped in fr_command_extract if
 *        needed. */
void
fr_archive_extract_to_local (FrArchive  *archive,
			     GList      *file_list,
			     const char *destination,
			     const char *base_dir,
			     gboolean    skip_older,
			     gboolean    overwrite,
			     gboolean    junk_paths,
			     const char *password)
{
	char     *dest_dir;
	GList    *filtered, *e_filtered;
	GList    *scan;
	gboolean  extract_all;
	gboolean  use_base_dir;
	gboolean  move_to_dest_dir;
	gboolean  file_list_created = FALSE;

	g_return_if_fail (archive != NULL);

	dest_dir = get_local_path_from_uri (destination);

	fr_archive_stoppable (archive, TRUE);

	/* if a command supports all the requested options use
	 * fr_command_extract directly. */

	extract_all = (file_list == NULL);
	if (extract_all && ! archive->command->propCanExtractAll) {
		int i;
		
		file_list = NULL;
		for (i = 0; i < archive->command->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->command->files, i);
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}
		file_list_created = TRUE;
	}

	use_base_dir = ! ((base_dir == NULL)
			  || (strcmp (base_dir, "") == 0)
			  || (strcmp (base_dir, "/") == 0));

	if (! use_base_dir
	    && ! (! overwrite && ! archive->command->propExtractCanAvoidOverwrite)
	    && ! (skip_older && ! archive->command->propExtractCanSkipOlder)
	    && ! (junk_paths && ! archive->command->propExtractCanJunkPaths)) 
	{
		gboolean created_filtered_list = FALSE;

		if (! extract_all && archive_type_has_issues_extracting_non_empty_folders (archive)) {
			created_filtered_list = TRUE;
			filtered = g_list_copy (file_list);
			for (scan = filtered; scan; /* empty */) {  
				gboolean changed = FALSE;
				
				filtered = remove_files_contained_in_this_dir (filtered, scan->data, &changed);
				if (changed)
					scan = filtered;
				else
					scan = scan->next;
			}
		}
		else
			filtered = file_list;

		if (! (created_filtered_list && (filtered == NULL))) {
			e_filtered = escape_file_list (archive->command, filtered);
			extract_in_chunks (archive->command,
					   e_filtered,
					   dest_dir,
					   overwrite,
					   skip_older,
					   junk_paths,
					   password);
			path_list_free (e_filtered);
		}

		if (created_filtered_list && (filtered != NULL))
			g_list_free (filtered);

		if (file_list_created)
			path_list_free (file_list);

		return;
	}

	/* .. else we have to implement the unsupported options. */

	move_to_dest_dir = (use_base_dir
			    || ((junk_paths
				 && ! archive->command->propExtractCanJunkPaths)));

	if (extract_all && ! file_list_created) {
		int i;

		file_list = NULL;
		for (i = 0; i < archive->command->files->len; i++) {
			FileData *fdata = g_ptr_array_index (archive->command->files, i);
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

		if (archive_type_has_issues_extracting_non_empty_folders (archive)
		    && fdata->dir
		    && file_list_contains_files_in_this_dir (file_list, archive_list_filename))
			continue;

		/* get the destination file path. */

		if (! junk_paths)
			filename = archive_list_filename;
		else
			filename = file_name_from_path (archive_list_filename);

		if ((dest_dir[strlen (dest_dir) - 1] == '/')
		    || (filename[0] == '/'))
			sprintf (dest_filename, "%s%s", dest_dir, filename);
		else
			sprintf (dest_filename, "%s/%s", dest_dir, filename);

		debug (DEBUG_INFO, "-> %s\n", dest_filename);

		/**/

		if (! archive->command->propExtractCanSkipOlder
		    && skip_older
		    && g_file_test (dest_filename, G_FILE_TEST_EXISTS)
		    && (fdata->modified < get_file_mtime (dest_filename)))
			continue;

		if (! archive->command->propExtractCanAvoidOverwrite
		    && ! overwrite
		    && g_file_test (dest_filename, G_FILE_TEST_EXISTS))
			continue;

		filtered = g_list_prepend (filtered, fdata->original_path);
	}

	if (filtered == NULL) {
		/* all files got filtered, do nothing. */
		debug (DEBUG_INFO, "All files got filtered, nothing to do.\n");

		g_free (dest_dir);
		if (extract_all)
			path_list_free (file_list);
		return;
	}

	e_filtered = escape_file_list (archive->command, filtered);

	if (move_to_dest_dir) {
		char *temp_dir;
		char *e_temp_dir;

		temp_dir = get_temp_work_dir ();
		extract_in_chunks (archive->command,
				   e_filtered,
				   temp_dir,
				   overwrite,
				   skip_older,
				   junk_paths,
				   password);

		if (use_base_dir) {
			GList *tmp_list = compute_list_base_path (base_dir, filtered, junk_paths, archive->command->propExtractCanJunkPaths);
			g_list_free (filtered);
			filtered = tmp_list;
		}

		move_files_in_chunks (archive,
				      filtered,
				      temp_dir,
				      dest_dir);

		/* remove the temp dir. */

		e_temp_dir = shell_escape (temp_dir);
		fr_process_begin_command (archive->process, "rm");
		fr_process_add_arg (archive->process, "-rf");
		fr_process_add_arg (archive->process, e_temp_dir);
		fr_process_end_command (archive->process);
		g_free (e_temp_dir);

		g_free (temp_dir);
	}
	else
		extract_in_chunks (archive->command,
				   e_filtered,
				   dest_dir,
				   overwrite,
				   skip_older,
				   junk_paths,
				   password);

	path_list_free (e_filtered);
	if (filtered != NULL)
		g_list_free (filtered);
	if (file_list_created)
		path_list_free (file_list);
	g_free (dest_dir);
}


void
fr_archive_extract (FrArchive  *archive,
		    GList      *file_list,
		    const char *destination,
		    const char *base_dir,
		    gboolean    skip_older,
		    gboolean    overwrite,
		    gboolean    junk_paths,
		    const char *password)
{
	g_free (archive->priv->extraction_destination);
	archive->priv->extraction_destination = g_strdup (destination);
	
	g_free (archive->priv->temp_extraction_dir);
	archive->priv->temp_extraction_dir = NULL;

	archive->priv->remote_extraction = ! uri_is_local (destination);

	if (archive->priv->remote_extraction) {
 		archive->priv->temp_extraction_dir = get_temp_work_dir ();
		fr_archive_extract_to_local (archive,
				  	     file_list,
				  	     archive->priv->temp_extraction_dir,
				  	     base_dir,
				  	     skip_older,
				  	     overwrite,
				  	     junk_paths,
				  	     password);
	}
	else 
		fr_archive_extract_to_local (archive,
					     file_list,
					     destination,
					     base_dir,
					     skip_older,
					     overwrite,
					     junk_paths,
					     password);
}


static char *
get_desired_destination_for_archive (GFile *file)
{
	GFile      *directory;
	char       *directory_uri;
	char       *name;
	const char *ext;
	char       *new_name;
	char       *desired_destination = NULL;
	
	directory = g_file_get_parent (file);
	directory_uri = g_file_get_uri (directory);
	
	name = g_file_get_basename (file);
	ext = fr_archive_utils__get_file_name_ext (name);
	if (ext == NULL)
		/* if no extension is present add a suffix to the name... */
		new_name = g_strconcat (name, "_FILES", NULL);
	else 
		/* ...else use the name without the extension */
		new_name = g_strndup (name, strlen (name) - strlen (ext)); 
	
	desired_destination = g_strconcat (directory_uri, "/", new_name, NULL);
	
	g_free (new_name);
	g_free (name);
	g_free (directory_uri);
	g_object_unref (directory);
	
	return desired_destination;
}


static char *
get_extract_here_destination (GFile   *file,
			      GError **error)
{
	char  *desired_destination;
	char  *destination = NULL;
	int    n = 1;
	GFile *directory;
	
	desired_destination = get_desired_destination_for_archive (file);
	do {
		g_free (destination);
		if (n == 1)
			destination = g_strdup (desired_destination);
		else
			destination = g_strdup_printf ("%s%%20(%d)", desired_destination, n);
			
		directory = g_file_new_for_uri (destination);
		g_file_make_directory (directory, NULL, error);
		g_object_unref (directory);
		
		n++;
	} while (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	
	g_free (desired_destination);
	
	if (*error != NULL) {
		g_warning ("could not create destination folder: %s\n", (*error)->message);
		g_free (destination);
		destination = NULL;
	}
	
	return destination;
}


gboolean
fr_archive_extract_here (FrArchive  *archive,
			 gboolean    skip_older,
			 gboolean    overwrite,
			 gboolean    junk_path,
			 const char *password)
{
	char   *destination;
	GError *error;
	
	destination = get_extract_here_destination (archive->file, &error);
	if (error != NULL) {
		fr_archive_action_completed (archive,
					     FR_ACTION_EXTRACTING_FILES,
					     FR_PROC_ERROR_GENERIC,
					     error->message);
		g_clear_error (&error);
		return FALSE;
	}
	
	archive->priv->extract_here = TRUE;
	fr_archive_extract (archive,
			    NULL,
			    destination,
			    NULL,
			    skip_older,
			    overwrite,
			    junk_path,
			    password);

	g_free (destination);
	
	return TRUE;			    
}			 


const char *
fr_archive_get_last_extraction_destination (FrArchive *archive)
{
	return archive->priv->extraction_destination;
}


void
fr_archive_test (FrArchive  *archive,
		 const char *password)
{
	fr_archive_stoppable (archive, TRUE);

	fr_process_clear (archive->process);
	fr_command_set_n_files (archive->command, 0);
	fr_command_test (archive->command, password);
	fr_process_start (archive->process);
}


/*
 * Remember to keep the ext array in alphanumeric order and to scan the array
 * in reverse order, this is because the file 'foo.tar.gz' must return the
 * '.tar.gz' and not the '.gz' extension.
 */
G_CONST_RETURN char *
fr_archive_utils__get_file_name_ext (const char *filename)
{
	static char * ext[] = {
		".7z",
		".ace",
		".ar",
		".arj",
		".bin",
		".bz",
		".bz2",
		".cpio",
		".deb",
		".ear",
		".gz",
		".iso",
		".jar",
		".lzh",
		".lzma",
		".lzo",
		".rar",
		".rpm",
		".sit",
		".tar",
		".tar.bz",
		".tar.bz2",
		".tar.gz",
		".tar.lzma",
		".tar.lzo",
		".tar.Z",
		".taz",
		".tbz",
		".tbz2",
		".tgz",
		".tzo",
		".war",
		".z",
		".zip",
		".zoo",
		".Z"
	};
	int n = sizeof (ext) / sizeof (char*);
	int i;

	for (i = n - 1; i >= 0; i--)
		if (file_extension_is (filename, ext[i]))
			return ext[i];

	return NULL;
}


gboolean
fr_archive_utils__file_is_archive (const char *filename)
{
	const char *mime_type;

	mime_type = get_mime_type_from_content (filename);
	if (mime_type == NULL)
		return FALSE;

	mime_type = get_mime_type_from_sniffer (filename);
	if (mime_type != NULL)
		return TRUE;

	return fr_archive_utils__get_file_name_ext (filename) != NULL;
}
