/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <gio/gio.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-init.h"


#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#endif

#define BUF_SIZE 4096
#define FILE_PREFIX    "file://"
#define FILE_PREFIX_L  7
#define SPECIAL_DIR(x) ((strcmp ((x), "..") == 0) || (strcmp ((x), ".") == 0))


gboolean
_g_uri_query_exists (const char *uri)
{
	GFile     *file;
	gboolean   exists;

	if (uri == NULL)
		return FALSE;

	file = g_file_new_for_uri (uri);
	exists = g_file_query_exists (file, NULL);
	g_object_unref (file);

	return exists;
}


static gboolean
_g_uri_is_filetype (const char *uri,
		    GFileType   file_type)
{
	gboolean   result = FALSE;
	GFile     *file;
	GFileInfo *info;
	GError    *error = NULL;

	file = g_file_new_for_uri (uri);

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return FALSE;
	}

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, &error);
	if (error == NULL) {
		result = (g_file_info_get_file_type (info) == file_type);
	}
	else {
		g_warning ("Failed to get file type for uri %s: %s", uri, error->message);
		g_error_free (error);
	}

	g_object_unref (info);
	g_object_unref (file);

	return result;
}


gboolean
_g_uri_query_is_file (const char *uri)
{
	return _g_uri_is_filetype (uri, G_FILE_TYPE_REGULAR);
}


gboolean
_g_uri_query_is_dir (const char *uri)
{
	return _g_uri_is_filetype (uri, G_FILE_TYPE_DIRECTORY);
}


char *
_g_uri_create_alternative (const char *folder,
			   const char *name)
{
	char *new_uri = NULL;
	int   n = 1;

	do {
		g_free (new_uri);
		if (n == 1)
			new_uri = g_strconcat (folder, "/", name, NULL);
		else
			new_uri = g_strdup_printf ("%s/%s%%20(%d)", folder, name, n);
		n++;
	}
	while (_g_uri_query_exists (new_uri));

	return new_uri;
}


char *
_g_uri_create_alternative_for_uri (const char *uri)
{
	char *base_uri;
	char *new_uri;

	base_uri = _g_path_remove_level (uri);
	new_uri = _g_uri_create_alternative (base_uri, _g_path_get_file_name (uri));
	g_free (base_uri);

	return new_uri;
}


gboolean
_g_uri_query_dir_is_empty (const char *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *error = NULL;
	int              n = 0;

	file = g_file_new_for_uri (uri);

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return TRUE;
	}

	file_enum = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &error);
	if (error != NULL) {
		g_warning ("Failed to enumerate children of %s: %s", uri, error->message);
		g_error_free (error);
		g_object_unref (file_enum);
		g_object_unref (file);
		return TRUE;
	}

	while ((n == 0) && ((info = g_file_enumerator_next_file (file_enum, NULL, &error)) != NULL)) {
		if (error != NULL) {
			g_warning ("Encountered error while enumerating children of %s (ignoring): %s", uri, error->message);
			g_error_free (error);
		}
		else if (! SPECIAL_DIR (g_file_info_get_name (info)))
			n++;
		g_object_unref (info);
	}

	g_object_unref (file);
	g_object_unref (file_enum);

	return (n == 0);
}


gboolean
_g_uri_dir_contains_one_object (const char *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *err = NULL;
	int              n = 0;

	file = g_file_new_for_uri (uri);

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return FALSE;
	}

	file_enum = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &err);
	if (err != NULL) {
		g_warning ("Failed to enumerate children of %s: %s", uri, err->message);
		g_error_free (err);
		g_object_unref (file_enum);
		g_object_unref (file);
		return FALSE;
	}

	while ((info = g_file_enumerator_next_file (file_enum, NULL, &err)) != NULL) {
		const char *name;

		if (err != NULL) {
			g_warning ("Encountered error while enumerating children of %s, ignoring: %s", uri, err->message);
			g_error_free (err);
			g_object_unref (info);
			continue;
		}

		name = g_file_info_get_name (info);
		if (strcmp (name, ".") == 0 || strcmp (name, "..") == 0) {
			g_object_unref (info);
 			continue;
		}

		g_object_unref (info);

		if (++n > 1)
			break;
	}

	g_object_unref (file);
	g_object_unref (file_enum);

	return (n == 1);
}


char *
_g_uri_get_dir_content_if_unique (const char  *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *err = NULL;
	char            *content_uri = NULL;

	file = g_file_new_for_uri (uri);

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return NULL;
	}

	file_enum = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &err);
	if (err != NULL) {
		g_warning ("Failed to enumerate children of %s: %s", uri, err->message);
		g_error_free (err);
		return NULL;
	}

	while ((info = g_file_enumerator_next_file (file_enum, NULL, &err)) != NULL) {
		const char *name;

		if (err != NULL) {
			g_warning ("Failed to get info while enumerating children: %s", err->message);
			g_clear_error (&err);
			g_object_unref (info);
			continue;
		}

		name = g_file_info_get_name (info);
		if ((strcmp (name, ".") == 0) || (strcmp (name, "..") == 0)) {
			g_object_unref (info);
			continue;
		}

		if (content_uri != NULL) {
			g_free (content_uri);
			g_object_unref (info);
			content_uri = NULL;
			break;
		}

		content_uri = _g_uri_build (uri, name, NULL);
		g_object_unref (info);
	}

	if (err != NULL) {
		g_warning ("Failed to get info after enumerating children: %s", err->message);
		g_clear_error (&err);
	}

	g_object_unref (file_enum);
	g_object_unref (file);

	return content_uri;
}


goffset
_g_uri_get_file_size (const char *uri)
{
	goffset    size = 0;
	GFile     *file;
	GFileInfo *info;
	GError    *err = NULL;

	if ((uri == NULL) || (*uri == '\0'))
		return 0;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, &err);
	if (err == NULL) {
		size = g_file_info_get_size (info);
	}
	else {
		g_warning ("Failed to get file size for %s: %s", uri, err->message);
		g_error_free (err);
	}

	g_object_unref (info);
	g_object_unref (file);

	return size;
}


static time_t
_g_uri_get_file_time_type (const char *uri,
			   const char *type)
{
	time_t     result = 0;
	GFile     *file;
	GFileInfo *info;
	GError    *err = NULL;

	if ((uri == NULL) || (*uri == '\0'))
 		return 0;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, type, 0, NULL, &err);
	if (err == NULL) {
		result = (time_t) g_file_info_get_attribute_uint64 (info, type);
	}
	else {
		g_warning ("Failed to get %s: %s", type, err->message);
		g_error_free (err);
		result = 0;
	}

	g_object_unref (info);
	g_object_unref (file);

	return result;
}


time_t
_g_uri_get_file_mtime (const char *uri)
{
	return _g_uri_get_file_time_type (uri, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}


time_t
_g_uri_get_file_ctime (const char *uri)
{
	return _g_uri_get_file_time_type (uri, G_FILE_ATTRIBUTE_TIME_CREATED);
}


gboolean
_g_uri_ensure_dir_exists (const char  *uri,
			  mode_t       mode,
			  GError     **error)
{
	GFile  *dir;
	GError *priv_error = NULL;

	if (uri == NULL)
		return FALSE;

	if (error == NULL)
		error = &priv_error;

	dir = g_file_new_for_uri (uri);
	if (! _g_file_make_directory_tree (dir, mode, error)) {
		g_warning ("could create directory %s: %s", uri, (*error)->message);
		if (priv_error != NULL)
			g_clear_error (&priv_error);
		return FALSE;
	}

	return TRUE;
}


const char *
_g_uri_get_mime_type (const char *uri,
                      gboolean    fast_file_type)
{
	GFile      *file;
	GFileInfo  *info;
	GError     *err = NULL;
 	const char *result = NULL;

 	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file,
				  fast_file_type ?
				  G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE :
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  0, NULL, &err);
	if (info == NULL) {
		g_warning ("could not get content type for %s: %s", uri, err->message);
		g_clear_error (&err);
	}
	else {
		result = _g_str_get_static (g_file_info_get_content_type (info));
		g_object_unref (info);
	}

	g_object_unref (file);

	return result;
}


/* -- _g_uri_remove_directory -- */


static gboolean
delete_directory_recursive (GFile   *dir,
			    GError **error)
{
	char            *uri;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	gboolean         error_occurred = FALSE;

	if (error != NULL)
		*error = NULL;

	file_enum = g_file_enumerate_children (dir,
					       G_FILE_ATTRIBUTE_STANDARD_NAME ","
					       G_FILE_ATTRIBUTE_STANDARD_TYPE,
					       0, NULL, error);

	uri = g_file_get_uri (dir);
	while (! error_occurred && (info = g_file_enumerator_next_file (file_enum, NULL, error)) != NULL) {
		char  *child_uri;
		GFile *child;

		child_uri = _g_uri_build (uri, g_file_info_get_name (info), NULL);
		child = g_file_new_for_uri (child_uri);

		switch (g_file_info_get_file_type (info)) {
		case G_FILE_TYPE_DIRECTORY:
			if (! delete_directory_recursive (child, error))
				error_occurred = TRUE;
			break;
		default:
			if (! g_file_delete (child, NULL, error))
				error_occurred = TRUE;
			break;
		}

		g_object_unref (child);
		g_free (child_uri);
		g_object_unref (info);
	}
	g_free (uri);

	if (! error_occurred && ! g_file_delete (dir, NULL, error))
 		error_occurred = TRUE;

	g_object_unref (file_enum);

	return ! error_occurred;
}


gboolean
_g_uri_remove_directory (const char *uri)
{
	GFile     *dir;
	gboolean   result;
	GError    *error = NULL;

	dir = g_file_new_for_uri (uri);
	result = delete_directory_recursive (dir, &error);
	if (! result) {
		g_warning ("Cannot delete %s: %s", uri, error->message);
		g_clear_error (&error);
	}
	g_object_unref (dir);

	return result;
}


gboolean
_g_uri_check_permissions (const char *uri,
		   int         mode)
{
	GFile    *file;
	gboolean  result;

	file = g_file_new_for_uri (uri);
	result = _g_file_check_permissions (file, mode);

	g_object_unref (file);

	return result;
}


/* path */


gboolean
_g_path_query_is_dir (const char *path)
{
	char     *uri;
	gboolean  result;

	uri = g_filename_to_uri (path, NULL, NULL);
	result = _g_uri_query_is_dir (uri);
	g_free (uri);

	return result;
}


goffset
_g_path_get_file_size (const char *path)
{
	char    *uri;
	goffset  result;

	uri = g_filename_to_uri (path, NULL, NULL);
	result = _g_uri_get_file_size (uri);
	g_free (uri);

	return result;
}


time_t
_g_path_get_file_mtime (const char *path)
{
	char   *uri;
	time_t  result;

	uri = g_filename_to_uri (path, NULL, NULL);
	result = _g_uri_get_file_mtime (uri);
	g_free (uri);

	return result;
}


gboolean
_g_path_make_directory_tree (const char  *path,
		   	     mode_t       mode,
		   	     GError     **error)
{
	char     *uri;
	gboolean  result;

	uri = g_filename_to_uri (path, NULL, NULL);
	result = _g_uri_ensure_dir_exists (uri, mode, error);
	g_free (uri);

	return result;
}


const char*
_g_path_get_mime_type (const char *filename,
                       gboolean    fast_file_type)
{
	char       *uri;
	const char *mime_type;

	uri = g_filename_to_uri (filename, NULL, NULL);
	mime_type = _g_uri_get_mime_type (uri, fast_file_type);
	g_free (uri);

	return mime_type;
}


guint64
_g_path_get_free_space (const char *path)
{
	guint64    freespace = 0;
	GFile     *file;
	GFileInfo *info;
	GError    *err = NULL;

	file = g_file_new_for_path (path);
	info = g_file_query_filesystem_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, &err);
	if (info != NULL) {
		freespace = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
		g_object_unref (info);
	}
	else {
		g_warning ("Could not get filesystem free space on volume that contains %s: %s", path, err->message);
		g_error_free (err);
	}
	g_object_unref (file);

	return freespace;
}


gboolean
_g_path_remove_directory (const char *path)
{
	char     *uri;
	gboolean  result;

	if (path == NULL)
		return TRUE;

	uri = g_filename_to_uri (path, NULL, NULL);
	result = _g_uri_remove_directory (uri);
	g_free (uri);

	return result;
}


static const char *try_folder[] = { "cache", "~", "tmp", NULL };


static char *
ith_temp_folder_to_try (int n)
{
	const char *folder;

	folder = try_folder[n];
	if (strcmp (folder, "cache") == 0)
		folder = g_get_user_cache_dir ();
	else if (strcmp (folder, "~") == 0)
		folder = g_get_home_dir ();
	else if (strcmp (folder, "tmp") == 0)
		folder = g_get_tmp_dir ();

	return g_strdup (folder);
}


char *
_g_path_get_temp_work_dir (const char *parent_folder)
{
	guint64  max_size = 0;
	char    *best_folder = NULL;
	int      i;
	char    *template;
	char    *result = NULL;

	if (parent_folder == NULL) {
		/* find the folder with more free space. */

		for (i = 0; try_folder[i] != NULL; i++) {
			char    *folder;
			guint64  size;

			folder = ith_temp_folder_to_try (i);
			size = _g_path_get_free_space (folder);
			if (max_size < size) {
				max_size = size;
				g_free (best_folder);
				best_folder = folder;
			}
			else
				g_free (folder);
		}
	}
	else
		best_folder = g_strdup (parent_folder);

	if (best_folder == NULL)
		return NULL;

	template = g_strconcat (best_folder, "/.fr-XXXXXX", NULL);
	result = mkdtemp (template);

	if ((result == NULL) || (*result == '\0')) {
		g_free (template);
		result = NULL;
	}

	return result;
}


gboolean
_g_path_is_temp_work_dir (const char *path)
{
	int i;

	if (strncmp (path, "file://", 7) == 0)
		path = path + 7;
	else if (path[0] != '/')
		return FALSE;

	for (i = 0; try_folder[i] != NULL; i++) {
		char *folder;

		folder = ith_temp_folder_to_try (i);
		if (strncmp (path, folder, strlen (folder)) == 0) {
			if (strncmp (path + strlen (folder), "/.fr-", 5) == 0) {
				g_free (folder);
				return TRUE;
			}
		}

		g_free (folder);
	}

	return FALSE;
}


gboolean
_g_path_is_temp_dir (const char *path)
{
	if (strncmp (path, "file://", 7) == 0)
		path = path + 7;
	if (strcmp (g_get_tmp_dir (), path) == 0)
		return TRUE;
	if (_g_path_is_parent_of (g_get_tmp_dir (), path))
		return TRUE;
	else
		return _g_path_is_temp_work_dir (path);
}


/* GFile */


gboolean
_g_file_check_permissions (GFile *file,
			   int    mode)
{
	gboolean   result = TRUE;
	GFileInfo *info;
	GError    *err = NULL;
	gboolean   default_permission_when_unknown = TRUE;

	info = g_file_query_info (file, "access::*", 0, NULL, &err);
	if (err != NULL) {
		g_clear_error (&err);
		result = FALSE;
	}
	else {
		if ((mode & R_OK) == R_OK) {
			if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ))
				result = (result && g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ));
			else
				result = (result && default_permission_when_unknown);
		}

		if ((mode & W_OK) == W_OK) {
			if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE))
				result = (result && g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE));
			else
				result = (result && default_permission_when_unknown);
		}

		if ((mode & X_OK) == X_OK) {
			if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE))
				result = (result && g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE));
			else
				result = (result && default_permission_when_unknown);
		}

		g_object_unref (info);
	}

	return result;
}


gboolean
_g_file_make_directory_tree (GFile    *dir,
			     mode_t    mode,
			     GError  **error)
{
	gboolean  success = TRUE;
	GFile    *parent;

	if ((dir == NULL) || g_file_query_exists (dir, NULL))
		return TRUE;

	parent = g_file_get_parent (dir);
	if (parent != NULL) {
		success = _g_file_make_directory_tree (parent, mode, error);
		g_object_unref (parent);
		if (! success)
			return FALSE;
	}

	success = g_file_make_directory (dir, NULL, error);
	if ((error != NULL) && (*error != NULL) && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
		g_clear_error (error);
		success = TRUE;
	}

	if (success)
		g_file_set_attribute_uint32 (dir,
					     G_FILE_ATTRIBUTE_UNIX_MODE,
					     mode,
					     0,
					     NULL,
					     NULL);

	return success;
}


GFile *
_g_file_new_user_config_subdir (const char *child_name,
			        gboolean    create_child)
{
	char   *full_path;
	GFile  *file;
	GError *error = NULL;

	full_path = g_strconcat (g_get_user_config_dir (), "/", child_name, NULL);
	file = g_file_new_for_path (full_path);
	g_free (full_path);

	if  (create_child && ! _g_file_make_directory_tree (file, 0700, &error)) {
		g_warning ("%s", error->message);
		g_error_free (error);
		g_object_unref (file);
		file = NULL;
	}

	return file;
}


/* program */


gboolean
_g_program_is_in_path (const char *filename)
{
	char *str;
	char *value;
	int   result = FALSE;

	value = g_hash_table_lookup (ProgramsCache, filename);
	if (value != NULL) {
		result = (strcmp (value, "1") == 0);
		return result;
	}

	str = g_find_program_in_path (filename);
	if (str != NULL) {
		g_free (str);
		result = TRUE;
	}

	g_hash_table_insert (ProgramsCache,
			     g_strdup (filename),
			     result ? "1" : "0");

	return result;
}


gboolean
_g_program_is_available (const char *filename,
		         gboolean    check)
{
	return ! check || _g_program_is_in_path (filename);
}


/* GKeyFile */


void
_g_key_file_save (GKeyFile *key_file,
	          GFile    *file)
{
	char   *file_data;
	gsize   size;
	GError *error = NULL;

	file_data = g_key_file_to_data (key_file, &size, &error);
	if (error != NULL) {
		g_warning ("Could not save options: %s\n", error->message);
		g_clear_error (&error);
	}
	else {
		GFileOutputStream *stream;

		stream = g_file_replace (file, NULL, FALSE, 0, NULL, &error);
		if (stream == NULL) {
			g_warning ("Could not save options: %s\n", error->message);
			g_clear_error (&error);
		}
		else if (! g_output_stream_write_all (G_OUTPUT_STREAM (stream), file_data, size, NULL, NULL, &error)) {
			g_warning ("Could not save options: %s\n", error->message);
			g_clear_error (&error);
		}
		else if (! g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, &error)) {
			g_warning ("Could not save options: %s\n", error->message);
			g_clear_error (&error);
		}

		g_object_unref (stream);
	}

	g_free (file_data);
}
