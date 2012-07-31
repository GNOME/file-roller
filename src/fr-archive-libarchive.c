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
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <archive.h>
#include <archive_entry.h>
#include "file-data.h"
#include "fr-error.h"
#include "fr-archive-libarchive.h"
#include "glib-utils.h"
#include "typedefs.h"


#define BUFFER_SIZE (10 * 1024)
#define FILE_ATTRIBUTES_NEEDED_BY_ARCHIVE_ENTRY ("standard::*,time::*,access::*,unix::*")


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
	return FR_ARCHIVE_CAN_STORE_MANY_FILES | FR_ARCHIVE_CAN_READ | FR_ARCHIVE_CAN_WRITE;
}


static const char *
fr_archive_libarchive_get_packages (FrArchive  *archive,
				    const char *mime_type)
{
	return NULL;
}


/* -- load -- */


#define LOAD_DATA(x) ((LoadData *)(x))


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
load_data_init (LoadData *load_data)
{
	load_data->buffer_size = BUFFER_SIZE;
	load_data->buffer = g_new (char, load_data->buffer_size);

}


static void
load_data_free (LoadData *load_data)
{
	_g_object_unref (load_data->archive);
	_g_object_unref (load_data->cancellable);
	_g_object_unref (load_data->result);
	_g_object_unref (load_data->istream);
	g_free (load_data->buffer);
	g_free (load_data);
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
	int                   r;

	load_data = g_simple_async_result_get_op_res_gpointer (result);

	a = archive_read_new ();
	archive_read_support_filter_all (a);
	archive_read_support_format_all (a);
	archive_read_open (a, load_data, load_data_open, load_data_read, load_data_close);
	while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
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

	if ((load_data->error == NULL) && (r != ARCHIVE_EOF))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (a));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);

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
	load_data_init (load_data);

	load_data->archive = g_object_ref (archive);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_load);

	g_simple_async_result_set_op_res_gpointer (load_data->result, load_data, NULL);
	g_simple_async_result_run_in_thread (load_data->result,
					     load_archive_thread,
					     G_PRIORITY_DEFAULT,
					     cancellable);
}


/* -- extract -- */


typedef struct {
	LoadData    parent;
	GList      *file_list;
	GFile      *destination;
	char       *base_dir;
	gboolean    skip_older;
	gboolean    overwrite;
	gboolean    junk_paths;
	GHashTable *files_to_extract;
	int         n_files_to_extract;
} ExtractData;


static void
extract_data_free (ExtractData *extract_data)
{
	g_free (extract_data->base_dir);
	_g_object_unref (extract_data->destination);
	_g_string_list_free (extract_data->file_list);
	g_hash_table_unref (extract_data->files_to_extract);
	load_data_free (LOAD_DATA (extract_data));
}


static gboolean
extract_data_get_extraction_requested (ExtractData *extract_data,
				       const char  *pathname)
{
	if (extract_data->file_list != NULL)
		return g_hash_table_lookup (extract_data->files_to_extract, pathname) != NULL;
	else
		return TRUE;
}


static void
extract_archive_thread (GSimpleAsyncResult *result,
			GObject            *object,
			GCancellable       *cancellable)
{
	ExtractData          *extract_data;
	LoadData             *load_data;
	GHashTable           *checked_folders;
	struct archive       *a;
	struct archive_entry *entry;
	int                   r;

	extract_data = g_simple_async_result_get_op_res_gpointer (result);
	load_data = LOAD_DATA (extract_data);

	checked_folders = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);
	fr_archive_progress_set_total_files (load_data->archive, extract_data->n_files_to_extract);

	a = archive_read_new ();
	archive_read_support_filter_all (a);
	archive_read_support_format_all (a);
	archive_read_open (a, load_data, load_data_open, load_data_read, load_data_close);
	while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
		const char    *pathname;
		char          *fullpath;
		GFile         *file;
		GFile         *parent;
		GOutputStream *ostream;
		const void    *buffer;
		size_t         buffer_size;
		int64_t        offset;
		GError        *local_error = NULL;

		if (g_cancellable_is_cancelled (cancellable))
			break;

		pathname = archive_entry_pathname (entry);
		if (! extract_data_get_extraction_requested (extract_data, pathname)) {
			archive_read_data_skip (a);
			continue;
		}

		fullpath = (*pathname == '/') ? g_strdup (pathname) : g_strconcat ("/", pathname, NULL);
		file = g_file_get_child (extract_data->destination, _g_path_get_basename (fullpath, extract_data->base_dir, extract_data->junk_paths));

		/* honor the skip_older and overwrite options */

		if (extract_data->skip_older || ! extract_data->overwrite) {
			GFileInfo *info;

			info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, cancellable, &local_error);
			if (info != NULL) {
				gboolean skip = FALSE;

				if (! extract_data->overwrite) {
					skip = TRUE;
				}
				else if (extract_data->skip_older) {
					GTimeVal modification_time;

					g_file_info_get_modification_time (info, &modification_time);
					if (archive_entry_mtime (entry) < modification_time.tv_sec)
						skip = TRUE;
				}

				g_object_unref (info);

				if (skip) {
					g_object_unref (file);

					archive_read_data_skip (a);
					fr_archive_progress_inc_completed_bytes (load_data->archive, archive_entry_size_is_set (entry) ? archive_entry_size (entry) : 0);

					if ((extract_data->file_list != NULL) && (--extract_data->n_files_to_extract == 0)) {
						r = ARCHIVE_EOF;
						break;
					}

					continue;
				}
			}
			else {
				if (! g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
					load_data->error = local_error;
					g_object_unref (info);
					break;
				}
				g_error_free (local_error);
			}
		}

		fr_archive_progress_inc_completed_files (load_data->archive, 1);

		/* create the file parents */

		parent = g_file_get_parent (file);

		if ((parent != NULL)
		    && (g_hash_table_lookup (checked_folders, parent) == NULL)
		    && ! g_file_query_exists (parent, cancellable))
		{
			if (g_file_make_directory_with_parents (parent, cancellable, &load_data->error)) {
				GFile *grandparent;

				grandparent = g_object_ref (parent);
				while (grandparent != NULL) {
					if (g_hash_table_lookup (checked_folders, grandparent) == NULL)
						g_hash_table_insert (checked_folders, grandparent, GINT_TO_POINTER (1));
					grandparent = g_file_get_parent (grandparent);
				}
			}
		}
		g_object_unref (parent);

		/* create the file */

		if (load_data->error == NULL) {
			switch (archive_entry_filetype (entry)) {
			case AE_IFDIR:
				if (! g_file_make_directory (file, cancellable, &local_error)) {
					if (! g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
						load_data->error = g_error_copy (local_error);
					g_error_free (local_error);
				}
				archive_read_data_skip (a);
				break;

			case AE_IFREG:
				ostream = (GOutputStream *) g_file_replace (file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, cancellable, &load_data->error);
				if (ostream == NULL)
					break;

				while ((r = archive_read_data_block (a, &buffer, &buffer_size, &offset)) == ARCHIVE_OK) {
					if (g_output_stream_write (ostream, buffer, buffer_size, cancellable, &load_data->error) == -1)
						break;
					fr_archive_progress_inc_completed_bytes (load_data->archive, buffer_size);
				}

				if (r != ARCHIVE_EOF)
					load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (a));

				_g_object_unref (ostream);
				break;

			case AE_IFLNK:
				if (! g_file_make_symbolic_link (file, archive_entry_symlink (entry), cancellable, &local_error)) {
					if (! g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
						load_data->error = g_error_copy (local_error);
					g_error_free (local_error);
				}
				archive_read_data_skip (a);
				break;
			}
		}

		g_object_unref (file);
		g_free (fullpath);

		if (load_data->error != NULL)
			break;

		if ((extract_data->file_list != NULL) && (--extract_data->n_files_to_extract == 0)) {
			r = ARCHIVE_EOF;
			break;
		}
	}

	if ((load_data->error == NULL) && (r != ARCHIVE_EOF))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (a));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);

	g_hash_table_unref (checked_folders);
	archive_read_free (a);
	extract_data_free (extract_data);
}


static void
fr_archive_libarchive_extract_files (FrArchive           *archive,
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
	ExtractData *extract_data;
	LoadData    *load_data;
	GList       *scan;

	extract_data = g_new0 (ExtractData, 1);

	load_data = LOAD_DATA (extract_data);
	load_data->archive = g_object_ref (archive);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = g_simple_async_result_new (G_OBJECT (archive),
						       callback,
						       user_data,
						       fr_archive_load);
	load_data->buffer_size = BUFFER_SIZE;
	load_data->buffer = g_new (char, load_data->buffer_size);

	extract_data->file_list = _g_string_list_dup (file_list);
	extract_data->destination = g_file_new_for_uri (destination);
	extract_data->base_dir = g_strdup (base_dir);
	extract_data->skip_older = skip_older;
	extract_data->overwrite = overwrite;
	extract_data->junk_paths = junk_paths;
	extract_data->files_to_extract = g_hash_table_new (g_str_hash, g_str_equal);
	extract_data->n_files_to_extract = 0;
	for (scan = extract_data->file_list; scan; scan = scan->next) {
		g_hash_table_insert (extract_data->files_to_extract, scan->data, GINT_TO_POINTER (1));
		extract_data->n_files_to_extract++;
	}

	g_simple_async_result_set_op_res_gpointer (load_data->result, extract_data, NULL);
	g_simple_async_result_run_in_thread (load_data->result,
					     extract_archive_thread,
					     G_PRIORITY_DEFAULT,
					     cancellable);
}


/* --  AddFile -- */


typedef struct {
	GFile *file;
	char  *pathname;
} AddFile;


static AddFile *
add_file_new (GFile      *file,
	      const char *archive_pathname)
{
	AddFile *add_file;

	add_file = g_new (AddFile, 1);
	add_file->file = g_object_ref (file);
	add_file->pathname = g_strdup (archive_pathname);

	return add_file;
}


static void
add_file_free (AddFile *add_file)
{
	g_object_unref (add_file->file);
	g_free (add_file->pathname);
	g_free (add_file);
}


/* -- _fr_archive_libarchive_save -- */


#define SAVE_DATA(x) ((SaveData *)(x))

typedef enum {
	WRITE_ACTION_ABORT,
	WRITE_ACTION_SKIP_ENTRY,
	WRITE_ACTION_WRITE_ENTRY
} WriteAction;

typedef struct      _SaveData SaveData;
typedef void        (*SaveDataFunc)    (SaveData *, gpointer user_data);
typedef WriteAction (*EntryActionFunc) (SaveData *, struct archive_entry *, gpointer user_data);


struct _SaveData {
	LoadData         parent;
	GFile           *tmp_file;
	GOutputStream   *ostream;
	GHashTable      *usernames;
	GHashTable      *groupnames;
	gboolean         update;
	char            *password;
	gboolean         encrypt_header;
	FrCompression    compression;
	guint            volume_size;
	void            *buffer;
	gsize            buffer_size;
	SaveDataFunc     begin_operation;
	SaveDataFunc     end_operation;
	EntryActionFunc  entry_action;
	gpointer         user_data;
	GDestroyNotify   user_data_notify;
	struct archive  *b;
};


static void
save_data_init (SaveData *save_data)
{
	load_data_init (LOAD_DATA (save_data));
	save_data->buffer_size = BUFFER_SIZE;
	save_data->buffer = g_new (char, save_data->buffer_size);
	save_data->usernames = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
	save_data->groupnames = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
}


static void
save_data_free (SaveData *save_data)
{
	if (save_data->user_data_notify != NULL)
		save_data->user_data_notify (save_data->user_data);
	g_free (save_data->buffer);
	g_free (save_data->password);
	g_hash_table_unref (save_data->groupnames);
	g_hash_table_unref (save_data->usernames);
	_g_object_unref (save_data->ostream);
	_g_object_unref (save_data->tmp_file);
	load_data_free (LOAD_DATA (save_data));
}


static int
save_data_open (struct archive *a,
	        void           *client_data)
{
	SaveData *save_data = client_data;
	LoadData *load_data = LOAD_DATA (save_data);
	GFile    *parent;
	char     *name;

	/* FIXME: use a better temp filename */
	parent = g_file_get_parent (fr_archive_get_file (load_data->archive));
	name = _g_filename_get_random (16);
	save_data->tmp_file = g_file_get_child (parent, name);
	save_data->ostream = (GOutputStream *) g_file_create (save_data->tmp_file, G_FILE_CREATE_NONE, load_data->cancellable, &load_data->error);

	g_free (name);
	g_object_unref (parent);

	return (save_data->ostream != NULL) ? ARCHIVE_OK : ARCHIVE_FATAL;
}


static ssize_t
save_data_write (struct archive *a,
		 void           *client_data,
		 const void     *buff,
		 size_t          n)
{
	SaveData *save_data = client_data;
	LoadData *load_data = LOAD_DATA (save_data);

	return g_output_stream_write (save_data->ostream, buff, n, load_data->cancellable, &load_data->error);
}


static int
save_data_close (struct archive *a,
		 void           *client_data)
{
	SaveData *save_data = client_data;
	LoadData *load_data = LOAD_DATA (save_data);

	if (save_data->ostream != NULL)
		g_output_stream_close (save_data->ostream, load_data->cancellable, &load_data->error);

	if (load_data->error == NULL)
		g_file_move (save_data->tmp_file,
			     fr_archive_get_file (load_data->archive),
			     G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
			     load_data->cancellable,
			     NULL,
			     NULL,
			     &load_data->error);
	else
		g_file_delete (save_data->tmp_file, NULL, NULL);

	return 0;
}


static void
_archive_write_set_format_from_context (struct archive *a,
					SaveData       *save_data)
{
	char       *uri;
	const char *ext;
	int         archive_filter;

	/* set format and filter from the file extension */

	uri = g_file_get_uri (fr_archive_get_file (LOAD_DATA (save_data)->archive));
	ext = _g_filename_get_extension (uri);
	if (ext == NULL) {
		/* defaults to an uncompressed tar */
		ext = ".tar";
	}
	archive_filter = ARCHIVE_FILTER_NONE;

	if ((strcmp (ext, ".tar.bz2") == 0) || (strcmp (ext, ".tbz2") == 0)) {
		archive_write_add_filter_bzip2 (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_BZIP2;
	}
	else if ((strcmp (ext, ".tar.Z") == 0) || (strcmp (ext, ".taz") == 0)) {
		archive_write_add_filter_compress (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_COMPRESS;
	}
	else if ((strcmp (ext, ".tar.gz") == 0) || (strcmp (ext, ".tgz") == 0)) {
		archive_write_add_filter_gzip (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_GZIP;
	}
	else if ((strcmp (ext, ".tar.lz") == 0) || (strcmp (ext, ".tlz") == 0)) {
		archive_write_add_filter_lzip (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_LZIP;
	}
	else if ((strcmp (ext, ".tar.lzma") == 0) || (strcmp (ext, ".tzma") == 0)) {
		archive_write_add_filter_lzma (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_LZMA;
	}
	else if ((strcmp (ext, ".tar.xz") == 0) || (strcmp (ext, ".txz") == 0)) {
		archive_write_add_filter_xz (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_XZ;
	}
	else {
		/* defaults to an uncompressed tar */
		archive_write_add_filter_none (a);
		archive_write_set_format_ustar (a);
		archive_filter = ARCHIVE_FILTER_NONE;
	}

	/* FIXME: add all the libarchive supported formats */

	/* set the compression level */

	if (archive_filter != ARCHIVE_FILTER_NONE) {
		char *compression_level = NULL;

		switch (save_data->compression) {
		case FR_COMPRESSION_VERY_FAST:
			compression_level = "1";
			break;
		case FR_COMPRESSION_FAST:
			compression_level = "3";
			break;
		case FR_COMPRESSION_NORMAL:
			compression_level = "6";
			break;
		case FR_COMPRESSION_MAXIMUM:
			compression_level = "9";
			break;
		}
		if (compression_level != NULL)
			archive_write_set_filter_option (a, NULL, "compression-level", compression_level);
	}

	g_free (uri);
}


/* -- _archive_write_file -- */


static gint64 *
_g_int64_pointer_new (gint64 i)
{
	gint64 *p;

	p = g_new (gint64, 1);
	*p = i;

	return p;
}


static gboolean
_archive_entry_copy_file_info (struct archive_entry *entry,
			       GFileInfo            *info,
			       SaveData             *save_data)
{
	int     filetype;
	char   *username;
	char   *groupname;
	gint64  id;

	switch (g_file_info_get_file_type (info)) {
	case G_FILE_TYPE_REGULAR:
		filetype = AE_IFREG;
		break;
	case G_FILE_TYPE_DIRECTORY:
		filetype = AE_IFDIR;
		break;
	case G_FILE_TYPE_SYMBOLIC_LINK:
		filetype = AE_IFLNK;
		break;
	default:
		return FALSE;
		break;
	}
	archive_entry_set_filetype (entry, filetype);

	archive_entry_set_atime (entry,
				 g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_ACCESS),
				 g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_ACCESS_USEC) * 1000);
	archive_entry_set_ctime (entry,
				 g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CREATED),
				 g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_CREATED_USEC) * 1000);
	archive_entry_set_mtime (entry,
				 g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED),
				 g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC) * 1000);
	archive_entry_unset_birthtime (entry);
	archive_entry_set_dev (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_DEVICE));
	archive_entry_set_gid (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID));
	archive_entry_set_uid (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID));
	archive_entry_set_ino64 (entry, g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_UNIX_INODE));
	archive_entry_set_mode (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE));
	archive_entry_set_nlink (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_NLINK));
	archive_entry_set_rdev (entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_RDEV));
	archive_entry_set_size (entry, g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE));
	if (filetype == AE_IFLNK) {
		/* FIXME: allow to store symlinks */
		g_print ("symlink: %s\n", g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET));
		archive_entry_set_symlink (entry, g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET));
	}

	/* username */

	id = archive_entry_uid (entry);
	username = g_hash_table_lookup (save_data->usernames, &id);
	if (username == NULL) {
		struct passwd *pwd = getpwuid (id);
		if (pwd != NULL) {
			username = g_strdup (pwd->pw_name);
			g_hash_table_insert (save_data->usernames, _g_int64_pointer_new (id), username);
		}
	}
	if (username != NULL)
		archive_entry_set_uname (entry, username);

	/* groupname */

	id = archive_entry_gid (entry);
	groupname = g_hash_table_lookup (save_data->groupnames, &id);
	if (groupname == NULL) {
		struct group *grp = getgrgid (id);
		if (grp != NULL) {
			groupname = g_strdup (grp->gr_name);
			g_hash_table_insert (save_data->groupnames, _g_int64_pointer_new (id), groupname);
		}
	}
	if (groupname != NULL)
		archive_entry_set_gname (entry, groupname);

	return TRUE;
}


static WriteAction
_archive_write_file (struct archive       *b,
		     SaveData             *save_data,
		     AddFile              *add_file,
		     struct archive_entry *r_entry,
		     GCancellable         *cancellable)
{
	LoadData             *load_data = LOAD_DATA (save_data);
	GFileInfo            *info;
	struct archive_entry *w_entry;
	int                   rb;

	/* write the file header */

	info = g_file_query_info (add_file->file,
				  FILE_ATTRIBUTES_NEEDED_BY_ARCHIVE_ENTRY,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  &load_data->error);
	if (info == NULL)
		return WRITE_ACTION_ABORT;

	w_entry = archive_entry_new ();
	if (! _archive_entry_copy_file_info (w_entry, info, save_data)) {
		archive_entry_free (w_entry);
		g_object_unref (info);
		return WRITE_ACTION_SKIP_ENTRY;
	}

	/* honor the update flag */

	if (save_data->update && (r_entry != NULL) && (archive_entry_mtime (w_entry) < archive_entry_mtime (r_entry))) {
		archive_entry_free (w_entry);
		g_object_unref (info);
		return WRITE_ACTION_WRITE_ENTRY;
	}

	archive_entry_set_pathname (w_entry, add_file->pathname);
	rb = archive_write_header (b, w_entry);

	/* write the file data */

	if (g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR) {
		GInputStream *istream;

		istream = (GInputStream *) g_file_read (add_file->file, cancellable, &load_data->error);
		if (istream != NULL) {
			gssize bytes_read;

			while ((bytes_read = g_input_stream_read (istream, save_data->buffer, save_data->buffer_size, cancellable, &load_data->error)) > 0) {
				archive_write_data (b, save_data->buffer, bytes_read);
				fr_archive_progress_inc_completed_bytes (load_data->archive, bytes_read);
			}

			g_object_unref (istream);
		}
	}

	rb = archive_write_finish_entry (b);

	if ((load_data->error == NULL) && (rb != ARCHIVE_OK))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (b));

	archive_entry_free (w_entry);
	g_object_unref (info);

	return (load_data->error == NULL) ? WRITE_ACTION_SKIP_ENTRY : WRITE_ACTION_ABORT;
}


static void
save_archive_thread (GSimpleAsyncResult *result,
		     GObject            *object,
		     GCancellable       *cancellable)
{
	SaveData             *save_data;
	LoadData             *load_data;
	struct archive       *a, *b;
	struct archive_entry *r_entry;
	int                   ra, rb;

	save_data = g_simple_async_result_get_op_res_gpointer (result);
	load_data = LOAD_DATA (save_data);

	save_data->b = b = archive_write_new ();
	_archive_write_set_format_from_context (b, save_data);
	archive_write_open (b, save_data, save_data_open, save_data_write, save_data_close);

	a = archive_read_new ();
	archive_read_support_filter_all (a);
	archive_read_support_format_all (a);
	archive_read_open (a, load_data, load_data_open, load_data_read, load_data_close);

	if (save_data->begin_operation != NULL)
		save_data->begin_operation (save_data, save_data->user_data);

	while ((load_data->error == NULL) && (ra = archive_read_next_header (a, &r_entry)) == ARCHIVE_OK) {
		struct archive_entry *w_entry;
		WriteAction           action;

		if (g_cancellable_is_cancelled (cancellable))
			break;

		action = WRITE_ACTION_WRITE_ENTRY;
		w_entry = archive_entry_clone (r_entry);
		if (save_data->entry_action != NULL)
			action = save_data->entry_action (save_data, w_entry, save_data->user_data);

		if (action == WRITE_ACTION_WRITE_ENTRY) {
			const void   *buffer;
			size_t        buffer_size;
			__LA_INT64_T  offset;

			rb = archive_write_header (b, w_entry);
			if (rb != ARCHIVE_OK)
				break;

			switch (archive_entry_filetype (r_entry)) {
			case AE_IFREG:
				while ((ra = archive_read_data_block (a, &buffer, &buffer_size, &offset)) == ARCHIVE_OK) {
					archive_write_data (b, buffer, buffer_size);
					fr_archive_progress_inc_completed_bytes (load_data->archive, buffer_size);
				}

				if (ra != ARCHIVE_EOF)
					load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (a));
				break;

			default:
				break;
			}

			rb = archive_write_finish_entry (b);
		}

		archive_entry_free (w_entry);
	}

	if (g_error_matches (load_data->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		ra = ARCHIVE_EOF;

	if (save_data->end_operation != NULL)
		save_data->end_operation (save_data, save_data->user_data);

	rb = archive_write_close (b);

	if ((load_data->error == NULL) && (ra != ARCHIVE_EOF))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (a));
	if ((load_data->error == NULL) && (rb != ARCHIVE_OK))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (b));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);

	archive_read_free (a);
	archive_write_free (b);
	save_data_free (save_data);
}


static void
_fr_archive_libarchive_save (FrArchive          *archive,
			     gboolean            update,
			     const char         *password,
			     gboolean            encrypt_header,
			     FrCompression       compression,
			     guint               volume_size,
			     GCancellable       *cancellable,
			     GSimpleAsyncResult *result,
			     SaveDataFunc        begin_operation,
			     SaveDataFunc        end_operation,
			     EntryActionFunc     entry_action,
			     gpointer            user_data,
			     GDestroyNotify      notify)
{
	SaveData *save_data;
	LoadData *load_data;

	save_data = g_new0 (SaveData, 1);
	save_data_init (SAVE_DATA (save_data));

	load_data = LOAD_DATA (save_data);
	load_data->archive = g_object_ref (archive);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = result;

	save_data->update = update;
	save_data->password = g_strdup (password);
	save_data->encrypt_header = encrypt_header;
	save_data->compression = compression;
	save_data->volume_size = volume_size;
	save_data->begin_operation = begin_operation;
	save_data->end_operation = end_operation;
	save_data->entry_action = entry_action;
	save_data->user_data = user_data;
	save_data->user_data_notify = notify;

	g_simple_async_result_set_op_res_gpointer (load_data->result, save_data, NULL);
	g_simple_async_result_run_in_thread (load_data->result,
					     save_archive_thread,
					     G_PRIORITY_DEFAULT,
					     cancellable);
}


/* -- add_files -- */


typedef struct {
	GHashTable *files_to_add;
	int         n_files_to_add;
} AddData;


static AddData *
add_data_new (void)
{
	AddData *add_data;

	add_data = g_new0 (AddData, 1);
	add_data->files_to_add = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) add_file_free);
	add_data->n_files_to_add = 0;

	return add_data;
}


static void
add_data_free (AddData *add_data)
{
	g_hash_table_unref (add_data->files_to_add);
	g_free (add_data);
}


static void
_add_files_begin (SaveData *save_data,
		  gpointer  user_data)
{
	AddData *add_data = user_data;

	fr_archive_progress_set_total_files (LOAD_DATA (save_data)->archive, add_data->n_files_to_add);
}


static WriteAction
_add_files_entry_action (SaveData             *save_data,
			 struct archive_entry *w_entry,
			 gpointer              user_data)
{
	AddData     *add_data = user_data;
	LoadData    *load_data = LOAD_DATA (save_data);
	WriteAction  action;
	const char  *pathname;
	AddFile     *add_file;

	action = WRITE_ACTION_WRITE_ENTRY;
	pathname = archive_entry_pathname (w_entry);
	add_file = g_hash_table_lookup (add_data->files_to_add, pathname);
	if (add_file != NULL) {
		action = _archive_write_file (save_data->b, save_data, add_file, w_entry, load_data->cancellable);
		fr_archive_progress_inc_completed_files (load_data->archive, 1);
		add_data->n_files_to_add--;
		g_hash_table_remove (add_data->files_to_add, pathname);
	}

	return action;
}


static void
_add_files_end (SaveData *save_data,
		gpointer  user_data)
{
	AddData  *add_data = user_data;
	LoadData *load_data = LOAD_DATA (save_data);
	GList    *remaining_files;
	GList    *scan;

	/* allow to add files to a new archive */

	if (g_error_matches (load_data->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		g_clear_error (&load_data->error);

	/* add the files that weren't present in the archive already */

	remaining_files = g_hash_table_get_values (add_data->files_to_add);
	for (scan = remaining_files; (load_data->error == NULL) && scan; scan = scan->next) {
		AddFile *add_file = scan->data;

		if (_archive_write_file (save_data->b,
					 save_data,
					 add_file,
					 NULL,
					 load_data->cancellable) == WRITE_ACTION_ABORT)
		{
			break;
		}
	}

	g_list_free (remaining_files);
}


static void
fr_archive_libarchive_add_files (FrArchive           *archive,
				 GList               *file_list,
				 const char          *base_dir_uri,
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
	AddData *add_data;
	GFile   *base_dir;
	GList   *scan;

	g_return_if_fail (base_dir_uri != NULL);

	add_data = add_data_new ();

	base_dir = g_file_new_for_uri (base_dir_uri);
	if (dest_dir != NULL)
		dest_dir = dest_dir[0] == '/' ? dest_dir + 1 : dest_dir;
	else
		dest_dir = "";
	for (scan = file_list; scan; scan = scan->next) {
		char  *relative_pathname = scan->data;
		char  *full_pathname;
		GFile *file;

		full_pathname = g_build_filename (dest_dir, relative_pathname, NULL);
		file = g_file_get_child (base_dir, relative_pathname);
		g_hash_table_insert (add_data->files_to_add, full_pathname, add_file_new (file, full_pathname));
		add_data->n_files_to_add++;

		g_object_unref (file);
	}

	_fr_archive_libarchive_save (archive,
				     update,
				     password,
				     encrypt_header,
				     compression,
				     volume_size,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_add_files),
				     _add_files_begin,
				     _add_files_end,
				     _add_files_entry_action,
				     add_data,
				     (GDestroyNotify) add_data_free);

	g_object_unref (base_dir);
}


/* -- remove -- */


typedef struct {
	GHashTable *files_to_remove;
	int         n_files_to_remove;
} RemoveData;


static void
remove_data_free (RemoveData *remove_data)
{
	g_hash_table_unref (remove_data->files_to_remove);
	g_free (remove_data);
}


static void
_remove_files_begin (SaveData *save_data,
		     gpointer  user_data)
{
	RemoveData *remove_data = user_data;

	fr_archive_progress_set_total_files (LOAD_DATA (save_data)->archive, remove_data->n_files_to_remove);
}


static WriteAction
_remove_files_entry_action (SaveData             *save_data,
			    struct archive_entry *w_entry,
			    gpointer              user_data)
{
	RemoveData  *remove_data = user_data;
	LoadData    *load_data = LOAD_DATA (save_data);
	WriteAction  action;
	const char  *pathname;

	action = WRITE_ACTION_WRITE_ENTRY;
	pathname = archive_entry_pathname (w_entry);
	if (g_hash_table_lookup (remove_data->files_to_remove, pathname)) {
		action = WRITE_ACTION_SKIP_ENTRY;
		fr_archive_progress_inc_completed_files (load_data->archive, 1);
		remove_data->n_files_to_remove--;
		g_hash_table_remove (remove_data->files_to_remove, pathname);
	}

	return action;
}


static void
fr_archive_libarchive_remove_files (FrArchive           *archive,
				    GList               *file_list,
				    FrCompression        compression,
				    GCancellable        *cancellable,
				    GAsyncReadyCallback  callback,
				    gpointer             user_data)
{
	RemoveData *remove_data;
	GList      *scan;

	remove_data = g_new0 (RemoveData, 1);
	remove_data->files_to_remove = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	remove_data->n_files_to_remove = 0;
	for (scan = file_list; scan; scan = scan->next) {
		g_hash_table_insert (remove_data->files_to_remove, g_strdup (scan->data), GINT_TO_POINTER (1));
		remove_data->n_files_to_remove++;
	}

	_fr_archive_libarchive_save (archive,
				     FALSE,
				     archive->password,
				     archive->encrypt_header,
				     compression,
				     0,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_remove),
     				     _remove_files_begin,
				     NULL,
				     _remove_files_entry_action,
				     remove_data,
				     (GDestroyNotify) remove_data_free);
}


/* -- fr_archive_libarchive_rename -- */


typedef struct {
	GHashTable *files_to_rename;
	int         n_files_to_rename;
} RenameData;


static void
rename_data_free (RenameData *rename_data)
{
	g_hash_table_unref (rename_data->files_to_rename);
	g_free (rename_data);
}


static void
_rename_files_begin (SaveData *save_data,
		     gpointer  user_data)
{
	RenameData *rename_data = user_data;

	fr_archive_progress_set_total_files (LOAD_DATA (save_data)->archive, rename_data->n_files_to_rename);
}


static WriteAction
_rename_files_entry_action (SaveData             *save_data,
			    struct archive_entry *w_entry,
			    gpointer              user_data)
{
	RenameData  *rename_data = user_data;
	LoadData    *load_data = LOAD_DATA (save_data);
	WriteAction  action;
	const char  *pathname;
	char       *new_pathname;

	action = WRITE_ACTION_WRITE_ENTRY;
	pathname = archive_entry_pathname (w_entry);
	new_pathname = g_hash_table_lookup (rename_data->files_to_rename, pathname);
	if (new_pathname != NULL) {
		archive_entry_set_pathname (w_entry, new_pathname);
		fr_archive_progress_inc_completed_files (load_data->archive, 1);
		rename_data->n_files_to_rename--;
		g_hash_table_remove (rename_data->files_to_rename, pathname);
	}

	return action;
}


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
	RenameData *rename_data;
	char       *old_dirname;
	char       *new_dirname;
	int         old_dirname_len;
	GList      *scan;

	rename_data = g_new0 (RenameData, 1);
	rename_data->files_to_rename = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	rename_data->n_files_to_rename = 0;

	old_dirname = g_build_filename (current_dir + 1, old_name, "/", NULL);
	old_dirname_len = strlen (old_dirname);
	new_dirname = g_build_filename (current_dir + 1, new_name, "/", NULL);
	for (scan = file_list; scan; scan = scan->next) {
		char *old_pathname = scan->data;
		char *new_pathname;

		new_pathname = g_build_filename (new_dirname, old_pathname + old_dirname_len, NULL);
		g_hash_table_insert (rename_data->files_to_rename, g_strdup (old_pathname), new_pathname);
		rename_data->n_files_to_rename++;
	}

	_fr_archive_libarchive_save (archive,
				     FALSE,
				     archive->password,
				     archive->encrypt_header,
				     archive->compression,
				     0,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_rename),
     				     _rename_files_begin,
				     NULL,
				     _rename_files_entry_action,
				     rename_data,
				     (GDestroyNotify) rename_data_free);

	g_free (new_dirname);
	g_free (old_dirname);
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
	AddData *add_data;
	GList   *scan;

	g_return_if_fail (base_dir != NULL);

	add_data = add_data_new ();

	current_dir = current_dir + 1;
	for (scan = files; scan; scan = scan->next) {
		const char *old_name = (char *) scan->data;
		char       *new_name;
		char       *filename;
		GFile      *file;

		new_name = g_build_filename (current_dir, old_name + strlen (base_dir) - 1, NULL);
		filename = g_build_filename (tmp_dir, old_name, NULL);
		file = g_file_new_for_path (filename);
		g_hash_table_insert (add_data->files_to_add, new_name, add_file_new (file, new_name));
		add_data->n_files_to_add++;

		g_object_unref (file);
		g_free (filename);
	}

	_fr_archive_libarchive_save (archive,
				     FALSE,
				     password,
				     encrypt_header,
				     compression,
				     volume_size,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_paste_clipboard),
				     _add_files_begin,
				     _add_files_end,
				     _add_files_entry_action,
				     add_data,
				     (GDestroyNotify) add_data_free);
}


/* -- fr_archive_libarchive_add_dropped_files -- */


static void
fr_archive_libarchive_add_dropped_files (FrArchive           *archive,
					 GList               *files,
					 const char          *base_dir,
					 const char          *dest_dir,
					 const char          *password,
					 gboolean             encrypt_header,
					 FrCompression        compression,
					 guint                volume_size,
					 GCancellable        *cancellable,
					 GAsyncReadyCallback  callback,
					 gpointer             user_data)
{
	AddData *add_data;
	GList   *scan;

	g_return_if_fail (base_dir != NULL);

	add_data = add_data_new ();

	if (dest_dir[0] == '/')
		dest_dir += 1;

	for (scan = files; scan; scan = scan->next) {
		const char *uri = (char *) scan->data;
		char       *filepath;
		char       *archive_pathname;
		GFile      *file;

		filepath = g_filename_from_uri (uri, NULL, NULL);
		if (filepath == NULL)
			continue;

		archive_pathname = g_build_filename (dest_dir, _g_path_get_file_name (filepath), NULL);
		file = g_file_new_for_uri (uri);
		g_hash_table_insert (add_data->files_to_add, archive_pathname, add_file_new (file, archive_pathname));

		g_object_unref (file);
		g_free (filepath);
	}

	_fr_archive_libarchive_save (archive,
				     FALSE,
				     password,
				     encrypt_header,
				     compression,
				     volume_size,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_paste_clipboard),
				     _add_files_begin,
				     _add_files_end,
				     _add_files_entry_action,
				     add_data,
				     (GDestroyNotify) add_data_free);
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
	AddData *add_data;
	GList   *scan_file;
	GList   *scan_dir;

	add_data = add_data_new ();

	for (scan_file = file_list, scan_dir = dir_list;
	     scan_file && scan_dir;
	     scan_file = scan_file->next, scan_dir = scan_dir->next)
	{
		char  *temp_dir = scan_dir->data;
		char  *relative_pathname = scan_file->data;
		char  *full_pathname;
		GFile *file;

		full_pathname = g_build_filename (temp_dir, relative_pathname, NULL);
		file = g_file_new_for_path (full_pathname);
		g_hash_table_insert (add_data->files_to_add, g_strdup (relative_pathname), add_file_new (file, relative_pathname));
		add_data->n_files_to_add++;

		g_object_unref (file);
		g_free (full_pathname);
	}

	_fr_archive_libarchive_save (archive,
				     FALSE,
				     password,
				     encrypt_header,
				     compression,
				     volume_size,
				     cancellable,
				     g_simple_async_result_new (G_OBJECT (archive),
				     				callback,
				     				user_data,
				     				fr_archive_paste_clipboard),
				     _add_files_begin,
				     _add_files_end,
				     _add_files_entry_action,
				     add_data,
				     (GDestroyNotify) add_data_free);
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
	archive_class->extract_files = fr_archive_libarchive_extract_files;
	archive_class->add_files = fr_archive_libarchive_add_files;
	archive_class->remove_files = fr_archive_libarchive_remove_files;
	archive_class->rename = fr_archive_libarchive_rename;
	archive_class->paste_clipboard = fr_archive_libarchive_paste_clipboard;
	archive_class->add_dropped_files = fr_archive_libarchive_add_dropped_files;
	archive_class->update_open_files = fr_archive_libarchive_update_open_files;
}


static void
fr_archive_libarchive_init (FrArchiveLibarchive *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_ARCHIVE_LIBARCHIVE, FrArchiveLibarchivePrivate);

	base->propAddCanReplace = TRUE;
	base->propAddCanUpdate = TRUE;
	base->propAddCanStoreFolders = TRUE;
	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder = TRUE;
	base->propExtractCanJunkPaths = TRUE;
	base->propCanExtractAll = TRUE;
	base->propCanDeleteNonEmptyFolders = TRUE;
	base->propCanExtractNonEmptyFolders = TRUE;
}
