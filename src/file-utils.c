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


#define BUF_SIZE 4096
#define FILE_PREFIX    "file://"
#define FILE_PREFIX_L  7
#define IS_SPECIAL_DIR(x) ((strcmp ((x), "..") == 0) || (strcmp ((x), ".") == 0))


/* path */


static const char *try_folder[] = { "cache", "~", "tmp", NULL };


static const char *
get_nth_temp_folder_to_try (int n)
{
        const char *folder;

        folder = try_folder[n];
        if (strcmp (folder, "cache") == 0)
                folder = g_get_user_cache_dir ();
        else if (strcmp (folder, "~") == 0)
                folder = g_get_home_dir ();
        else if (strcmp (folder, "tmp") == 0)
                folder = g_get_tmp_dir ();

        return folder;
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
                        const char *folder;
                        GFile      *file;
                        guint64     size;

                        folder = get_nth_temp_folder_to_try (i);
                        file = g_file_new_for_path (folder);
                        size = _g_file_get_free_space (file);
                        g_object_unref (file);

                        if (max_size < size) {
                                max_size = size;
                                g_free (best_folder);
                                best_folder = g_strdup (folder);
                        }
                }
        }
        else
                best_folder = g_strdup (parent_folder);

        if (best_folder == NULL)
                return NULL;

        template = g_strconcat (best_folder, "/.fr-XXXXXX", NULL);
        result = g_mkdtemp (template);
        g_free (best_folder);

        if ((result == NULL) || (*result == '\0')) {
                g_free (template);
                result = NULL;
        }

        return result;
}


/* GFile */


static gboolean
_g_file_is_filetype (GFile     *file,
		     GFileType  file_type)
{
	gboolean   result = FALSE;
	GFileInfo *info;

	if (! g_file_query_exists (file, NULL))
		return FALSE;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, NULL);
	if (info != NULL) {
		result = (g_file_info_get_file_type (info) == file_type);
		g_object_unref (info);
	}

	return result;
}


gboolean
_g_file_query_is_file (GFile *file)
{
	return _g_file_is_filetype (file, G_FILE_TYPE_REGULAR);
}


gboolean
_g_file_query_is_dir (GFile *file)
{
	return _g_file_is_filetype (file, G_FILE_TYPE_DIRECTORY);
}


static time_t
_g_file_get_file_time_type (GFile      *file,
			    const char *type)
{
	time_t     result = 0;
	GFileInfo *info;
	GError    *err = NULL;

	if (file == NULL)
 		return 0;

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

	return result;
}


goffset
_g_file_get_file_size (GFile *file)
{
	goffset    size = 0;
	GFileInfo *info;
	GError    *error = NULL;

	if (file == NULL)
		return 0;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, &error);
	if (info != NULL) {
		size = g_file_info_get_size (info);
		g_object_unref (info);
	}
	else {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	return size;
}


time_t
_g_file_get_file_mtime (GFile *file)
{
	return _g_file_get_file_time_type (file, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}


time_t
_g_file_get_file_ctime (GFile *file)
{
	return _g_file_get_file_time_type (file, G_FILE_ATTRIBUTE_TIME_CREATED);
}


const char *
_g_file_get_mime_type (GFile    *file,
                       gboolean  fast_file_type)
{
	GFileInfo  *info;
	GError     *error = NULL;
 	const char *result = NULL;

 	info = g_file_query_info (file,
				  (fast_file_type ?
				   G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE :
				   G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE),
				  0,
				  NULL,
				  &error);
	if (info == NULL) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}
	else {
		result = _g_str_get_static (g_file_info_get_content_type (info));
		g_object_unref (info);
	}

	return result;
}


gboolean
_g_file_is_temp_dir (GFile *file)
{
	gboolean  result = FALSE;
	char     *path;

	path = g_file_get_path (file);
	if (path == NULL)
		result = TRUE;
	else if (strcmp (g_get_tmp_dir (), path) == 0)
		result = TRUE;
	else if (_g_path_is_parent_of (g_get_tmp_dir (), path))
		result = TRUE;
	else
		result = _g_file_is_temp_work_dir (file);

	g_free (path);

	return result;
}


GFile *
_g_file_create_alternative (GFile      *folder,
			    const char *name)
{
	GFile *file = NULL;
	int    n = 1;

	do {
		char *new_name;

		_g_object_unref (file);

		if (n == 1)
			new_name = g_strdup (name);
		else
			new_name = g_strdup_printf ("%s (%d)", name, n);
		n++;

		file = g_file_get_child (folder, new_name);

		g_free (new_name);
	}
	while (g_file_query_exists (file, NULL));

	return file;
}


GFile *
_g_file_create_alternative_for_file (GFile *file)
{
	GFile *parent;
	char  *name;
	GFile *new_file;

	parent = g_file_get_parent (file);
	name = g_file_get_basename (file);
	new_file = _g_file_create_alternative (parent, name);

	g_free (name);
	g_object_unref (parent);

	return new_file;
}


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


gboolean
_g_file_remove_directory (GFile         *directory,
			  GCancellable  *cancellable,
			  GError       **error)
{
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	gboolean         error_occurred = FALSE;

	if (directory == NULL)
		return TRUE;

	enumerator = g_file_enumerate_children (directory,
					        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
						G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					        cancellable,
					        error);

	while (! error_occurred && (info = g_file_enumerator_next_file (enumerator, cancellable, error)) != NULL) {
		GFile *child;

		child = g_file_get_child (directory, g_file_info_get_name (info));
		switch (g_file_info_get_file_type (info)) {
		case G_FILE_TYPE_DIRECTORY:
			if (! _g_file_remove_directory (child, cancellable, error))
				error_occurred = TRUE;
			break;
		default:
			if (! g_file_delete (child, cancellable, error))
				error_occurred = TRUE;
			break;
		}

		g_object_unref (child);
		g_object_unref (info);
	}

	if (! error_occurred && ! g_file_delete (directory, cancellable, error))
 		error_occurred = TRUE;

	g_object_unref (enumerator);

	return ! error_occurred;
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


GFile *
_g_file_get_dir_content_if_unique (GFile *file)
{
	GFileEnumerator *enumarator;
	GFileInfo       *info;
	GError          *error = NULL;
	GFile           *content = NULL;

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return NULL;
	}

	enumarator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
		return NULL;
	}

	while ((info = g_file_enumerator_next_file (enumarator, NULL, &error)) != NULL) {
		const char *name;

		if (error != NULL) {
			g_warning ("Failed to get info while enumerating children: %s", error->message);
			g_clear_error (&error);
			g_object_unref (info);
			continue;
		}

		name = g_file_info_get_name (info);
		if ((strcmp (name, ".") == 0) || (strcmp (name, "..") == 0)) {
			g_object_unref (info);
			continue;
		}

		if (content != NULL) {
			g_object_unref (content);
			g_object_unref (info);
			content = NULL;
			break;
		}

		content = g_file_get_child (file, name);

		g_object_unref (info);
	}

	if (error != NULL) {
		g_warning ("Failed to get info after enumerating children: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (enumarator);

	return content;
}


guint64
_g_file_get_free_space (GFile *file)
{
	GFileInfo *info;
	guint64    freespace = 0;
	GError    *error = NULL;

	info = g_file_query_filesystem_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, &error);
	if (info != NULL) {
		freespace = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
		g_object_unref (info);
	}
	else {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	return freespace;
}


GFile *
_g_file_get_temp_work_dir (GFile *parent_folder)
{
	char  *parent_path;
	char  *tmp;
	GFile *file;

	parent_path = (parent_folder != NULL) ? g_file_get_path (parent_folder) : NULL;
	tmp = _g_path_get_temp_work_dir (parent_path);
	file = g_file_new_for_path (tmp);

	g_free (tmp);
	g_free (parent_path);

	return file;
}


gboolean
_g_file_is_temp_work_dir (GFile *file)
{
	gboolean  result = FALSE;
	char     *path;
	int       i;

	path = g_file_get_path (file);
	if (path[0] != '/') {
		g_free (path);
		return FALSE;
	}

	for (i = 0; try_folder[i] != NULL; i++) {
		const char *folder;

		folder = get_nth_temp_folder_to_try (i);
		if (strncmp (path, folder, strlen (folder)) == 0) {
			if (strncmp (path + strlen (folder), "/.fr-", 5) == 0) {
				result = TRUE;
				break;
			}
		}
	}

	g_free (path);

	return result;
}


gboolean
_g_file_query_dir_is_empty (GFile *file)
{
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GError          *error = NULL;
	int              n = 0;

	if (! g_file_query_exists (file, NULL))
		return TRUE;

	enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
		g_object_unref (enumerator);
		return TRUE;
	}

	while ((n == 0) && ((info = g_file_enumerator_next_file (enumerator, NULL, &error)) != NULL)) {
		if (error != NULL) {
			g_warning ("%s", error->message);
			g_error_free (error);
		}
		else if (! IS_SPECIAL_DIR (g_file_info_get_name (info)))
			n++;
		g_object_unref (info);
	}

	g_object_unref (enumerator);

	return (n == 0);
}


gboolean
_g_file_dir_contains_one_object (GFile *file)
{
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GError          *error = NULL;
	int              n = 0;

	if (! g_file_query_exists (file, NULL))
		return FALSE;

	enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
		g_object_unref (enumerator);
		return FALSE;
	}

	while ((info = g_file_enumerator_next_file (enumerator, NULL, &error)) != NULL) {
		const char *name;

		if (error != NULL) {
			g_warning ("%s", error->message);
			g_error_free (error);
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

	g_object_unref (enumerator);

	return (n == 1);
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
