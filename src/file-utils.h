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

/* path */

char *              _g_path_get_temp_work_dir             (const char *parent_folder);

/* GFile */

gboolean            _g_file_query_is_file                 (GFile       *file);
gboolean            _g_file_query_is_dir                  (GFile       *file);
goffset             _g_file_get_file_size                 (GFile       *file);
time_t              _g_file_get_file_mtime                (GFile       *file);
time_t              _g_file_get_file_ctime                (GFile       *file);
const char*         _g_file_get_mime_type                 (GFile       *file,
							   gboolean     fast_file_type);
gboolean            _g_file_is_temp_dir                   (GFile       *file);
GFile *             _g_file_create_alternative            (GFile       *folder,
							   const char  *name);
GFile *             _g_file_create_alternative_for_file   (GFile       *file);
gboolean            _g_file_check_permissions             (GFile       *file,
							   int          mode);
gboolean            _g_file_make_directory_tree           (GFile       *dir,
							   mode_t       mode,
							   GError     **error);
gboolean            _g_file_remove_directory              (GFile         *directory,
							   GCancellable  *cancellable,
							   GError       **error);
GFile *             _g_file_new_user_config_subdir        (const char  *child_name,
						    	   gboolean     create_);
GFile *             _g_file_get_dir_content_if_unique     (GFile       *file);
guint64             _g_file_get_free_space                (GFile       *file);
GFile *             _g_file_get_temp_work_dir             (GFile       *parent_folder);
gboolean            _g_file_is_temp_work_dir              (GFile       *file);
gboolean            _g_file_query_dir_is_empty            (GFile       *file);
gboolean            _g_file_dir_contains_one_object       (GFile       *file);

/* program */

gboolean 	    _g_program_is_in_path		  (const char *filename);
gboolean 	    _g_program_is_available	          (const char *filename,
							   gboolean    check);

/* GKeyFile */

void                _g_key_file_save                      (GKeyFile   *key_file,
						           GFile      *file);

#endif /* FILE_UTILS_H */
