/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2008 Free Software Foundation, Inc.
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

#ifndef _GIO_UTILS_H
#define _GIO_UTILS_H

#include <glib.h>
#include <gio/gio.h>

/* callback types */

typedef gboolean (*StartDirCallback) (const char  *uri,
				      GError     **error,
				      gpointer     user_data);
typedef void (*ForEachChildCallback) (const char  *uri, 
				      GFileInfo   *info, 
				      gpointer     user_data);
typedef void (*ForEachDoneCallback)  (GError      *error, 
				      gpointer     data);			     
typedef void (*ListReadyCallback)    (GList       *files, 
				      GList       *dirs, 
				      GError      *error, 
				      gpointer     user_data);
typedef void (*CopyProgressCallback) (goffset      current_file,
                                      goffset      total_files,
                                      GFile       *source,
                                      GFile       *destination,
                                      goffset      current_num_bytes,
                                      goffset      total_num_bytes,
                                      gpointer     user_data);
typedef void (*CopyDoneCallback)     (GError      *error,
				      gpointer     user_data);
				           
/* asynchronous recursive list functions */

void   g_directory_foreach_child     (const char            *directory,
				      gboolean               recursive,
				      gboolean               follow_links,
				      GCancellable          *cancellable,
				      StartDirCallback       start_dir_func,
				      ForEachChildCallback   for_each_file_func,
				      ForEachDoneCallback    done_func,
				      gpointer               user_data);
void   g_directory_list_async        (const char            *directory, 
				      const char            *base_dir,
				      gboolean               recursive,
				      gboolean               follow_links,
				      gboolean               no_backup_files,
				      gboolean               no_dot_files,
				      const char            *include_files,
				      const char            *exclude_files,
				      gboolean               ignorecase,
				      GCancellable          *cancellable,
				      ListReadyCallback      done_func,
				      gpointer               done_data);
void   g_list_items_async            (GList                 *items,
				      const char            *base_dir,
				      GCancellable          *cancellable,
				      ListReadyCallback      done_func,
				      gpointer               done_data);
				      
/* asynchronous recursive copy functions */				      
				      				  
void   g_copy_files_async            (GList                 *sources,
				      GList                 *destinations,
				      GFileCopyFlags         flags,
				      int                    io_priority,
				      GCancellable          *cancellable,
				      CopyProgressCallback   progress_callback,
				      gpointer               progress_callback_data,
				      CopyDoneCallback       callback,
				      gpointer               user_data);
void   g_copy_uris_async             (GList                 *sources,
				      GList                 *destinations,
				      GFileCopyFlags         flags,
				      int                    io_priority,
				      GCancellable          *cancellable,
				      CopyProgressCallback   progress_callback,
				      gpointer               progress_callback_data,
				      CopyDoneCallback       callback,
				      gpointer               user_data);		  
void   g_directory_copy_async        (GFile                 *source,
				      GFile                 *destination,
				      GFileCopyFlags         flags,
				      int                    io_priority,
				      GCancellable          *cancellable,
				      CopyProgressCallback   progress_callback,
				      gpointer               progress_callback_data,
				      CopyDoneCallback       callback,
				      gpointer               user_data);

/* convenience macros */
/**
 * g_directory_list_all_async:
 * @directory: 
 * @base_dir: 
 * @recursive:
 * @cancellable:
 * @done_func:
 * @done_data:
 * 
 * Inserts a #GNode as the last child of the given parent.
 *
 */
#define g_directory_list_all_async(directory, \
				   base_dir, \
				   recursive, \
				   cancellable, \
				   done_func, \
				   done_data)	g_directory_list_async ((directory), (base_dir), (recursive), TRUE, FALSE, FALSE, NULL, NULL, FALSE, (cancellable), (done_func), (done_data))

#endif /* _GIO_UTILS_H */

