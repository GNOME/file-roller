/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
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

#include <time.h>
#include <libgnomevfs/gnome-vfs-file-size.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>


gboolean            path_is_file                 (const gchar *s);

gboolean            path_is_dir                  (const gchar *s);

gboolean            dir_is_empty                 (const gchar *s);

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

gchar *             shell_escape                 (const gchar *filename);

gchar *             application_get_command      (const GnomeVFSMimeApplication *app);

gboolean            match_patterns               (char       **patterns, 
						  const char  *string);

char **             search_util_get_patterns     (const char  *pattern_string);

#endif /* FILE_UTILS_H */
