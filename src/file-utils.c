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
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime.h>
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


gboolean
path_exists (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	gboolean exists;
	gchar *escaped;

	if (! path || ! *path) return FALSE;

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));

	exists = (result == GNOME_VFS_OK);

	g_free (escaped);
	gnome_vfs_file_info_unref (info);

	return exists;
}


gboolean
uri_exists (const char  *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	gboolean          exists;

	if (uri == NULL) 
		return FALSE;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  GNOME_VFS_FILE_INFO_DEFAULT);

	exists = (result == GNOME_VFS_OK);

	gnome_vfs_file_info_unref (info);

	return exists;	
}


gboolean
path_is_file (const char *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	gboolean          is_file;

	if (! uri || ! *uri)
		return FALSE;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	is_file = FALSE;
	if (result == GNOME_VFS_OK)
		is_file = (info->type == GNOME_VFS_FILE_TYPE_REGULAR);

	gnome_vfs_file_info_unref (info);

	return is_file;
}


gboolean
path_is_dir (const char *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	gboolean          is_dir;

	if (! uri || ! *uri)
		return FALSE;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	is_dir = FALSE;
	if (result == GNOME_VFS_OK)
		is_dir = (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY);

	gnome_vfs_file_info_unref (info);

	return is_dir;
}


gboolean
dir_is_empty (const char *path)
{
	DIR *dp;
	int  n;

	if (strcmp (path, "/") == 0)
		return FALSE;

	dp = opendir (path);
	if (dp == NULL)
		return TRUE;

	n = 0;
	while (readdir (dp) != NULL) {
		n++;
		if (n > 2) {
			closedir (dp);
			return FALSE;
		}
	}
	closedir (dp);

	return TRUE;
}


gboolean
dir_contains_one_object (const char *uri)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult           result;
	int                      n;
	GnomeVFSFileInfo         info;
	
	result = gnome_vfs_directory_open (&handle, uri, GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;
		
	n = 0;
	while (gnome_vfs_directory_read_next (handle, &info) == GNOME_VFS_OK) {
		if ((strcmp (info.name, ".") == 0) || (strcmp (info.name, "..") == 0))
			continue;
		if (++n > 1) 
			break;
	}

	gnome_vfs_directory_close (handle);
	
	return (n == 1);
}


char *
get_directory_content_if_unique (const char  *uri)
{
	GnomeVFSDirectoryHandle *handle;
	GnomeVFSResult           result;
	GnomeVFSFileInfo        *info;
	char                    *content_uri = NULL;
	
	result = gnome_vfs_directory_open (&handle, uri, GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return NULL;
	
	info = gnome_vfs_file_info_new ();
	while (gnome_vfs_directory_read_next (handle, info) == GNOME_VFS_OK) {
		if ((strcmp (info->name, ".") == 0) || (strcmp (info->name, "..") == 0))
			continue;
		if (content_uri != NULL) {
			g_free (content_uri);
			content_uri = NULL;
			break;
		}
		else
			content_uri = g_strconcat (uri, "/", info->name, NULL);
	}
	gnome_vfs_file_info_unref (info);
	gnome_vfs_directory_close (handle);
	
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


GnomeVFSFileSize
get_file_size (const char *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	GnomeVFSFileSize  size;

	if ((uri == NULL) || (*uri == '\0'))
		return 0;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	size = 0;
	if (result == GNOME_VFS_OK)
		size = info->size;

	gnome_vfs_file_info_unref (info);

	return size;
}


time_t
get_file_mtime (const char *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	time_t            mtime;

	if ((uri == NULL) || (*uri == '\0'))
		return 0;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	mtime = 0;
	if (result == GNOME_VFS_OK)
		mtime = info->mtime;

	gnome_vfs_file_info_unref (info);

	return mtime;
}


time_t
get_file_ctime (const char *uri)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    result;
	time_t            ctime;

	if ((uri == NULL) || (*uri == '\0'))
		return 0;

	info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (uri,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	ctime = 0;
	if (result == GNOME_VFS_OK)
		ctime = info->ctime;

	gnome_vfs_file_info_unref (info);

	return ctime;
}


gboolean
file_copy (const gchar *from,
	   const gchar *to)
{
	FILE *fin, *fout;
	gchar buf[BUF_SIZE];
	gchar *dest_dir;
	gint  n;

	if (strcmp (from, to) == 0) {
		g_warning ("cannot copy file %s: source and destination are the same\n", from);
		return FALSE;
	}

	fin = fopen (from, "rb");
	if (! fin)
		return FALSE;

	dest_dir = remove_level_from_path (to);
	if (! ensure_dir_exists (dest_dir, 0755)) {
		g_free (dest_dir);
		fclose (fin);
		return FALSE;
	}

	fout = fopen (to, "wb");
	if (! fout) {
		g_free (dest_dir);
		fclose (fin);
		return FALSE;
	}

	while ((n = fread (buf, sizeof (char), BUF_SIZE, fin)) != 0)
		if (fwrite (buf, sizeof (char), n, fout) != n) {
			g_free (dest_dir);
			fclose (fin);
			fclose (fout);
			return FALSE;
		}

	g_free (dest_dir);
	fclose (fin);
	fclose (fout);

	return TRUE;
}


gboolean
file_move (const gchar *from,
	   const gchar *to)
{
	if (file_copy (from, to) && ! unlink (from))
		return TRUE;

	return FALSE;
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


gchar *
remove_ending_separator (const gchar *path)
{
	gint len, copy_len;

	if (path == NULL)
		return NULL;

	copy_len = len = strlen (path);
	if ((len > 1) && (path[len - 1] == '/'))
		copy_len--;

	return g_strndup (path, copy_len);
}


gboolean
ensure_dir_exists (const gchar *a_path,
		   mode_t       mode)
{
	if (! a_path) return FALSE;

	if (! path_is_dir (a_path)) {
		char *path = g_strdup (a_path);
		char *p = path;

		while (*p != '\0') {
			p++;
			if ((*p == '/') || (*p == '\0')) {
				gboolean end = TRUE;

				if (*p != '\0') {
					*p = '\0';
					end = FALSE;
				}

				if (! path_is_dir (path)) {
					if (gnome_vfs_make_directory (path, mode) != GNOME_VFS_OK) {
						g_warning ("directory creation failed: %s.", path);
						g_free (path);
						return FALSE;
					}
				}
				if (! end) *p = '/';
			}
		}
		g_free (path);
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
is_mime_type (const char* type, const char* pattern) {
	return (strncasecmp (type, pattern, strlen (pattern)) == 0);
}


GHashTable *static_strings = NULL;


static const char *
get_static_string (const char *s)
{
        const char *result;

        if (s == NULL)
                return NULL;

        if (static_strings == NULL)
                static_strings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        if (! g_hash_table_lookup_extended (static_strings, s, (gpointer*) &result, NULL)) {
                result = g_strdup (s);
                g_hash_table_insert (static_strings,
                                     (gpointer) result,
                                     GINT_TO_POINTER (1));
        }

        return result;
}


static const char *
get_extension (const char *path)
{
        int         len;
        int         p;
        const char *ptr = path;

        if (! path)
                return NULL;

        len = strlen (path);
        if (len <= 1)
                return NULL;

        p = len - 1;
        while ((p >= 0) && (ptr[p] != '.'))
                p--;

        if (p < 0)
                return NULL;

        return path + p;
}


static char*
get_sample_name (const char *filename)
{
        const char *ext;

        ext = get_extension (filename);
        if (ext == NULL)
                return NULL;

        return g_strconcat ("a", get_extension (filename), NULL);
}


const char*
get_file_mime_type (const char *filename,
                    gboolean    fast_file_type)
{
	const char *result = NULL;
	
        if (filename == NULL)
                return NULL;

        if (fast_file_type) {
                char *sample_name;
                char *n1;

                sample_name = get_sample_name (filename);
                if (sample_name != NULL) {
                        n1 = g_filename_to_utf8 (sample_name, -1, 0, 0, 0);
                        if (n1 != NULL) {
                                char *n2 = g_utf8_strdown (n1, -1);
                                char *n3 = g_filename_from_utf8 (n2, -1, 0, 0, 0);
                                if (n3 != NULL)
                                        result = gnome_vfs_mime_type_from_name_or_default (file_name_from_path (n3), NULL);
                                g_free (n3);
                                g_free (n2);
                                g_free (n1);
                        }
                        g_free (sample_name);
                }
        } 
        else {
                if (uri_scheme_is_file (filename))
                        filename = get_file_path_from_uri (filename);
                result = gnome_vfs_get_file_mime_type (filename, NULL, FALSE);
        }

        return get_static_string (result);
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


GnomeVFSFileSize
get_dest_free_space (const char *uri)
{
	GnomeVFSURI      *vfs_uri;
	GnomeVFSResult    result;
	GnomeVFSFileSize  ret_val;

	vfs_uri = gnome_vfs_uri_new (uri);
	result = gnome_vfs_get_volume_free_space (vfs_uri, &ret_val);
	gnome_vfs_uri_unref (vfs_uri);

	if (result != GNOME_VFS_OK)
		return (GnomeVFSFileSize) 0;
	else
		return ret_val;
}


#define SPECIAL_DIR(x) (! strcmp (x, "..") || ! strcmp (x, "."))


static gboolean
get_directory_content (const char  *uri,
	       	       GList      **files,
	               GList      **dirs)
{
	GnomeVFSDirectoryHandle *h;
	GnomeVFSResult           result;
	GnomeVFSFileInfo        *info;
	GList                   *f_list = NULL;
	GList                   *d_list = NULL;
		
	if (dirs) 
		*dirs = NULL;
	if (files) 
		*files = NULL;

	result = gnome_vfs_directory_open (&h, uri, GNOME_VFS_FILE_INFO_DEFAULT);
	if (result != GNOME_VFS_OK)
		return FALSE;	

	info = gnome_vfs_file_info_new ();
	
	while ((result = gnome_vfs_directory_read_next (h, info)) == GNOME_VFS_OK) {
		char *entry_uri;
		char *e_name;
		
		e_name = gnome_vfs_escape_string (info->name);
		entry_uri = g_strconcat (uri, "/", e_name, NULL);
		g_free (e_name);
		
		if (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
			if (! SPECIAL_DIR (info->name)) {
				d_list = g_list_prepend (d_list, entry_uri);
				entry_uri = NULL;
			}
		}
		else if (info->type == GNOME_VFS_FILE_TYPE_REGULAR) {
			f_list = g_list_prepend (f_list, entry_uri);
			entry_uri = NULL;
		}
		
		g_free (entry_uri);	
	}
	
	gnome_vfs_file_info_unref (info);
	gnome_vfs_directory_close (h);
	
	if (dirs) 
		*dirs = g_list_reverse (d_list);
	if (files) 
		*files = g_list_reverse (f_list);

	return TRUE;
}


gboolean
remove_directory (const char *uri)
{
	GList          *files, *dirs;
	GList          *scan;
	gboolean        error = FALSE;
	GnomeVFSResult  result;

	if (! get_directory_content (uri, &files, &dirs))
		return FALSE;

	for (scan = files; scan; scan = scan->next) {
		char *file = scan->data;
		
		result = gnome_vfs_unlink (file);
		if (result != GNOME_VFS_OK) {
			g_warning ("Cannot delete %s: %s\n", file, gnome_vfs_result_to_string (result));
			error = TRUE;
		}
	}

	for (scan = dirs; scan; scan = scan->next) 
		if (! remove_directory ((char*) scan->data))
			error = TRUE;
	
	path_list_free (files);
	path_list_free (dirs);

	result = gnome_vfs_remove_directory (uri);
	if (result != GNOME_VFS_OK) {
		g_warning ("Cannot delete %s: %s\n", uri, gnome_vfs_result_to_string (result));
		error = TRUE;
	}

	return ! error;
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


GnomeVFSResult
make_tree (const char *uri)
{
	char  *root;
	char **parts;
	char  *parent;
	int    i;

	root = get_uri_host (uri);
	if ((uricmp (root, uri) == 0) || (strlen (uri) <= strlen (root) + 1)) {
		g_free (root);
		return GNOME_VFS_OK;
	}

	parts = g_strsplit (uri + strlen (root) + 1, "/", -1);
	if ((parts == NULL) || (parts[0] == NULL) || (parts[1] == NULL)) {
		if (parts != NULL)
			g_strfreev (parts);
		g_free (root);
		return GNOME_VFS_OK;
	}

	parent = g_strdup (root);
	for (i = 0; parts[i + 1] != NULL; i++) {
		char           *tmp;
		GnomeVFSResult  result;

		tmp = g_strconcat (parent, "/", parts[i], NULL);
		g_free (parent);
		parent = tmp;

		result = gnome_vfs_make_directory (parent, 0755);
		if ((result != GNOME_VFS_OK) && (result != GNOME_VFS_ERROR_FILE_EXISTS)) {
			g_free (parent);
			g_strfreev (parts);
			g_free (root);
			return result;
		}
	}

	g_free (parent);
	g_strfreev (parts);
	g_free (root);

	return GNOME_VFS_OK;
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
	GnomeVFSFileSize  max_size = 0;
	char             *best_folder = NULL;
	int               i;
	char             *template;
	char             *result = NULL;

	/* find the folder with more free space. */

	for (i = 0; try_folder[i] != NULL; i++) {
		const char       *folder;
		char             *uri;
		GnomeVFSFileSize  size;

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
is_temp_work_dir (const char *dir) {
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
		} else {
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


char *
escape_uri (const char *uri)
{
	const char *start = NULL;
	const char *uri_no_method;
	char       *method;
	char       *epath, *euri;

	if (uri == NULL)
		return NULL;

	start = strstr (uri, "://");
	if (start != NULL) {
		uri_no_method = start + strlen ("://");
		method = g_strndup (uri, start - uri);
	} 
	else {
		uri_no_method = uri;
		method = NULL;
	}

	epath = gnome_vfs_escape_host_and_path_string (uri_no_method);

	if (method != NULL) {
		euri = g_strdup_printf ("%s://%s", method, epath);
		g_free (epath);
	} else
		euri = epath;

	g_free (method);

	return euri;
}


gboolean
check_permissions (const char *uri,
		   int         mode)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult    vfs_result;
	gboolean          result = TRUE;

	info = gnome_vfs_file_info_new ();
	vfs_result = gnome_vfs_get_file_info (uri,
					      info,
					      (GNOME_VFS_FILE_INFO_FOLLOW_LINKS
					       | GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS));

	if (vfs_result != GNOME_VFS_OK)
		result = FALSE;

	else if ((mode & R_OK) && ! (info->permissions & GNOME_VFS_PERM_ACCESS_READABLE))
		result = FALSE;

	else if ((mode & W_OK) && ! (info->permissions & GNOME_VFS_PERM_ACCESS_WRITABLE))
		result = FALSE;

	else if ((mode & X_OK) && ! (info->permissions & GNOME_VFS_PERM_ACCESS_EXECUTABLE))
		result = FALSE;

	gnome_vfs_file_info_unref (info);

	return TRUE;
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


GnomeVFSURI *
new_uri_from_path (const char *path)
{
	char        *uri_txt;
	GnomeVFSURI *uri;

	uri_txt = get_uri_from_local_path (path);
	uri = gnome_vfs_uri_new (uri_txt);
	g_free (uri_txt);

	g_return_val_if_fail (uri != NULL, NULL);

	return uri;
}


char *
get_uri_from_local_path (const char *local_path)
{
	char *escaped;
	char *uri;

	escaped = escape_uri (local_path);
	if (escaped[0] == '/') {
		uri = g_strconcat ("file://", escaped, NULL);
		g_free (escaped);
	}
	else
		uri = escaped;

	return uri;
}


char *
get_local_path_from_uri (const char *uri)
{
	return gnome_vfs_unescape_string (remove_host_from_uri (uri), NULL);
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

	result = strcmp_null_tollerant (uri1, uri2);

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
