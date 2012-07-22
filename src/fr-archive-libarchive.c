/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <archive.h>
#include <archive_entry.h>
#include "file-data.h"
#include "fr-archive-libarchive.h"
#include "glib-utils.h"


G_DEFINE_TYPE (FrArchiveLibarchive, fr_archive_libarchive, FR_TYPE_ARCHIVE)


struct _FrArchiveLibarchivePrivate {
	int dummy;
};


static void
fr_archive_libarchive_finalize (GObject *object)
{
	/*FrArchiveLibarchive *self;*/

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_ARCHIVE_LIBARCHIVE (object));

	/*self = FR_ARCHIVE_LIBARCHIVE (object);*/

	if (G_OBJECT_CLASS (fr_archive_libarchive_parent_class)->finalize)
		G_OBJECT_CLASS (fr_archive_libarchive_parent_class)->finalize (object);
}


const char *libarchiver_mime_types[] = {
		"application/x-compressed-tar",
		"application/x-bzip-compressed-tar",
		"application/x-tar",
		"application/x-7z-compressed-tar",
		"application/x-lrzip-compressed-tar",
		"application/x-lzip-compressed-tar",
		"application/x-lzma-compressed-tar",
		"application/x-tarz",
		"application/x-xz-compressed-tar",
		NULL };


static const char **
fr_archive_libarchive_get_mime_types (FrArchive *archive)
{
	return libarchiver_mime_types;
}


static FrArchiveCap
fr_archive_libarchive_get_capabilities (FrArchive  *archive,
					const char *mime_type,
					gboolean    check_command)
{
	return FR_ARCHIVE_CAN_STORE_MANY_FILES | FR_ARCHIVE_CAN_READ;
}


static const char *
fr_archive_libarchive_get_packages (FrArchive  *archive,
				    const char *mime_type)
{
	return NULL;
}


/* -- load -- */


#define BUFFER_SIZE_FOR_READING (10 * 1024)


typedef struct {
	FrArchive          *archive;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
	GInputStream       *istream;
	void               *buffer;
	gssize              buffer_size;
	GError             *error;
} LoadData;


static void
load_data_free (LoadData *add_data)
{
	_g_object_unref (add_data->archive);
	_g_object_unref (add_data->cancellable);
	_g_object_unref (add_data->result);
	_g_object_unref (add_data->istream);
	g_free (add_data->buffer);
	g_free (add_data);
}


static int
load_data_open (struct archive *a,
		void           *client_data)
{
	LoadData *load_data = client_data;

	load_data->istream = (GInputStream *) g_file_read (fr_archive_get_file (load_data->archive),
							   load_data->cancellable,
							   &load_data->error);
	return (load_data->error == NULL) ? ARCHIVE_OK : ARCHIVE_FATAL;
}


static ssize_t
load_data_read (struct archive  *a,
		void            *client_data,
		const void     **buff)
{
	LoadData *load_data = client_data;

	*buff = load_data->buffer;
	return g_input_stream_read (load_data->istream,
				    load_data->buffer,
				    load_data->buffer_size,
				    load_data->cancellable,
				    &load_data->error);
}


static int
load_data_close (struct archive *a,
		 void           *client_data)
{
	LoadData *load_data = client_data;

	if (load_data->istream != NULL) {
		_g_object_unref (load_data->istream);
		load_data->istream = NULL;
	}

	return ARCHIVE_OK;
}


static void
load_archive_thread (GSimpleAsyncResult *result,
		     GObject            *object,
		     GCancellable       *cancellable)
{
	LoadData             *load_data;
	struct archive       *a;
	struct archive_entry *entry;

	load_data = g_simple_async_result_get_op_res_gpointer (result);

	a = archive_read_new ();
	archive_read_support_filter_all (a);
	archive_read_support_format_all (a);
	archive_read_open (a, load_data, load_data_open, load_data_read, load_data_close);
	while (archive_read_next_header (a, &entry) == ARCHIVE_OK) {
		FileData   *file_data;
		const char *pathname;

		if (g_cancellable_is_cancelled (cancellable))
			break;

		file_data = file_data_new ();

		if (archive_entry_size_is_set (entry))
			file_data->size =  archive_entry_size (entry);

		if (archive_entry_mtime_is_set (entry))
			file_data->modified =  archive_entry_mtime (entry);

		if (archive_entry_filetype (entry) == AE_IFLNK)
			file_data->link = g_strdup (archive_entry_symlink (entry));

		pathname = archive_entry_pathname (entry);
		if (*pathname == '/') {
			file_data->full_path = g_strdup (pathname);
			file_data->original_path = file_data->full_path;
		}
		else {
			file_data->full_path = g_strconcat ("/", pathname, NULL);
			file_data->original_path = file_data->full_path + 1;
		}

		file_data->dir = (archive_entry_filetype (entry) == AE_IFDIR);
		if (file_data->dir)
			file_data->name = _g_path_get_dir_name (file_data->full_path);
		else
			file_data->name = g_strdup (_g_path_get_file_name (file_data->full_path));
		file_data->path = _g_path_remove_level (file_data->full_path);

		/*
		g_print ("%s\n", archive_entry_pathname (entry));
		g_print ("\tfull_path: %s\n", file_data->full_path);
		g_print ("\toriginal_path: %s\n", file_data->original_path);
		g_print ("\tname: %s\n", file_data->name);
		g_print ("\tpath: %s\n", file_data->path);
		*/

		fr_archive_add_file (load_data->archive, file_data);

		archive_read_data_skip (a);
	}
	archive_read_free (a);

	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);

	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);
	g_simple_async_result_complete_in_idle (result);

	load_data_free (load_data);
}


static void
fr_archive_libarchive_load (FrArchive           *archive,
			    const char          *password,
			    GCancellable        *cancellable,
			    GAsyncReadyCallback  callback,
			    gpointer             user_data)
{
	LoadData *load_data;

	load_data = g_new0 (LoadData, 1);
	load_data->archive = g_object_ref (archive);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_load);
	load_data->buffer_size = BUFFER_SIZE_FOR_READING;
	load_data->buffer = g_new (char, load_data->buffer_size);

	g_simple_async_result_set_op_res_gpointer (load_data->result, load_data, NULL);
	g_simple_async_result_run_in_thread (load_data->result,
					     load_archive_thread,
					     G_PRIORITY_DEFAULT,
					     cancellable);
}


/* -- add -- */


static void
fr_archive_libarchive_add_files (FrArchive           *base,
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
}


/* -- remove -- */


static void
fr_archive_libarchive_remove_files (FrArchive           *archive,
				    GList               *file_list,
				    FrCompression        compression,
				    GCancellable        *cancellable,
				    GAsyncReadyCallback  callback,
				    gpointer             user_data)
{
}


/* -- extract -- */


static void
fr_archive_libarchive_extract_files (FrArchive           *base,
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
}


/* -- fr_archive_libarchive_rename -- */


static void
fr_archive_libarchive_rename (FrArchive           *archive,
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
}


/* -- fr_archive_libarchive_paste_clipboard -- */


static void
fr_archive_libarchive_paste_clipboard (FrArchive           *archive,
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
}


/* -- fr_archive_libarchive_add_dropped_items -- */


static void
fr_archive_libarchive_add_dropped_items (FrArchive           *archive,
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
}


/* -- fr_archive_libarchive_update_open_files -- */


static void
fr_archive_libarchive_update_open_files (FrArchive           *archive,
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
}


static void
fr_archive_libarchive_class_init (FrArchiveLibarchiveClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;

	fr_archive_libarchive_parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (FrArchiveLibarchivePrivate));

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_archive_libarchive_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types = fr_archive_libarchive_get_mime_types;
	archive_class->get_capabilities = fr_archive_libarchive_get_capabilities;
	archive_class->get_packages = fr_archive_libarchive_get_packages;
	archive_class->load = fr_archive_libarchive_load;
	archive_class->add_files = fr_archive_libarchive_add_files;
	archive_class->remove_files = fr_archive_libarchive_remove_files;
	archive_class->extract_files = fr_archive_libarchive_extract_files;
	archive_class->rename = fr_archive_libarchive_rename;
	archive_class->paste_clipboard = fr_archive_libarchive_paste_clipboard;
	archive_class->add_dropped_items = fr_archive_libarchive_add_dropped_items;
	archive_class->update_open_files = fr_archive_libarchive_update_open_files;
}


static void
fr_archive_libarchive_init (FrArchiveLibarchive *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_ARCHIVE_LIBARCHIVE, FrArchiveLibarchivePrivate);
}
