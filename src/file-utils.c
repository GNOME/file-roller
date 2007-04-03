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
path_is_file (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	gboolean is_file;
	gchar *escaped;

	if (! path || ! *path) return FALSE;

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	is_file = FALSE;
	if (result == GNOME_VFS_OK)
		is_file = (info->type == GNOME_VFS_FILE_TYPE_REGULAR);

	g_free (escaped);
	gnome_vfs_file_info_unref (info);

	return is_file;
}


gboolean
path_is_dir (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	gboolean is_dir;
	gchar *escaped;

	if (! path || ! *path) return FALSE;

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped,
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	is_dir = FALSE;
	if (result == GNOME_VFS_OK)
		is_dir = (info->type == GNOME_VFS_FILE_TYPE_DIRECTORY);

	g_free (escaped);
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
dir_contains_one_object (const char *path)
{
	DIR *dp;
	int  n;

	if (path == NULL)
		return FALSE;

	if (strcmp (path, "/") == 0)
		return FALSE;

	dp = opendir (path);
	if (dp == NULL)
		return FALSE;

	n = 0;
	while (readdir (dp) != NULL) {
		n++;
		if (n > 3) {
			closedir (dp);
			return FALSE;
		}
	}
	closedir (dp);

	return TRUE;
}


gboolean
dir_is_temp_dir (const char *dir) {
	const char *tmp_dir = g_get_tmp_dir ();
	return (strncmp (dir, tmp_dir, strlen (tmp_dir)) == 0);
}


/* Check whether the path_src is contained in path_dest */
gboolean
path_in_path (const char  *path_src,
	      const char  *path_dest)
{
	int path_src_l, path_dest_l;

	if ((path_src == NULL) || (path_dest == NULL))
		return FALSE;

	path_src_l = strlen (path_src);
	path_dest_l = strlen (path_dest);

	return ((path_dest_l > path_src_l)
		&& (strncmp (path_src, path_dest, path_src_l) == 0)
		&& ((path_src_l == 1) || (path_dest[path_src_l] == '/')));
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
	register gssize base;
	register gssize last_char;

	if (file_name == NULL)
		return NULL;

	if (file_name[0] == '\0')
		return "";

	last_char = strlen (file_name) - 1;

	if (file_name [last_char] == G_DIR_SEPARATOR)
		return "";

	base = last_char;
	while ((base >= 0) && (file_name [base] != G_DIR_SEPARATOR))
		base--;

	return file_name + base + 1;
}


char *
dir_name_from_path (const gchar *path)
{
	register  gssize base;
	register  gssize last_char;

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

	if (! path)
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


G_CONST_RETURN char*
get_mime_type (const char  *filename)
{
	return gnome_vfs_get_mime_type_for_name (filename);
}


void
path_list_free (GList *path_list)
{
	if (path_list != NULL) {
		g_list_foreach (path_list, (GFunc) g_free, NULL);
		g_list_free (path_list);
	}
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
path_list_new (const char  *path,
	       GList      **files,
	       GList      **dirs)
{
	DIR *dp;
	struct dirent *dir;
	struct stat stat_buf;
	GList *f_list = NULL;
	GList *d_list = NULL;

	dp = opendir (path);
	if (dp == NULL) return FALSE;

	while ((dir = readdir (dp)) != NULL) {
		gchar *name;
		gchar *filepath;

		/* Skip removed files */
		if (dir->d_ino == 0)
			continue;

		name = dir->d_name;
		if (strcmp (path, "/") == 0)
			filepath = g_strconcat (path, name, NULL);
		else
			filepath = g_strconcat (path, "/", name, NULL);

		if (stat (filepath, &stat_buf) >= 0) {
			if (dirs
			    && S_ISDIR (stat_buf.st_mode)
			    && ! SPECIAL_DIR (name))
			{
				d_list = g_list_prepend (d_list, filepath);
				filepath = NULL;
			} else if (files && S_ISREG (stat_buf.st_mode)) {
				f_list = g_list_prepend (f_list, filepath);
				filepath = NULL;
			}
		}

		if (filepath) g_free (filepath);
	}
	closedir (dp);

	if (dirs) *dirs = g_list_reverse (d_list);
	if (files) *files = g_list_reverse (f_list);

	return TRUE;
}


gboolean
rmdir_recursive (const gchar *directory)
{
	GList    *files, *dirs;
	GList    *scan;
	gboolean  error = FALSE;

	if (! path_is_dir (directory))
		return FALSE;

	path_list_new (directory, &files, &dirs);

	for (scan = files; scan; scan = scan->next) {
		char *file = scan->data;
		if ((unlink (file) < 0)) {
			g_warning ("Cannot delete %s\n", file);
			error = TRUE;
		}
	}
	path_list_free (files);

	for (scan = dirs; scan; scan = scan->next) {
		char *sub_dir = scan->data;
		if (rmdir_recursive (sub_dir) == FALSE)
			error = TRUE;
		if (rmdir (sub_dir) == 0)
			error = TRUE;
	}
	path_list_free (dirs);

	if (rmdir (directory) == 0)
		error = TRUE;

	return !error;
}


char *
get_temp_work_dir (void)
{
	const char       *try_folder[] = { "~", "tmp", NULL };
	GnomeVFSFileSize  max_size = 0;
	char             *best_folder = NULL;
	int               i;
	char             *template;
	char             *result = NULL;

	for (i = 0; try_folder[i] != NULL; i++) {
		const char       *folder;
		char             *uri;
		GnomeVFSFileSize  size;

		folder = try_folder[i];
		if (strcmp (folder, "~") == 0)
			folder = g_get_home_dir ();
		else if (strcmp (folder, "tmp") == 0)
			folder = g_get_tmp_dir ();

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
		template = NULL;
	}

	return result;
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
	} else {
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

	value = g_hash_table_lookup (programs_cache, filename);
	if (value != NULL) {
		result = (strcmp (value, "1") == 0);
		return result;
	}

	str = g_find_program_in_path (filename);
	if (str != NULL) {
		g_free (str);
		result = TRUE;
	}

	g_hash_table_insert (programs_cache,
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


gboolean
uri_has_scheme (const char *uri)
{
	return strstr (uri, "://") != NULL;
}


gboolean
uri_is_local (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	gboolean     is_local;

	vfs_uri = new_uri_from_path (uri);
	g_return_val_if_fail (vfs_uri != NULL, FALSE);

	is_local = gnome_vfs_uri_is_local (vfs_uri);
	gnome_vfs_uri_unref (vfs_uri);

	return is_local;
}


const char *
remove_host_from_uri (const char *uri)
{
	const char *idx;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return uri;
	idx = strstr (idx + 3, "/");
	if (idx == NULL)
		return NULL;
	return idx;
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
get_uri_from_path (const char *path)
{
	if (path == NULL)
		return NULL;
	if ((path == "") || (path[0] == '/'))
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
