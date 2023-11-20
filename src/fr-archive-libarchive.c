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
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <archive.h>
#include <archive_entry.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "fr-error.h"
#include "fr-archive-libarchive.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "typedefs.h"


#define BUFFER_SIZE (64 * 1024)
#define FILE_ATTRIBUTES_NEEDED_BY_ARCHIVE_ENTRY ("standard::*,time::*,access::*,unix::*")


/* workaround the struct types of libarchive */
typedef struct archive _archive_read_ctx;

static void
_archive_read_ctx_free(_archive_read_ctx *arch)
{
	archive_read_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_read_ctx, _archive_read_ctx_free)

typedef struct archive _archive_write_ctx;

static void
_archive_write_ctx_free(_archive_write_ctx *arch)
{
	archive_write_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_write_ctx, _archive_write_ctx_free)

typedef struct archive_entry _archive_entry_ctx;

static void
_archive_entry_ctx_free(_archive_entry_ctx *entry)
{
	archive_entry_free(entry);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_entry_ctx, _archive_entry_ctx_free)


typedef struct {
	gssize compressed_size;
	gssize uncompressed_size;
} FrArchiveLibarchivePrivate;


G_DEFINE_FINAL_TYPE_WITH_PRIVATE (FrArchiveLibarchive, fr_archive_libarchive, FR_TYPE_ARCHIVE)


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
	"application/epub+zip",
	"application/vnd.ms-cab-compressed",
	"application/vnd.rar",
	"application/x-7z-compressed",
	"application/x-bzip-compressed-tar",
	"application/x-cbr",
	"application/x-cbz",
	"application/x-cd-image",
	"application/x-compressed-tar",
	"application/x-cpio",
	"application/x-lha",
	"application/x-lrzip-compressed-tar",
	"application/x-lzip-compressed-tar",
	"application/x-lzma-compressed-tar",
	"application/x-rar",
	"application/x-rpm",
	"application/x-tar",
	"application/x-tarz",
	"application/x-tzo",
	"application/x-xar",
	"application/x-xz-compressed-tar",
#if (ARCHIVE_VERSION_NUMBER >= 3003003)
	"application/x-zstd-compressed-tar",
#endif
	"application/zip",
	NULL
};


static const char **
fr_archive_libarchive_get_mime_types (FrArchive *archive)
{
	return libarchiver_mime_types;
}


static FrArchiveCaps
fr_archive_libarchive_get_capabilities (FrArchive  *archive,
					const char *mime_type,
					gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;

	/* give priority to 7z* for 7z archives. */
	if (strcmp (mime_type, "application/x-7z-compressed") == 0) {
		if (_g_program_is_available ("7zz", TRUE)
		    || _g_program_is_available ("7zzs", TRUE)
		    || _g_program_is_available ("7za", TRUE)
		    || _g_program_is_available ("7zr", TRUE)
		    || _g_program_is_available ("7z", TRUE))
		{
			return capabilities;
		}
	}

	/* give priority to 7za that supports CAB files better. */
	if ((strcmp (mime_type, "application/vnd.ms-cab-compressed") == 0)
	    && _g_program_is_available ("7za", TRUE))
	{
		return capabilities;
	}

	/* give priority to 7z, unzip and zip that supports ZIP files better. */
	if ((strcmp (mime_type, "application/zip") == 0)
	    || (strcmp (mime_type, "application/x-cbz") == 0))
	{
		if (_g_program_is_available ("7zz", TRUE)
		    || _g_program_is_available ("7zzs", TRUE)
		    || _g_program_is_available ("7z", TRUE)) {
			return capabilities;
		}
		if (!_g_program_is_available ("unzip", TRUE)) {
			capabilities |= FR_ARCHIVE_CAN_READ;
		}
		if (!_g_program_is_available ("zip", TRUE)) {
			capabilities |= FR_ARCHIVE_CAN_WRITE;
		}
		return capabilities;
	}

	/* give priority to utilities that support RAR files better. */
	if ((strcmp (mime_type, "application/vnd.rar") == 0)
	    || (strcmp (mime_type, "application/x-rar") == 0)
	    || (strcmp (mime_type, "application/x-cbr") == 0))
	{
		if (_g_program_is_available ("rar", TRUE)
		    || _g_program_is_available ("unrar", TRUE)
		    || _g_program_is_available ("unar", TRUE)) {
			return capabilities;
		}
	}

	/* tar.lrz format requires external lrzip */
	if (strcmp (mime_type, "application/x-lrzip-compressed-tar") == 0) {
		if (!_g_program_is_available ("lrzip", check_command))
			return capabilities;
	}

	capabilities |= FR_ARCHIVE_CAN_READ;

	/* read-only formats */
	if ((strcmp (mime_type, "application/vnd.ms-cab-compressed") == 0)
	    || (strcmp (mime_type, "application/vnd.rar") == 0)
	    || (strcmp (mime_type, "application/x-cbr") == 0)
	    || (strcmp (mime_type, "application/x-lha") == 0)
	    || (strcmp (mime_type, "application/x-rar") == 0)
	    || (strcmp (mime_type, "application/x-rpm") == 0)
	    || (strcmp (mime_type, "application/x-xar") == 0))
	{
		return capabilities;
	}

	/* all other formats can be read and written */
	capabilities |= FR_ARCHIVE_CAN_WRITE;

	return capabilities;
}


static const char *
fr_archive_libarchive_get_packages (FrArchive  *archive,
				    const char *mime_type)
{
	return NULL;
}


/* LoadData */


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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LoadData, load_data_free)


static int
load_data_open (struct archive *a,
		void           *client_data)
{
	LoadData *load_data = client_data;

	if (load_data->error != NULL)
		return ARCHIVE_FATAL;

	if (g_simple_async_result_get_source_tag (load_data->result) == fr_archive_list) {
		FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));
		private->compressed_size = 0;
		private->uncompressed_size = 0;
	}

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
	gssize    bytes;

	if (load_data->error != NULL)
		return -1;

	*buff = load_data->buffer;
	bytes = g_input_stream_read (load_data->istream,
				     load_data->buffer,
				     load_data->buffer_size,
				     load_data->cancellable,
				     &load_data->error);

	/* update the progress only if listing the content */
	if (g_simple_async_result_get_source_tag (load_data->result) == fr_archive_list) {
		FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));
		fr_archive_progress_set_completed_bytes (load_data->archive,
							 g_seekable_tell (G_SEEKABLE (load_data->istream)));
		private->compressed_size += bytes;
	}

	return bytes;
}


static gint64
load_data_seek (struct archive *a,
		void           *client_data,
		gint64          request,
		int             whence)
{
	GSeekable *seekable;
	GSeekType  seektype;
	off_t      new_offset;

	LoadData *load_data = client_data;

	seekable = (GSeekable*)(load_data->istream);
	if ((load_data->error != NULL) || (load_data->istream == NULL))
		return -1;

	switch (whence) {
	case SEEK_SET:
		seektype = G_SEEK_SET;
		break;
	case SEEK_CUR:
		seektype = G_SEEK_CUR;
		break;
	case SEEK_END:
		seektype = G_SEEK_END;
		break;
	default:
		return -1;
	}

	g_seekable_seek (seekable,
			 request,
			 seektype,
			 load_data->cancellable,
			 &load_data->error);
	new_offset = g_seekable_tell (seekable);
	if (load_data->error != NULL)
		return -1;

	return new_offset;
}


static gint64
load_data_skip (struct archive *a,
		void           *client_data,
		gint64          request)
{
	GSeekable *seekable;
	off_t      old_offset, new_offset;

	LoadData *load_data = client_data;

	seekable = (GSeekable*)(load_data->istream);
	if (load_data->error != NULL || load_data->istream == NULL)
		return -1;

	old_offset = g_seekable_tell (seekable);
	new_offset = load_data_seek (a, client_data, request, SEEK_CUR);
	if (new_offset > old_offset)
		return (new_offset - old_offset);

	return 0;
}



static int
load_data_close (struct archive *a,
		 void           *client_data)
{
	LoadData *load_data = client_data;

	if (load_data->error != NULL)
		return ARCHIVE_FATAL;

	if (load_data->istream != NULL) {
		_g_object_unref (load_data->istream);
		load_data->istream = NULL;
	}

	return ARCHIVE_OK;
}


static int
create_read_object (LoadData        *load_data,
                    _archive_read_ctx **a)
{
	*a = archive_read_new ();
	archive_read_support_filter_all (*a);
	archive_read_support_format_all (*a);

	archive_read_set_open_callback (*a, load_data_open);
	archive_read_set_read_callback (*a, load_data_read);
	archive_read_set_close_callback (*a, load_data_close);
	archive_read_set_seek_callback (*a, load_data_seek);
	archive_read_set_skip_callback (*a, load_data_skip);
	archive_read_set_callback_data (*a, load_data);

	return archive_read_open1 (*a);
}


/* -- list -- */


static goffset
_g_file_get_size (GFile        *file,
		  GCancellable *cancellable)
{
	g_autoptr (GFileInfo) info = NULL;
	goffset    size;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_SIZE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  NULL);
	if (info == NULL)
		return 0;

	size = g_file_info_get_size (info);

	return size;
}


static GError *
_g_error_new_from_archive_error (const char *s)
{
	g_autofree char *msg = NULL;
	GError *error;

	msg = (s != NULL) ? g_locale_to_utf8 (s, -1, NULL, NULL, NULL) : NULL;
	if (msg == NULL)
		msg = g_strdup ("Fatal error");
	error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, msg);

	return error;
}


static void
list_archive_thread (GSimpleAsyncResult *result,
		     GObject            *object,
		     GCancellable       *cancellable)
{
	g_autoptr (LoadData) load_data = NULL;
	g_autoptr (_archive_read_ctx) a = NULL;
	struct archive_entry *entry;
	int                   r;

	load_data = g_simple_async_result_get_op_res_gpointer (result);

	fr_archive_progress_set_total_bytes (load_data->archive,
					     _g_file_get_size (fr_archive_get_file (load_data->archive), cancellable));

	r = create_read_object (load_data, &a);
	if (r != ARCHIVE_OK) {
		return;
	}

	while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
		FrFileData *file_data;
		const char *pathname;

		if (g_cancellable_is_cancelled (cancellable))
			break;

		file_data = fr_file_data_new ();

		if (archive_entry_size_is_set (entry)) {
			FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));
			file_data->size = archive_entry_size (entry);
			private->uncompressed_size += file_data->size;
		}

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
			file_data->name = g_strdup (_g_path_get_basename (file_data->full_path));
		file_data->path = _g_path_remove_level (file_data->full_path);

		/*
		g_print ("%s\n", archive_entry_pathname (entry));
		g_print ("\tfull_path: %s\n", file_data->full_path);
		g_print ("\toriginal_path: %s\n", file_data->original_path);
		g_print ("\tname: %s\n", file_data->name);
		g_print ("\tpath: %s\n", file_data->path);
		g_print ("\tlink: %s\n", file_data->link);
		*/

		fr_archive_add_file (load_data->archive, file_data);

		archive_read_data_skip (a);
	}

	if ((load_data->error == NULL) && (r != ARCHIVE_EOF) && (archive_error_string (a) != NULL))
		load_data->error = _g_error_new_from_archive_error (archive_error_string (a));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);
}


static void
fr_archive_libarchive_list (FrArchive           *archive,
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
						       fr_archive_list);

	g_simple_async_result_set_op_res_gpointer (load_data->result, load_data, NULL);
	g_simple_async_result_run_in_thread (load_data->result,
					     list_archive_thread,
					     G_PRIORITY_DEFAULT,
					     cancellable);
}


/* -- extract -- */


#define NULL_BUFFER_SIZE (16 * 1024)


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
	GHashTable *usernames;
	GHashTable *groupnames;
	char       *null_buffer;
} ExtractData;


static void
extract_data_free (ExtractData *extract_data)
{
	g_free (extract_data->base_dir);
	_g_object_unref (extract_data->destination);
	_g_string_list_free (extract_data->file_list);
	g_hash_table_unref (extract_data->files_to_extract);
	g_hash_table_unref (extract_data->usernames);
	g_hash_table_unref (extract_data->groupnames);
	g_free (extract_data->null_buffer);
	load_data_free (LOAD_DATA (extract_data));
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ExtractData, extract_data_free)


static gboolean
extract_data_get_extraction_requested (ExtractData *extract_data,
				       const char  *pathname)
{
	if (extract_data->file_list != NULL)
		return g_hash_table_lookup (extract_data->files_to_extract, pathname) != NULL;
	else
		return TRUE;
}


static GFileInfo *
_g_file_info_create_from_entry (struct archive_entry *entry,
			        ExtractData          *extract_data)
{
	GFileInfo *info;

	info = g_file_info_new ();

	/* times */

	if (archive_entry_mtime_is_set (entry))
		g_file_info_set_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED, archive_entry_mtime (entry));

	/* username */

	if (archive_entry_uname (entry) != NULL) {
		guint32 uid;

		uid = GPOINTER_TO_INT (g_hash_table_lookup (extract_data->usernames, archive_entry_uname (entry)));
		if (uid == 0) {
			struct passwd *pwd = getpwnam (archive_entry_uname (entry));
			if (pwd != NULL) {
				uid = pwd->pw_uid;
				g_hash_table_insert (extract_data->usernames, g_strdup (archive_entry_uname (entry)), GINT_TO_POINTER (uid));
			}
		}
		if (uid != 0)
			g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID, uid);
	}

	/* groupname */

	if (archive_entry_gname (entry) != NULL) {
		guint32 gid;

		gid = GPOINTER_TO_INT (g_hash_table_lookup (extract_data->groupnames, archive_entry_gname (entry)));
		if (gid == 0) {
			struct group *grp = getgrnam (archive_entry_gname (entry));
			if (grp != NULL) {
				gid = grp->gr_gid;
				g_hash_table_insert (extract_data->groupnames, g_strdup (archive_entry_gname (entry)), GINT_TO_POINTER (gid));
			}
		}
		if (gid != 0)
			g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID, gid);
	}

	/* permsissions */

	g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE, archive_entry_mode (entry));

	return info;
}


static gboolean
_g_file_set_attributes_from_info (GFile         *file,
				  GFileInfo     *info,
				  GCancellable  *cancellable,
				  GError       **error)
{
	return g_file_set_attributes_from_info (file, info, G_FILE_QUERY_INFO_NONE, cancellable, error);
}


static void
restore_original_file_attributes (GHashTable    *created_files,
				  GCancellable  *cancellable)
{
	GHashTableIter iter;
	gpointer       key, value;

	g_hash_table_iter_init (&iter, created_files);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GFile     *file = key;
		GFileInfo *info = value;

		_g_file_set_attributes_from_info (file, info, cancellable, NULL);
	}
}


static gboolean
_g_output_stream_add_padding (ExtractData    *extract_data,
			      GOutputStream  *ostream,
			      gssize          target_offset,
			      gssize          actual_offset,
			      GCancellable   *cancellable,
			      GError        **error)
{
	gboolean success = TRUE;
	gsize    count;
	gsize    bytes_written;

	if (extract_data->null_buffer == NULL)
		extract_data->null_buffer = g_malloc0 (NULL_BUFFER_SIZE);

	while (target_offset > actual_offset) {
		count = NULL_BUFFER_SIZE;
		if (target_offset < actual_offset + NULL_BUFFER_SIZE)
			count = target_offset - actual_offset;

		success = g_output_stream_write_all (ostream, extract_data->null_buffer, count, &bytes_written, cancellable, error);
		if (! success)
			break;

		actual_offset += bytes_written;
	}

	return success;
}

static gboolean
_g_file_contains_symlinks_in_path (const char *relative_path,
				   GFile      *destination,
				   GHashTable *symlinks)
{
	gboolean  contains_symlinks = FALSE;
	g_autoptr (GFile) parent = NULL;
	char    **components;
	int       i;

	if (relative_path == NULL)
		return FALSE;

	if (destination == NULL)
		return TRUE;

	parent = g_object_ref (destination);
	components = g_strsplit (relative_path, "/", -1);
	for (i = 0; (components[i] != NULL) && (components[i + 1] != NULL); i++) {
		GFile *tmp;

		if (components[i][0] == 0)
			continue;

		tmp = g_file_get_child (parent, components[i]);
		parent = tmp;

		if (g_hash_table_contains (symlinks, parent)) {
			contains_symlinks = TRUE;
			break;
		}
	}

	g_strfreev (components);

	return contains_symlinks;
}


static void
extract_archive_thread (GSimpleAsyncResult *result,
			GObject            *object,
			GCancellable       *cancellable)
{
	g_autoptr (ExtractData) extract_data = NULL;
	LoadData             *load_data;
	g_autoptr (GHashTable) checked_folders = NULL;
	g_autoptr (GHashTable) created_files = NULL;
	g_autoptr (GHashTable) folders_created_during_extraction = NULL;
	g_autoptr (GHashTable) symlinks = NULL;
	g_autoptr (_archive_read_ctx) a = NULL;
	struct archive_entry *entry;
	int                   r;

	extract_data = g_simple_async_result_get_op_res_gpointer (result);
	load_data = LOAD_DATA (extract_data);

	r = create_read_object (load_data, &a);
	if (r != ARCHIVE_OK) {
		return;
	}

	checked_folders = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);
	created_files = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, g_object_unref);
	folders_created_during_extraction = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);
	symlinks = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);
	fr_archive_progress_set_total_files (load_data->archive, extract_data->n_files_to_extract);

	while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
		const char    *pathname;
		g_autofree char * fullpath = NULL;
		const char    *relative_path;
		g_autoptr (GFile) file = NULL;
		g_autoptr (GFile) parent = NULL;
		g_autoptr (GOutputStream) ostream = NULL;
		const void    *buffer;
		size_t         buffer_size;
		int64_t target_offset = 0;
		int64_t actual_offset = 0;
		GError        *local_error = NULL;
		__LA_MODE_T    filetype;

		if (g_cancellable_is_cancelled (cancellable))
			break;

		pathname = archive_entry_pathname (entry);
		if (! extract_data_get_extraction_requested (extract_data, pathname)) {
			archive_read_data_skip (a);
			continue;
		}

		fullpath = (*pathname == '/') ? g_strdup (pathname) : g_strconcat ("/", pathname, NULL);
		relative_path = _g_path_get_relative_basename_safe (fullpath, extract_data->base_dir, extract_data->junk_paths);
		if (relative_path == NULL) {
			fr_archive_progress_inc_completed_files (load_data->archive, 1);
			fr_archive_progress_inc_completed_bytes (load_data->archive, archive_entry_size_is_set (entry) ? archive_entry_size (entry) : 0);
			archive_read_data_skip (a);
			continue;
		}

		/* Symlinks in parents are dangerous as it can easily happen
		 * that files are written outside of the destination. The tar
		 * cmd fails to extract such archives with ENOTDIR. Let's skip
		 * those files here for sure. This is most probably malicious,
		 * or corrupted archive.
		 */
		if (_g_file_contains_symlinks_in_path (relative_path, extract_data->destination, symlinks)) {
			g_warning ("Skipping '%s' file as it has symlink in parents.", relative_path);
			fr_archive_progress_inc_completed_files (load_data->archive, 1);
			fr_archive_progress_inc_completed_bytes (load_data->archive, archive_entry_size_is_set (entry) ? archive_entry_size (entry) : 0);
			archive_read_data_skip (a);
			continue;
		}

		file = g_file_get_child (extract_data->destination, relative_path);

		/* honor the skip_older and overwrite options */

		if ((g_hash_table_lookup (folders_created_during_extraction, file) == NULL)
		    && (extract_data->skip_older || ! extract_data->overwrite))
		{
			g_autoptr (GFileInfo) info = NULL;

			info = g_file_query_info (file,
						  G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
						  G_FILE_QUERY_INFO_NONE,
						  cancellable,
						  &local_error);
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

				if (skip) {
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
					break;
				}
				g_clear_error (&local_error);
			}
		}

		fr_archive_progress_inc_completed_files (load_data->archive, 1);

		/* create the file parents */

		parent = g_file_get_parent (file);

		if ((parent != NULL)
		    && (g_hash_table_lookup (checked_folders, parent) == NULL)
		    && ! g_file_query_exists (parent, cancellable))
		{
			if (! _g_file_make_directory_with_parents (parent,
								   folders_created_during_extraction,
								   cancellable,
								   &local_error))
			{
				if (! g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
					load_data->error = local_error;
				else
					g_clear_error (&local_error);
			}

			if (load_data->error == NULL) {
				GFile *grandparent;

				grandparent = g_object_ref (parent);
				while (grandparent != NULL) {
					if (g_hash_table_lookup (checked_folders, grandparent) == NULL)
						g_hash_table_insert (checked_folders, grandparent, GINT_TO_POINTER (1));
					grandparent = g_file_get_parent (grandparent);
				}
			}
		}

		/* create the file */

		filetype = archive_entry_filetype (entry);

		if (load_data->error == NULL) {
			const char  *linkname;

			linkname = archive_entry_hardlink (entry);
			if (linkname != NULL) {
				g_autofree char *link_fullpath = NULL;
				const char *relative_path;
				g_autoptr (GFile) link_file = NULL;
				g_autofree char *oldname = NULL;
				g_autofree char *newname = NULL;
				int          r;

				link_fullpath = (*linkname == '/') ? g_strdup (linkname) : g_strconcat ("/", linkname, NULL);
				relative_path = _g_path_get_relative_basename_safe (link_fullpath, extract_data->base_dir, extract_data->junk_paths);
				if (relative_path == NULL) {
					archive_read_data_skip (a);
					continue;
				}

				link_file = g_file_get_child (extract_data->destination, relative_path);
				oldname = g_file_get_path (link_file);
				newname = g_file_get_path (file);

				if ((oldname != NULL) && (newname != NULL))
					r = link (oldname, newname);
				else
					r = -1;

				if (r == 0) {
					__LA_INT64_T filesize;

					if (archive_entry_size_is_set (entry))
						filesize = archive_entry_size (entry);
					else
						filesize = -1;

					if (filesize > 0)
						filetype = AE_IFREG; /* treat as a regular file to save the data */
				}
				else {
					g_autofree char *uri = g_file_get_uri (file);
					g_autofree char *msg = g_strdup_printf ("Could not create the hard link %s", uri);
					load_data->error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, msg);
				}
			}
		}

		if (load_data->error == NULL) {
			switch (filetype) {
			case AE_IFDIR:
				if (! g_file_make_directory (file, cancellable, &local_error)) {
					if (! g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
						load_data->error = g_error_copy (local_error);
					g_clear_error (&local_error);
				}
				if (load_data->error == NULL)
					g_hash_table_insert (created_files, g_object_ref (file), _g_file_info_create_from_entry (entry, extract_data));
				archive_read_data_skip (a);
				break;

			case AE_IFREG:
				ostream = (GOutputStream *) g_file_replace (file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, cancellable, &load_data->error);
				if (ostream == NULL)
					break;

				while ((r = archive_read_data_block (a, &buffer, &buffer_size, &target_offset)) == ARCHIVE_OK) {
					gsize bytes_written;

					if (target_offset > actual_offset) {
						if (! _g_output_stream_add_padding (extract_data, ostream, target_offset, actual_offset, cancellable, &load_data->error))
							break;
						fr_archive_progress_inc_completed_bytes (load_data->archive, target_offset - actual_offset);
						actual_offset = target_offset;
					}

					if (! g_output_stream_write_all (ostream, buffer, buffer_size, &bytes_written, cancellable, &load_data->error))
						break;

					actual_offset += bytes_written;
					fr_archive_progress_inc_completed_bytes (load_data->archive, bytes_written);
				}

				if ((r == ARCHIVE_EOF) && (target_offset > actual_offset))
					_g_output_stream_add_padding (extract_data, ostream, target_offset, actual_offset, cancellable, &load_data->error);

				if (r != ARCHIVE_EOF)
					load_data->error = _g_error_new_from_archive_error (archive_error_string (a));
				else
					g_hash_table_insert (created_files, g_object_ref (file), _g_file_info_create_from_entry (entry, extract_data));
				break;

			case AE_IFLNK:
				if (! g_file_make_symbolic_link (file, archive_entry_symlink (entry), cancellable, &local_error)) {
					if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
						g_clear_error (&local_error);
						if (g_file_delete (file, cancellable, &local_error)) {
							g_clear_error (&local_error);
							if (! g_file_make_symbolic_link (file, archive_entry_symlink (entry), cancellable, &local_error))
								load_data->error = g_error_copy (local_error);
						}
						else
							load_data->error = g_error_copy (local_error);
					}
					else
						load_data->error = g_error_copy (local_error);
					g_clear_error (&local_error);
				}
				if (load_data->error == NULL)
					g_hash_table_add (symlinks, g_object_ref (file));
				archive_read_data_skip (a);
				break;

			default:
				archive_read_data_skip (a);
				break;
			}
		}

		if (load_data->error != NULL)
			break;

		if ((extract_data->file_list != NULL) && (--extract_data->n_files_to_extract == 0)) {
			r = ARCHIVE_EOF;
			break;
		}
	}

	if (load_data->error == NULL)
		restore_original_file_attributes (created_files, cancellable);

	if ((load_data->error == NULL) && (r != ARCHIVE_EOF))
		load_data->error = _g_error_new_from_archive_error (archive_error_string (a));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);
}


static void
fr_archive_libarchive_extract_files (FrArchive           *archive,
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
						       fr_archive_extract);
	load_data->buffer_size = BUFFER_SIZE;
	load_data->buffer = g_new (char, load_data->buffer_size);

	extract_data->file_list = _g_string_list_dup (file_list);
	extract_data->destination = g_object_ref (destination);
	extract_data->base_dir = g_strdup (base_dir);
	extract_data->skip_older = skip_older;
	extract_data->overwrite = overwrite;
	extract_data->junk_paths = junk_paths;
	extract_data->files_to_extract = g_hash_table_new (g_str_hash, g_str_equal);
	extract_data->n_files_to_extract = 0;
	extract_data->usernames = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	extract_data->groupnames = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SaveData, save_data_free)


static int
save_data_open (struct archive *a,
	        void           *client_data)
{
	SaveData *save_data = client_data;
	LoadData *load_data = LOAD_DATA (save_data);
	g_autoptr (GFile) parent = NULL;
	g_autofree char * basename = NULL;
	g_autofree char * tmpname = NULL;

	if (load_data->error != NULL)
		return ARCHIVE_FATAL;

	parent = g_file_get_parent (fr_archive_get_file (load_data->archive));
	basename = g_file_get_basename (fr_archive_get_file (load_data->archive));
	tmpname = _g_filename_get_random (16, basename);
	save_data->tmp_file = g_file_get_child (parent, tmpname);
	save_data->ostream = (GOutputStream *) g_file_create (save_data->tmp_file, G_FILE_CREATE_NONE, load_data->cancellable, &load_data->error);

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

	if (load_data->error != NULL)
		return -1;

	return g_output_stream_write (save_data->ostream, buff, n, load_data->cancellable, &load_data->error);
}


static int
save_data_close (struct archive *a,
		 void           *client_data)
{
	SaveData *save_data = client_data;
	LoadData *load_data = LOAD_DATA (save_data);

	if (save_data->ostream != NULL) {
		GError *error = NULL;

		g_output_stream_close (save_data->ostream, load_data->cancellable, &error);
		if (load_data->error == NULL && error != NULL)
			load_data->error = g_error_copy (error);

		_g_error_free (error);
	}

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

	return ARCHIVE_OK;
}


static void
_archive_write_set_format_from_context (struct archive *a,
					SaveData       *save_data)
{
	const char *mime_type;
	int         archive_filter;

	/* set format and filter from the mime type */

	mime_type = fr_archive_get_mime_type (LOAD_DATA (save_data)->archive);
	archive_filter = ARCHIVE_FILTER_NONE;

	if (_g_str_equal (mime_type, "application/x-bzip-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_BZIP2;
	}
	else if (_g_str_equal (mime_type, "application/x-tarz")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_COMPRESS;
	}
	else if (_g_str_equal (mime_type, "application/x-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_GZIP;
	}
	else if (_g_str_equal (mime_type, "application/x-lrzip-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_LRZIP;
	}
	else if (_g_str_equal (mime_type, "application/x-lzip-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_LZIP;
	}
	else if (_g_str_equal (mime_type, "application/x-lzma-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_LZMA;
	}
	else if (_g_str_equal (mime_type, "application/x-tzo")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_LZOP;
	}
	else if (_g_str_equal (mime_type, "application/x-xz-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_XZ;
	}
#if (ARCHIVE_VERSION_NUMBER >= 3003003)
	else if (_g_str_equal (mime_type, "application/x-zstd-compressed-tar")) {
		archive_write_set_format_pax_restricted (a);
		archive_filter = ARCHIVE_FILTER_ZSTD;
	}
#endif
	else if (_g_str_equal (mime_type, "application/x-tar")) {
		archive_write_add_filter_none (a);
		archive_write_set_format_pax_restricted (a);
	}
	else if (_g_str_equal (mime_type, "application/x-cd-image")) {
		archive_write_set_format_iso9660 (a);
	}
	else if (_g_str_equal (mime_type, "application/x-cpio")) {
		archive_write_set_format_cpio (a);
	}
	else if (_g_str_equal (mime_type, "application/x-xar")) {
		archive_write_set_format_xar (a);
	}
	else if (_g_str_equal (mime_type, "application/x-7z-compressed")) {
		archive_write_set_format_7zip (a);
	}
	else if (_g_str_equal (mime_type, "application/zip")
	    || _g_str_equal (mime_type, "application/x-cbz")) {
		archive_write_set_format_zip (a);
	}

	/* set the filter */

	if (archive_filter != ARCHIVE_FILTER_NONE) {
		char *compression_level = NULL;

		switch (archive_filter) {
		case ARCHIVE_FILTER_BZIP2:
			archive_write_add_filter_bzip2 (a);
			break;
		case ARCHIVE_FILTER_COMPRESS:
			archive_write_add_filter_compress (a);
			break;
		case ARCHIVE_FILTER_GZIP:
			archive_write_add_filter_gzip (a);
			break;
		case ARCHIVE_FILTER_LRZIP:
			archive_write_add_filter_lrzip (a);
			break;
		case ARCHIVE_FILTER_LZIP:
			archive_write_add_filter_lzip (a);
			break;
		case ARCHIVE_FILTER_LZMA:
			archive_write_add_filter_lzma (a);
			break;
		case ARCHIVE_FILTER_LZOP:
			archive_write_add_filter_lzop (a);
			break;
		case ARCHIVE_FILTER_XZ:
			archive_write_add_filter_xz (a);
			break;
#if (ARCHIVE_VERSION_NUMBER >= 3003003)
		case ARCHIVE_FILTER_ZSTD:
			archive_write_add_filter_zstd (a);
			break;
#endif
		default:
			break;
		}

		/* set the compression level */

		compression_level = NULL;
#if (ARCHIVE_VERSION_NUMBER >= 3003003)
		if (archive_filter == ARCHIVE_FILTER_ZSTD) {
			switch (save_data->compression) {
			case FR_COMPRESSION_VERY_FAST:
				compression_level = "1";
				break;
			case FR_COMPRESSION_FAST:
				compression_level = "2";
				break;
			case FR_COMPRESSION_NORMAL:
				compression_level = "3";
				break;
			case FR_COMPRESSION_MAXIMUM:
				compression_level = "22";
				break;
			}
		}
		else
#endif
		{
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
		}
		if (compression_level != NULL)
			archive_write_set_filter_option (a, NULL, "compression-level", compression_level);

		/* set the amount of threads */

		if (archive_filter == ARCHIVE_FILTER_XZ) {
			archive_write_set_filter_option (a, NULL, "threads", fr_get_thread_count());
		}
#if (ARCHIVE_VERSION_NUMBER >= 3006000)
		if (archive_filter == ARCHIVE_FILTER_ZSTD) {
			archive_write_set_filter_option (a, NULL, "threads", fr_get_thread_count());
		}
#endif
	}
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
	if (filetype == AE_IFLNK)
		archive_entry_set_symlink (entry, g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET));

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
		     gboolean              follow_link,
		     struct archive_entry *r_entry,
		     GCancellable         *cancellable)
{
	LoadData             *load_data = LOAD_DATA (save_data);
	g_autoptr (GFileInfo) info = NULL;
	g_autoptr (_archive_entry_ctx) w_entry = NULL;
	int                   rb;

	/* write the file header */

	info = g_file_query_info (add_file->file,
				  FILE_ATTRIBUTES_NEEDED_BY_ARCHIVE_ENTRY,
				  (! follow_link ? G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS : 0),
				  cancellable,
				  &load_data->error);
	if (info == NULL)
		return WRITE_ACTION_ABORT;

	w_entry = archive_entry_new ();
	if (! _archive_entry_copy_file_info (w_entry, info, save_data)) {
		return WRITE_ACTION_SKIP_ENTRY;
	}

	/* honor the update flag */

	if (save_data->update && (r_entry != NULL) && (archive_entry_mtime (w_entry) < archive_entry_mtime (r_entry))) {
		return WRITE_ACTION_WRITE_ENTRY;
	}

	archive_entry_set_pathname (w_entry, add_file->pathname);
	rb = archive_write_header (b, w_entry);

	/* write the file data */

	if (g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR) {
		g_autoptr (GInputStream) istream = NULL;

		istream = (GInputStream *) g_file_read (add_file->file, cancellable, &load_data->error);
		if (istream != NULL) {
			gssize bytes_read;

			while ((bytes_read = g_input_stream_read (istream, save_data->buffer, save_data->buffer_size, cancellable, &load_data->error)) > 0) {
				archive_write_data (b, save_data->buffer, bytes_read);
				fr_archive_progress_inc_completed_bytes (load_data->archive, bytes_read);
			}
		}
	}

	rb = archive_write_finish_entry (b);

	if ((load_data->error == NULL) && (rb <= ARCHIVE_FAILED))
		load_data->error = g_error_new_literal (FR_ERROR, FR_ERROR_COMMAND_ERROR, archive_error_string (b));

	return (load_data->error == NULL) ? WRITE_ACTION_SKIP_ENTRY : WRITE_ACTION_ABORT;
}


static void
save_archive_thread (GSimpleAsyncResult *result,
		     GObject            *object,
		     GCancellable       *cancellable)
{
	g_autoptr (SaveData) save_data = NULL;
	LoadData             *load_data;
	g_autoptr (_archive_read_ctx) a = NULL;
	g_autoptr (_archive_write_ctx) b = NULL;
	struct archive_entry *r_entry;
	int                   ra = ARCHIVE_OK, rb = ARCHIVE_OK;

	save_data = g_simple_async_result_get_op_res_gpointer (result);
	load_data = LOAD_DATA (save_data);

	save_data->b = b = archive_write_new ();
	_archive_write_set_format_from_context (b, save_data);
	archive_write_open (b, save_data, save_data_open, save_data_write, save_data_close);
	archive_write_set_bytes_in_last_block (b, 1);

	create_read_object (load_data, &a);

	if (save_data->begin_operation != NULL)
		save_data->begin_operation (save_data, save_data->user_data);

	while ((load_data->error == NULL) && (ra = archive_read_next_header (a, &r_entry)) == ARCHIVE_OK) {
		g_autoptr (_archive_entry_ctx) w_entry = NULL;
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
			if (rb <= ARCHIVE_FAILED) {
				load_data->error = _g_error_new_from_archive_error (archive_error_string (b));
				break;
			}

			switch (archive_entry_filetype (r_entry)) {
			case AE_IFREG:
				while ((ra = archive_read_data_block (a, &buffer, &buffer_size, &offset)) == ARCHIVE_OK) {
					archive_write_data (b, buffer, buffer_size);
					fr_archive_progress_inc_completed_bytes (load_data->archive, buffer_size);
				}

				if (ra <= ARCHIVE_FAILED) {
					load_data->error = _g_error_new_from_archive_error (archive_error_string (a));
					break;
				}
				break;

			default:
				break;
			}

			rb = archive_write_finish_entry (b);
		}
		else if (action == WRITE_ACTION_SKIP_ENTRY)
			fr_archive_progress_inc_completed_bytes (load_data->archive, archive_entry_size (r_entry));
	}

	if (g_error_matches (load_data->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		ra = ARCHIVE_EOF;

	if (save_data->end_operation != NULL)
		save_data->end_operation (save_data, save_data->user_data);

	rb = archive_write_close (b);

	if ((load_data->error == NULL) && (ra != ARCHIVE_EOF))
		load_data->error = _g_error_new_from_archive_error (archive_error_string (a));
	if ((load_data->error == NULL) && (rb <= ARCHIVE_FAILED))
		load_data->error =  _g_error_new_from_archive_error (archive_error_string (b));
	if (load_data->error == NULL)
		g_cancellable_set_error_if_cancelled (cancellable, &load_data->error);
	if (load_data->error != NULL)
		g_simple_async_result_set_from_error (result, load_data->error);
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
	gboolean    follow_links;
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
	add_data->follow_links = TRUE;

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
	AddData  *add_data = user_data;
	LoadData *load_data = LOAD_DATA (save_data);

	fr_archive_progress_set_total_files (load_data->archive, add_data->n_files_to_add);

	if (load_data->archive->files_to_add_size == 0) {
		g_autoptr (GList) files_to_add = NULL;
		GList *scan;

		files_to_add = g_hash_table_get_values (add_data->files_to_add);
		for (scan = files_to_add; scan; scan = scan->next) {
			AddFile *add_file = scan->data;

			if (g_cancellable_is_cancelled (load_data->cancellable))
				break;

			load_data->archive->files_to_add_size += _g_file_get_size (add_file->file, load_data->cancellable);
		}
	}

	FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));
	fr_archive_progress_set_total_bytes (load_data->archive, private->uncompressed_size + load_data->archive->files_to_add_size);
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
		action = _archive_write_file (save_data->b,
					      save_data,
					      add_file,
					      add_data->follow_links,
					      w_entry,
					      load_data->cancellable);
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
	g_autoptr (GList) remaining_files = NULL;
	GList    *scan;

	/* allow to add files to a new archive */

	if (g_error_matches (load_data->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		g_clear_error (&load_data->error);

	/* add the files that weren't present in the archive already */

	remaining_files = g_hash_table_get_values (add_data->files_to_add);
	for (scan = remaining_files; (load_data->error == NULL) && scan; scan = scan->next) {
		AddFile *add_file = scan->data;

		if (g_cancellable_is_cancelled (load_data->cancellable))
			break;

		if (_archive_write_file (save_data->b,
					 save_data,
					 add_file,
					 add_data->follow_links,
					 NULL,
					 load_data->cancellable) == WRITE_ACTION_ABORT)
		{
			break;
		}

		fr_archive_progress_inc_completed_files (load_data->archive, 1);
	}
}


static void
fr_archive_libarchive_add_files (FrArchive           *archive,
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
	AddData *add_data;
	GList   *scan;

	g_return_if_fail (base_dir != NULL);

	add_data = add_data_new ();
	add_data->follow_links = follow_links;

	if (dest_dir != NULL)
		dest_dir = (dest_dir[0] == '/' ? dest_dir + 1 : dest_dir);
	else
		dest_dir = "";

	for (scan = file_list; scan; scan = scan->next) {
		GFile *file = G_FILE (scan->data);
		g_autofree char *relative_pathname = g_file_get_relative_path (base_dir, file);
		g_autofree char *archive_pathname = g_build_filename (dest_dir, relative_pathname, NULL);
		g_hash_table_insert (add_data->files_to_add,
				     g_strdup (archive_pathname),
				     add_file_new (file, archive_pathname));
		add_data->n_files_to_add++;
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
}


/* -- remove -- */


typedef struct {
	GHashTable *files_to_remove;
	gboolean    remove_all_files;
	int         n_files_to_remove;
} RemoveData;


static void
remove_data_free (RemoveData *remove_data)
{
	if (remove_data->files_to_remove != NULL)
		g_hash_table_unref (remove_data->files_to_remove);
	g_free (remove_data);
}


static void
_remove_files_begin (SaveData *save_data,
		     gpointer  user_data)
{
	LoadData   *load_data = LOAD_DATA (save_data);
	RemoveData *remove_data = user_data;
	FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));

	fr_archive_progress_set_total_files (load_data->archive, remove_data->n_files_to_remove);
	fr_archive_progress_set_total_bytes (load_data->archive, private->uncompressed_size);
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

	if (remove_data->remove_all_files)
		return WRITE_ACTION_SKIP_ENTRY;

	action = WRITE_ACTION_WRITE_ENTRY;
	pathname = archive_entry_pathname (w_entry);
	if (g_hash_table_lookup (remove_data->files_to_remove, pathname) != NULL) {
		action = WRITE_ACTION_SKIP_ENTRY;
		remove_data->n_files_to_remove--;
		fr_archive_progress_inc_completed_files (load_data->archive, 1);
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
	remove_data->remove_all_files = (file_list == NULL);
	if (! remove_data->remove_all_files) {
		remove_data->files_to_remove = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		remove_data->n_files_to_remove = 0;
		for (scan = file_list; scan; scan = scan->next) {
			g_hash_table_insert (remove_data->files_to_remove, g_strdup (scan->data), GINT_TO_POINTER (1));
			remove_data->n_files_to_remove++;
		}
	}
	else
		remove_data->n_files_to_remove = archive->files->len;

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
	LoadData   *load_data = LOAD_DATA (save_data);
	RenameData *rename_data = user_data;

	fr_archive_progress_set_total_files (load_data->archive, rename_data->n_files_to_rename);
	FrArchiveLibarchivePrivate *private = fr_archive_libarchive_get_instance_private (FR_ARCHIVE_LIBARCHIVE (load_data->archive));
	fr_archive_progress_set_total_bytes (load_data->archive, private->uncompressed_size);
}


static WriteAction
_rename_files_entry_action (SaveData             *save_data,
			    struct archive_entry *w_entry,
			    gpointer              user_data)
{
	LoadData    *load_data = LOAD_DATA (save_data);
	RenameData  *rename_data = user_data;
	WriteAction  action;
	const char  *pathname;
	char        *new_pathname;

	action = WRITE_ACTION_WRITE_ENTRY;
	pathname = archive_entry_pathname (w_entry);
	new_pathname = g_hash_table_lookup (rename_data->files_to_rename, pathname);
	if (new_pathname != NULL) {
		archive_entry_set_pathname (w_entry, new_pathname);
		rename_data->n_files_to_rename--;
		g_hash_table_remove (rename_data->files_to_rename, pathname);
		fr_archive_progress_inc_completed_files (load_data->archive, 1);
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

	rename_data = g_new0 (RenameData, 1);
	rename_data->files_to_rename = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	rename_data->n_files_to_rename = 0;

	if (is_dir) {
		g_autofree char *old_dirname = g_build_filename (current_dir + 1, old_name, "/", NULL);
		int old_dirname_len = strlen (old_dirname);
		g_autofree char *new_dirname = g_build_filename (current_dir + 1, new_name, "/", NULL);
		GList *scan;

		for (scan = file_list; scan; scan = scan->next) {
			char *old_pathname = scan->data;
			char *new_pathname = g_build_filename (new_dirname, old_pathname + old_dirname_len, NULL);
			g_hash_table_insert (rename_data->files_to_rename, g_strdup (old_pathname), new_pathname);
			rename_data->n_files_to_rename++;
		}
	}
	else {
		char *old_pathname = (char *) file_list->data;
		char *new_pathname;

		new_pathname = g_build_filename (current_dir + 1, new_name, NULL);
		g_hash_table_insert (rename_data->files_to_rename,
				     g_strdup (old_pathname),
				     new_pathname);
		rename_data->n_files_to_rename = 1;
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
}


/* -- fr_archive_libarchive_paste_clipboard -- */


static void
fr_archive_libarchive_paste_clipboard (FrArchive           *archive,
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
	AddData *add_data;
	GList   *scan;

	g_return_if_fail (base_dir != NULL);

	add_data = add_data_new ();

	current_dir = current_dir + 1;
	for (scan = files; scan; scan = scan->next) {
		const char *old_name = (char *) scan->data;
		char       *new_name;
		g_autoptr (GFile) file = NULL;

		new_name = g_build_filename (current_dir, old_name + strlen (base_dir) - 1, NULL);
		file = _g_file_append_path (tmp_dir, old_name, NULL);
		g_hash_table_insert (add_data->files_to_add, new_name, add_file_new (file, new_name));
		add_data->n_files_to_add++;
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
	AddData *add_data;
	GList   *scan;

	add_data = add_data_new ();

	if (dest_dir[0] == '/')
		dest_dir += 1;

	for (scan = file_list; scan; scan = scan->next) {
		GFile *file = G_FILE (scan->data);
		g_autofree char *basename = g_file_get_basename (file);
		g_autofree char *archive_pathname = g_build_filename (dest_dir, basename, NULL);

		g_hash_table_insert (add_data->files_to_add,
				     g_strdup (archive_pathname),
				     add_file_new (file, archive_pathname));
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
				     				fr_archive_add_dropped_items),
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
		GFile *temp_dir = G_FILE (scan_dir->data);
		GFile *extracted_file = G_FILE (scan_file->data);
		g_autofree char *archive_pathname = g_file_get_relative_path (temp_dir, extracted_file);
		g_hash_table_insert (add_data->files_to_add, g_strdup (archive_pathname), add_file_new (extracted_file, archive_pathname));
		add_data->n_files_to_add++;
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
				     				fr_archive_update_open_files),
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

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_archive_libarchive_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types = fr_archive_libarchive_get_mime_types;
	archive_class->get_capabilities = fr_archive_libarchive_get_capabilities;
	archive_class->get_packages = fr_archive_libarchive_get_packages;
	archive_class->list = fr_archive_libarchive_list;
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

	base->propAddCanReplace = TRUE;
	base->propAddCanUpdate = TRUE;
	base->propAddCanStoreFolders = TRUE;
	base->propAddCanStoreLinks = TRUE;
	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder = TRUE;
	base->propExtractCanJunkPaths = TRUE;
	base->propCanExtractAll = TRUE;
	base->propCanDeleteNonEmptyFolders = TRUE;
	base->propCanExtractNonEmptyFolders = TRUE;
}
