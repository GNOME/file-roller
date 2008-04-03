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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
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
#include <gconf/gconf-client.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "main.h"


#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#endif

#define BUF_SIZE 4096
#define FILE_PREFIX    "file://"
#define FILE_PREFIX_L  7
#define SPECIAL_DIR(x) ((strcmp ((x), "..") == 0) || (strcmp ((x), ".") == 0))


gboolean
uri_exists (const char *uri)
{
	GFile     *file;
	GFileInfo *info;
	GError    *err = NULL;
	gboolean   exists;

	if (uri == NULL) 
		return FALSE;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file,	G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &err);
	
	exists = (err != NULL);
	
	if (err != NULL)
		g_error_free (err);
	g_object_unref (info);
	g_object_unref (file);

	return exists;	
}


static gboolean
uri_is_filetype (const char *uri, 
		 GFileType   file_type)
{
	gboolean   result = FALSE;
	GFile     *file;
	GFileInfo *info;
	GError    *error = NULL;
	
	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, &error);
	if (error == NULL) {
		result = (g_file_info_get_file_type (info) == file_type);
	}
	else {
		g_warning ("Failed to get file type for uri %s: %s", uri, err->message);
		g_error_free (error);
	}
	
	g_object_unref (info);
	g_object_unref (file);
	
	return result;
}


gboolean
path_is_file (const char *uri)
{
	return uri_is_filetype (uri, G_FILE_TYPE_REGULAR);
}


gboolean
path_is_dir (const char *uri)
{
	return uri_is_filetype (uri, G_FILE_TYPE_DIRECTORY);
}


gboolean
dir_is_empty (const char *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *error = NULL;
	int              n = 0;

	file = g_file_new_for_uri (uri);
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
dir_contains_one_object (const char *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *err = NULL;
	int              n = 0;

	file = g_file_new_for_uri (uri);
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
get_directory_content_if_unique (const char  *uri)
{
	GFile           *file;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *err = NULL;
	char            *content_uri = NULL;
	
	file = g_file_new_for_uri (uri);
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
		
		content_uri = g_build_path (uri, name, NULL);
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


/* Check whether the dirname is contained in filename */
gboolean
path_in_path (const char *dirname,
	      const char *filename)
{
	int dirname_l, filename_l, separator_position;

	if ((dirname == NULL) || (filename == NULL))
		return FALSE;

	dirname_l = strlen (dirname);
	filename_l = strlen (filename);

	if ((dirname_l == filename_l + 1)
	     && (dirname[dirname_l - 1] == '/'))
		return FALSE;

	if ((filename_l == dirname_l + 1)
	     && (filename[filename_l - 1] == '/'))
		return FALSE;

	if (dirname[dirname_l - 1] == '/')
		separator_position = dirname_l - 1;
	else
		separator_position = dirname_l;

	return ((filename_l > dirname_l)
		&& (strncmp (dirname, filename, dirname_l) == 0)
		&& (filename[separator_position] == '/'));
}


goffset
get_file_size (const char *uri)
{
	GFile     *file;
	GFileInfo *info;
	goffset    size;
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
}


static time_t
get_file_time_type (const char *uri, 
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
		g_warning ("Failed to get %s for %s: %s", type, uri, err->message);
		g_error_free (err);
		result = 0;		
	}		
	
	g_object_unref (info);
	g_object_unref (file);
	
	return result;
}


time_t
get_file_mtime (const char *uri)
{
	return get_file_time_type (uri, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}


time_t
get_file_ctime (const char *uri)
{
	return get_file_time_type (uri, G_FILE_ATTRIBUTE_TIME_CREATED);
}


gboolean
file_is_hidden (const gchar *name)
{
	if (name[0] != '.') return FALSE;
	if (name[1] == '\0') return FALSE;
	if ((name[1] == '.') && (name[2] == '\0')) return FALSE;

	return TRUE;
}


/* like g_basename but does not warns about NULL and does not
 * alloc a new string. */
G_CONST_RETURN gchar *
file_name_from_path (const gchar *file_name)
{
	register char   *base;
	register gssize  last_char;

	if (file_name == NULL)
		return NULL;

	if (file_name[0] == '\0')
		return "";

	last_char = strlen (file_name) - 1;

	if (file_name [last_char] == G_DIR_SEPARATOR)
		return "";

	base = g_utf8_strrchr (file_name, -1, G_DIR_SEPARATOR);
	if (! base)
		return file_name;

	return base + 1;
}


char *
dir_name_from_path (const gchar *path)
{
	register gssize base;
	register gssize last_char;

	if (path == NULL)
		return NULL;

	if (path[0] == '\0')
		return g_strdup ("");

	last_char = strlen (path) - 1;
	if (path[last_char] == G_DIR_SEPARATOR)
		last_char--;

	base = last_char;
	while ((base >= 0) && (path[base] != G_DIR_SEPARATOR))
		base--;

	return g_strndup (path + base + 1, last_char - base);
}


gchar *
remove_level_from_path (const gchar *path)
{
	int         p;
	const char *ptr = path;
	char       *new_path;

	if (path == NULL)
		return NULL;

	p = strlen (path) - 1;
	if (p < 0)
		return NULL;

	while ((p > 0) && (ptr[p] != '/'))
		p--;
	if ((p == 0) && (ptr[p] == '/'))
		p++;
	new_path = g_strndup (path, (guint)p);

	return new_path;
}


gchar *
remove_extension_from_path (const gchar *path)
{
	int         len;
	int         p;
	const char *ptr = path;
	char       *new_path;

	if (! path)
		return NULL;

	len = strlen (path);
	if (len == 1)
		return g_strdup (path);

	p = len - 1;
	while ((p > 0) && (ptr[p] != '.'))
		p--;
	if (p == 0)
		p = len;
	new_path = g_strndup (path, (guint) p);

	return new_path;
}


char *
remove_ending_separator (const char *path)
{
	gint len, copy_len;

	if (path == NULL)
		return NULL;

	copy_len = len = strlen (path);
	if ((len > 1) && (path[len - 1] == '/'))
		copy_len--;

	return g_strndup (path, copy_len);
}


static gboolean
make_directory_tree (GFile    *dir,
		     mode_t    mode,
		     GError  **error)
{
	GFile *parent;

	if (dir == NULL) 
		return FALSE;

	parent = g_file_get_parent (dir);
	if (parent != NULL) {
		ensure_dir_exists (parent, mode, error);
		if (error != NULL) {
			g_object_unref (parent);
			return FALSE;
		}
	}
	g_object_unref (parent);
	
	g_file_make_directory (dir, NULL, error);
	if (error != NULL)	
		return FALSE;

	g_file_set_attribute_uint32 (dir,
				     G_FILE_ATTRIBUTE_UNIX_MODE,
				     mode, 
				     0, 
				     NULL, 
				     error);
	return TRUE;
}


gboolean
ensure_dir_exists (const char  *uri,
		   mode_t       mode.
		   GError     **error)
{
	GFile  *dir;
	GError *priv_error = NULL;
	
	if (uri == NULL)
		return FALSE;
	
	if (error == NULL)
		error = &priv_error;
	
	dir = g_file_new_for_uri (uri);
	if (! make_directory_tree (dir, mode, error)) {
		g_warning ("could create directory %s: %s", uri, (*error)->message);
		if (priv_error != NULL)
			g_clear_error (&priv_error);
		return FALSE;
	}
	
	return TRUE;
}


gboolean
file_extension_is (const char *filename,
		   const char *ext)
{
	int filename_l, ext_l;

	filename_l = strlen (filename);
	ext_l = strlen (ext);

	if (filename_l < ext_l)
		return FALSE;
	return strcasecmp (filename + filename_l - ext_l, ext) == 0;
}


gboolean
is_mime_type (const char *type, 
	      const char *pattern) 
{
	return (strncasecmp (type, pattern, strlen (pattern)) == 0);
}


const char*
get_file_mime_type (const char *filename,
                    gboolean    fast_file_type)
{
	GFile      *file;
	GFileInfo  *info;
	GError     *err = NULL;
 	const char *result = NULL;
 	
	info = g_file_query_info (file, 
				  fast_file_type ?
				  G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE :
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  0, NULL, &err);

	result = g_file_info_get_content_type (info);

	g_object_unref (info);
	g_object_unref (file);

	return result;
}


void
path_list_free (GList *path_list)
{
	if (path_list == NULL)
		return;
	g_list_foreach (path_list, (GFunc) g_free, NULL);
	g_list_free (path_list);
}


GList *
path_list_dup (GList *path_list)
{
	GList *new_list = NULL;
	GList *scan;

	for (scan = path_list; scan; scan = scan->next)
		new_list = g_list_prepend (new_list, g_strdup (scan->data));

	return g_list_reverse (new_list);
}


guint64
get_dest_free_space (const char *uri)
{
	GFile     *file;
	GFileInfo *info;
	goffset    freespace;
	GError    *err = NULL;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, 0, NULL, &err);
	if (err != NULL) {
		freespace = 0;
		g_warning ("Could not get filesystem free space on volume that contains %s: %s", uri, err->message);
		g_error_free (err);
	}
	g_object_unref (info);
	g_object_unref (file);

	return freespace;
}


static gboolean
delete_directory_recursive (GFile   *dir,
			    GError **error)
{
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	gboolean         error_occurred = FALSE;
	         
	file_enum = g_file_enumerate_children (dir, 
					       G_FILE_ATTRIBUTE_STANDARD_NAME "," 
					       G_FILE_ATTRIBUTE_STANDARD_TYPE,
					       0, NULL, error);

	while (! error_occurred && (info = g_file_enumerator_next_file (file_enum, NULL, error)) != NULL) {
		const char *name;
		char       *child_uri;
		GFile      *child;
		
		child_uri = g_build_path ("/", uri, g_file_info_get_name (info), NULL);
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
	}

	if (! error_occurred && ! g_file_delete (dir, NULL, error)) 
 		error_occurred = TRUE;
	
	g_object_unref (file_enum);
	
	return ! error_occurred;
}


gboolean
remove_directory (const char *uri)
{
	GFile     *dir;
	gboolean   result;
	GError    *error;
	
	dir = g_file_new_for_uri (uri);
	result = delete_directory_recursive (dir, &error);
	if (error != NULL) {
		g_warning ("Cannot delete %s: %s", uri, error->message);
		g_clear_error (&err);
	}
	g_object_unref (dir);
	
	return result;
}


gboolean
remove_local_directory (const char *path)
{
	char     *uri;
	gboolean  result;
	
	uri = get_uri_from_local_path (path);
	result = remove_directory (uri);
	g_free (uri);
	
	return result;
}


gboolean
make_tree (const char  *uri,
	   GError     **error)
{
	GFile  *dir;
	
	dir = g_file_new_for_uri (uri);
	if (! make_directory_tree (dir, 0755, error)) {
		g_warning ("could create directory %s: %s", uri, error->message);
		return FALSE;
	}
	
	return TRUE;
}


static const char *try_folder[] = { "~", "tmp", NULL };


static const char *
get_folder_from_try_folder_list (int n)
{
	const char *folder;

	folder = try_folder[n];
	if (strcmp (folder, "~") == 0)
		folder = g_get_home_dir ();
	else if (strcmp (folder, "tmp") == 0)
		folder = g_get_tmp_dir ();

	return folder;
}


char *
get_temp_work_dir (void)
{
	guint64  max_size = 0;
	char    *best_folder = NULL;
	int      i;
	char    *template;
	char    *result = NULL;

	/* find the folder with more free space. */

	for (i = 0; try_folder[i] != NULL; i++) {
		const char *folder;
		char       *uri;
		guint64     size;

		folder = get_folder_from_try_folder_list (i);
		uri = g_strconcat ("file://", folder, NULL);

		size = get_dest_free_space (uri);
		if (max_size < size) {
			max_size = size;
			g_free (best_folder);
			best_folder = uri;
		}
		else
			g_free (uri);
	}

	template = g_strconcat (best_folder + strlen ("file://"), "/.fr-XXXXXX", NULL);
	result = mkdtemp (template);

	if ((result == NULL) || (*result == '\0')) {
		g_free (template);
		result = NULL;
	}

	return result;
}


gboolean
is_temp_work_dir (const char *dir)
{
	int i;

	if (strncmp (dir, "file://", 7) == 0)
		dir = dir + 7;
	else if (dir[0] != '/')
		return FALSE;

	for (i = 0; try_folder[i] != NULL; i++) {
		const char *folder;

		folder = get_folder_from_try_folder_list (i);
		if (strncmp (dir, folder, strlen (folder)) == 0) 
			if (strncmp (dir + strlen (folder), "/.fr-", 5) == 0)
				return TRUE;
	}

	return FALSE;
}


gboolean
is_temp_dir (const char *dir)
{
	if (strncmp (dir, "file://", 7) == 0)
		dir = dir + 7;
	if (strcmp (g_get_tmp_dir (), dir) == 0)
		return TRUE;
	if (path_in_path (g_get_tmp_dir (), dir))
		return TRUE;
	else
		return is_temp_work_dir (dir);
}


/* file list utils */


gboolean
file_list__match_pattern (const char *line,
			  const char *pattern)
{
	const char *l = line, *p = pattern;

	for (; (*p != 0) && (*l != 0); p++, l++) {
		if (*p != '%') {
			if (*p != *l)
				return FALSE;
		} 
		else {
			p++;
			switch (*p) {
			case 'a':
				break;
			case 'n':
				if (!isdigit (*l))
					return FALSE;
				break;
			case 'c':
				if (!isalpha (*l))
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}

	return (*p == 0);
}


int
file_list__get_index_from_pattern (const char *line,
				   const char *pattern)
{
	int         line_l, pattern_l;
	const char *l;

	line_l = strlen (line);
	pattern_l = strlen (pattern);

	if ((pattern_l == 0) || (line_l == 0))
		return -1;

	for (l = line; *l != 0; l++)
		if (file_list__match_pattern (l, pattern))
			return (l - line);

	return -1;
}


char*
file_list__get_next_field (const char *line,
			   int         start_from,
			   int         field_n)
{
	const char *f_start, *f_end;

	line = line + start_from;

	f_start = line;
	while ((*f_start == ' ') && (*f_start != *line))
		f_start++;
	f_end = f_start;

	while ((field_n > 0) && (*f_end != 0)) {
		if (*f_end == ' ') {
			field_n--;
			if (field_n != 0) {
				while ((*f_end == ' ') && (*f_end != *line))
					f_end++;
				f_start = f_end;
			}
		} else
			f_end++;
	}

	return g_strndup (f_start, f_end - f_start);
}


char*
file_list__get_prev_field (const char *line,
			   int         start_from,
			   int         field_n)
{
	const char *f_start, *f_end;

	f_start = line + start_from - 1;
	while ((*f_start == ' ') && (*f_start != *line))
		f_start--;
	f_end = f_start;

	while ((field_n > 0) && (*f_start != *line)) {
		if (*f_start == ' ') {
			field_n--;
			if (field_n != 0) {
				while ((*f_start == ' ') && (*f_start != *line))
					f_start--;
				f_end = f_start;
			}
		} else
			f_start--;
	}

	return g_strndup (f_start + 1, f_end - f_start);
}


gboolean
check_permissions (const char *uri,
		   int         mode)
{
	gboolean   result = TRUE;
	GFile     *file;
	GFileInfo *info;
	GError    *err = NULL;

	file = g_file_new_for_uri (uri);
	info = g_file_query_info (file, "access::*", 0, NULL, &err);
	if (err != NULL) {
		g_warning ("Failed to get access permissions for %s: %s", uri, err->message);
		g_clear_error (&err);
		result = FALSE;
	}
	else if ((mode & R_OK) && ! g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ))
		result = FALSE;
	else if ((mode & W_OK) && ! g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE))
		result = FALSE;
	else if ((mode & X_OK) && ! g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE))
		result = FALSE;

	g_object_unref (info);
	g_object_unref (file);

	return result;
}


gboolean
is_program_in_path (const char *filename)
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


const char *
get_home_uri (void)
{
	static char *home_uri = NULL;
	if (home_uri == NULL)
		home_uri = g_strconcat ("file://", g_get_home_dir (), NULL);
	return home_uri;
}


char *
get_uri_from_local_path (const char *path)
{
	return g_filename_to_uri (path, NULL, NULL);
}


char *
get_local_path_from_uri (const char *uri)
{
	return g_filename_from_uri (uri, NULL, NULL);
}


const char *
get_file_path_from_uri (const char *uri)
{
	if (uri == NULL)
		return NULL;
	if (uri_scheme_is_file (uri))
		return uri + FILE_PREFIX_L;
	else if (uri[0] == '/')
		return uri;
	else
		return NULL;
}


gboolean
uri_has_scheme (const char *uri)
{
	return strstr (uri, "://") != NULL;
}


gboolean
uri_is_local (const char *uri)
{
	return (! uri_has_scheme (uri)) || uri_scheme_is_file (uri);
}


gboolean
uri_scheme_is_file (const char *uri)
{
        if (uri == NULL)
                return FALSE;
        if (g_utf8_strlen (uri, -1) < FILE_PREFIX_L)
                return FALSE;
        return strncmp (uri, FILE_PREFIX, FILE_PREFIX_L) == 0;

}


const char *
remove_host_from_uri (const char *uri)
{
        const char *idx, *sep;

        if (uri == NULL)
                return NULL;

        idx = strstr (uri, "://");
        if (idx == NULL)
                return uri;
        idx += 3;
        if (*idx == '\0')
                return "/";
        sep = strstr (idx, "/");
        if (sep == NULL)
                return idx;
        return sep;
}


char *
get_uri_host (const char *uri)
{
	const char *idx;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return NULL;
	idx = strstr (idx + 3, "/");
	if (idx == NULL)
		return NULL;
	return g_strndup (uri, (idx - uri));
}


char *
get_uri_root (const char *uri)
{
	char *host;
	char *root;

	host = get_uri_host (uri);
	if (host == NULL)
		return NULL;
	root = g_strconcat (host, "/", NULL);
	g_free (host);

	return root;
}


char *
get_uri_from_path (const char *path)
{
	if (path == NULL)
		return NULL;
	if ((path[0] == '\0') || (path[0] == '/'))
		return g_strconcat ("file://", path, NULL);
	return g_strdup (path);
}


int
uricmp (const char *path1,
	const char *path2)
{
	char *uri1, *uri2;
	int   result;

	uri1 = get_uri_from_path (path1);
	uri2 = get_uri_from_path (path2);

	result = strcmp_null_tolerant (uri1, uri2);

	g_free (uri1);
	g_free (uri2);

	return result;
}


char *
get_new_uri (const char *folder,
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
	} while (uri_exists (new_uri));
	
	return new_uri;	
}


char *
get_new_uri_from_uri (const char *uri)
{
	char *base_uri;
	char *new_uri;
	
	base_uri = remove_level_from_path (uri);
	new_uri = get_new_uri (base_uri, file_name_from_path (uri));
	g_free (base_uri);
	
	return new_uri;
}


GList *
gio_file_list_dup (GList *l)
{
	GList *r = NULL, *scan;
	for (scan = l; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_dup ((GFile*)scan->data));
	return g_list_reverse (r);
}


void
gio_file_list_free (GList *l)
{
	GList *scan;
	for (scan = l; scan; scan = scan->next)
		g_object_unref (scan->data);
	g_list_free (l);
}


GList *
gio_file_list_new_from_uri_list (GList *uris) 
{
	GList *r = NULL, *scan;
	for (scan = uris; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_new_for_uri ((char*)scan->data));
	return g_list_reverse (r);
}
