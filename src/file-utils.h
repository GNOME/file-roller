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

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <sys/types.h>
#include <time.h>
#include <libgnomevfs/gnome-vfs-file-size.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>


#define FILENAME_MAX_LENGTH 30 /* FIXME: find out the best value */

#define get_home_relative_dir(x)        \
        g_strconcat (g_get_home_dir (), \
                     "/",               \
                     (x),               \
                     NULL)

gboolean            path_is_file                 (const gchar *s);

gboolean            path_is_dir                  (const gchar *s);

gboolean            dir_is_empty                 (const gchar *s);

gboolean            path_in_path                 (const char  *path_src,
						  const char  *path_dest);

GnomeVFSFileSize    get_file_size                (const gchar *s);

time_t              get_file_mtime               (const gchar *s);

time_t              get_file_ctime               (const gchar *s);

gboolean            file_copy                    (const gchar *from, 
						  const gchar *to);

gboolean            file_move                    (const gchar *from, 
						  const gchar *to);

gint                file_in_path                 (const gchar *name);

gboolean            ensure_dir_exists            (const gchar *a_path,
						  mode_t       mode);

gboolean            file_is_hidden               (const gchar *name);

G_CONST_RETURN gchar *             file_name_from_path          (const gchar *path);

gchar *             remove_level_from_path       (const gchar *path);

gchar *             remove_extension_from_path   (const gchar *path);

gchar *             remove_ending_separator      (const gchar *path);

gboolean            file_extension_is            (const char *filename, 
						  const char *ext);

void                path_list_free               (GList *path_list);

GList *             path_list_dup                (GList *path_list);

gboolean            is_mime_type                 (const char* type, 
						  const char* pattern);

gboolean            strchrs                      (const char *str,
						  const char *chars);

char*               escape_str_common            (const char *str, 
						  const char *meta_chars,
						  const char  prefix,
						  const char  postfix);

char*               escape_str                   (const char  *str, 
						  const char  *meta_chars);

gchar *             shell_escape                 (const gchar *filename);

gboolean            match_patterns               (char       **patterns, 
						  const char  *string,
						  int          flags);

char **             search_util_get_patterns     (const char  *pattern_string);

GnomeVFSFileSize    get_dest_free_space          (const char  *path);

gboolean            rmdir_recursive              (const gchar *directory);

char *              _g_strdup_with_max_size      (const char *s,
						  int         max_size);

const char *        eat_spaces                   (const char *line);

char **             split_line                   (const char *line, 
						  int   n_fields);

const char *        get_last_field               (const char *line,
						  int         last_field);

char *              get_temp_work_dir            (void);

char *              get_temp_work_dir_name       (void);

char *              str_substitute               (const char *str,
						  const char *from_str,
						  const char *to_str);

/* misc functions used to parse a command output lines. */

gboolean            file_list__match_pattern     (const char *line, 
						  const char *pattern);

int                 file_list__get_index_from_pattern (const char *line, 
						       const char *pattern);

char*               file_list__get_next_field    (const char *line,
						  int         start_from,
						  int         field_n);

char*               file_list__get_prev_field    (const char *line,
						  int         start_from,
						  int         field_n);

#endif /* FILE_UTILS_H */
