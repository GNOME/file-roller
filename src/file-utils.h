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

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <gio/gio.h>

/* uri */

gboolean            _g_uri_query_exists                   (const char  *uri);
gboolean            _g_uri_query_is_file                  (const char  *uri);
gboolean            _g_uri_query_is_dir                   (const char  *uri);
char *              _g_uri_create_alternative             (const char  *folder,
							   const char  *name);
char *              _g_uri_create_alternative_for_uri     (const char  *uri);
gboolean            _g_uri_query_dir_is_empty             (const char  *uri);
gboolean            _g_uri_dir_contains_one_object        (const char  *uri);
char *              _g_uri_get_dir_content_if_unique      (const char  *uri);
goffset             _g_uri_get_file_size                  (const char  *uri);

time_t              _g_uri_get_file_mtime                 (const char  *uri);

time_t              _g_uri_get_file_ctime                 (const char  *uri);
gboolean            _g_uri_ensure_dir_exists              (const char  *uri,
							   mode_t       mode,
							   GError     **error);
const char*         _g_uri_get_mime_type                  (const char  *uri,
							   gboolean     fast_file_type);
gboolean            _g_uri_remove_directory               (const char  *uri);
gboolean            _g_uri_check_permissions              (const char  *uri,
							   int          mode);

/* path */

gboolean            _g_path_query_is_dir                  (const char  *path);
goffset             _g_path_get_file_size                 (const char  *path);
time_t              _g_path_get_file_mtime                (const char  *path);
gboolean            _g_path_make_directory_tree           (const char  *path,
							   mode_t       mode,
							   GError     **error);
const char*         _g_path_get_mime_type                 (const char  *filename,
							   gboolean     fast_file_type);
guint64             _g_path_get_free_space                (const char  *path);
gboolean            _g_path_remove_directory              (const char  *path);
char *              _g_path_get_temp_work_dir             (const char  *parent_folder);
gboolean            _g_path_is_temp_work_dir              (const char  *path);
gboolean            _g_path_is_temp_dir                   (const char  *path);

/* GFile */

gboolean            _g_file_check_permissions             (GFile       *file,
							   int          mode);
gboolean            _g_file_make_directory_tree           (GFile       *dir,
							   mode_t       mode,
							   GError     **error);
GFile *             _g_file_new_user_config_subdir        (const char  *child_name,
						    	   gboolean     create_);

/* program */

gboolean 	    _g_program_is_in_path		  (const char *filename);
gboolean 	    _g_program_is_available	          (const char *filename,
							   gboolean    check);

/* GKeyFile */

void                _g_key_file_save                      (GKeyFile   *key_file,
						           GFile      *file);

#endif /* FILE_UTILS_H */
