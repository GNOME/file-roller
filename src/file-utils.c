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

#include <fnmatch.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>

#include <glib.h>
#include <libgnome/libgnome.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <gconf/gconf-client.h>
#include "file-utils.h"

#define BUF_SIZE 4096

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
dir_is_empty (const gchar *path)
{
	DIR *dp;
	int n;

	if (strcmp (path, "/") == 0)
		return FALSE;

	dp = opendir (path);
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


GnomeVFSFileSize 
get_file_size (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	GnomeVFSFileSize size;
	gchar *escaped;

	if (! path || ! *path) return 0; 

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped, 
					  info,
					  (GNOME_VFS_FILE_INFO_DEFAULT 
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS)); 
	size = 0;
	if (result == GNOME_VFS_OK)
		size = info->size;

	g_free (escaped);
	gnome_vfs_file_info_unref (info);

	return size;
}


time_t 
get_file_mtime (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	gchar *escaped;
	time_t mtime;

	if (! path || ! *path) return 0; 

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped, 
					  info, 
					  (GNOME_VFS_FILE_INFO_DEFAULT 
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	mtime = 0;
	if (result == GNOME_VFS_OK)
		mtime = info->mtime;

	g_free (escaped);
	gnome_vfs_file_info_unref (info);

	return mtime;
}


time_t 
get_file_ctime (const gchar *path)
{
	GnomeVFSFileInfo *info;
	GnomeVFSResult result;
	gchar *escaped;
	time_t ctime;

	if (! path || ! *path) return 0; 

	info = gnome_vfs_file_info_new ();
	escaped = gnome_vfs_escape_path_string (path);
	result = gnome_vfs_get_file_info (escaped, 
					  info, 
					  (GNOME_VFS_FILE_INFO_DEFAULT 
					   | GNOME_VFS_FILE_INFO_FOLLOW_LINKS));
	ctime = 0;
	if (result == GNOME_VFS_OK)
		ctime = info->ctime;

	g_free (escaped);
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


gchar *
remove_level_from_path (const gchar *path)
{
	gchar *new_path;
	const gchar *ptr = path;
	gint p;

	if (! path) return NULL;

	p = strlen (path) - 1;
	if (p < 0) return NULL;

	while ((ptr[p] != '/') && (p > 0)) p--;
	if ((p == 0) && (ptr[p] == '/')) p++;
	new_path = g_strndup (path, (guint)p);

	return new_path;
}


gchar *
remove_extension_from_path (const gchar *path)
{
	gchar *new_path;
	const gchar *ptr = path;
	gint p;

	if (!path) return NULL;
	if (strlen (path) < 2) return g_strdup( path);

	p = strlen (path) - 1;
	while ((ptr[p] != '.') && (p > 0)) p--;
	if (p == 0) p = strlen (path) - 1;
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
		gchar *path = g_strdup (a_path);
		gchar *p = path;

		while (*p != '\0') {
			p++;
			if ((*p == '/') || (*p == '\0')) {
				gboolean end = TRUE;

				if (*p != '\0') {
					*p = '\0';
					end = FALSE;
				}

			if (! path_is_dir (path)) {
					if (mkdir (path, mode) < 0) {
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


/* characters to escape */
static gchar bad_char[] = { '$', '\'', '`', '"', '\\', '!', '?', '*',
			    ' ', '(', ')', '[', ']', '&', '|', '@', '#',
			    ':', ';', '=', '-' };

/* the size of bad_char */
static const gint bad_chars = sizeof (bad_char) / sizeof (gchar);


/* counts how many characters to escape in @str. */
static gint
count_chars_to_escape (const gchar *str)
{
	const gchar *s;
	gint i, n;

	n = 0;
	for (s = str; *s != 0; s++)
		for (i = 0; i < bad_chars; i++)
			if (*s == bad_char[i]) {
				n++;
				break;
			}
	return n;
}


/* escape with backslash the file name. */
gchar*
shell_escape (const gchar *filename)
{
	gchar *escaped;
	gint i, new_l;
	const gchar *s;
	gchar *t;

	if (filename == NULL) 
		return NULL;

	new_l = strlen (filename) + count_chars_to_escape (filename);
	escaped = g_malloc (new_l + 1);

	s = filename;
	t = escaped;
	while (*s) {
		gboolean is_bad;
	
		is_bad = FALSE;
		for (i = 0; (i < bad_chars) && !is_bad; i++)
			is_bad = (*s == bad_char[i]);

		if (is_bad)
			*t++ = '\\';
		*t++ = *s++;
	}
	*t = 0;

	return escaped;
}


static gchar *
get_terminal ()
{
	GConfClient *client;
	gchar *result;
	gchar *terminal = NULL;
	gchar *exec_flag = NULL;

	client = gconf_client_get_default ();
	terminal = gconf_client_get_string (client, "/desktop/gnome/applications/terminal/exec", NULL);
	g_object_unref (G_OBJECT (client));
	
	if (terminal) 
		exec_flag = gconf_client_get_string (client, "/desktop/gnome/applications/terminal/exec_arg", NULL);

	if (terminal == NULL) {
		char *check;

		check = g_find_program_in_path ("gnome-terminal");
		if (check != NULL) {
			terminal = check;
			/* Note that gnome-terminal takes -x and
			 * as -e in gnome-terminal is broken we use that. */
			exec_flag = g_strdup ("-x");
		} else {
			if (check == NULL)
				check = g_find_program_in_path ("nxterm");
			if (check == NULL)
				check = g_find_program_in_path ("color-xterm");
			if (check == NULL)
				check = g_find_program_in_path ("rxvt");
			if (check == NULL)
				check = g_find_program_in_path ("xterm");
			if (check == NULL)
				check = g_find_program_in_path ("dtterm");
			if (check == NULL) {
				g_warning (_("Cannot find a terminal, using "
					     "xterm, even if it may not work"));
				check = g_strdup ("xterm");
			}
			terminal = check;
			exec_flag = g_strdup ("-e");
		}
	}

	if (terminal == NULL)
		return NULL;

	result = g_strconcat (terminal, " ", exec_flag, NULL);
	return result;
}


gchar *
application_get_command (const GnomeVFSMimeApplication *app)
{
	char *command;
	
	if (app->requires_terminal) {
		char *terminal;
		char *command_to_exec;

		terminal = get_terminal ();
		if (terminal == NULL)
			return NULL;

		command_to_exec = shell_escape (app->command);
		command = g_strconcat (terminal,
				       " ",
				       command_to_exec,
				       NULL);
		g_free (terminal);
		g_free (command_to_exec);
	} else
		command = shell_escape (app->command);

	return command;
}


#define CASE_INSENSITIVE (1 << 4)


gboolean
match_patterns (char       **patterns, 
		const char  *string)
{
	gint i;
	gint result;
       
	if (patterns[0] == NULL)
		return TRUE;
	
	if (string == NULL)
		return FALSE;
	
	result = FNM_NOMATCH;
	i = 0;
	while ((result != 0) && (patterns[i] != NULL)) {
		result = fnmatch (patterns[i], string, CASE_INSENSITIVE);
		i++;
	}
	
	return (result == 0);
}


char **
search_util_get_patterns (const char *pattern_string)
{
	gchar **patterns;
	gint    i;
	
	patterns = g_strsplit (pattern_string, ";", 10);
	for (i = 0; patterns[i] != NULL; i++) 
		patterns[i] = g_strstrip (patterns[i]);
	
	return patterns;
}


GnomeVFSFileSize
get_dest_free_space (const char  *path)
{
	char             *escaped;
	GnomeVFSURI      *uri;
	GnomeVFSResult    result;
	GnomeVFSFileSize  ret_val;

	escaped = gnome_vfs_escape_path_string (path);
	uri = gnome_vfs_uri_new (escaped);
	g_free (escaped);

	result = gnome_vfs_get_volume_free_space (uri, &ret_val);

	gnome_vfs_uri_unref (uri);

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
