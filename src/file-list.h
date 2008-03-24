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

#ifndef _FILE_LIST_H
#define _FILE_LIST_H

#include <glib.h>
#include <gio/gio.h>

typedef void (*DoneFunc) (gpointer data);

typedef struct _VisitDirData   VisitDirData;
typedef struct _VisitDirHandle VisitDirHandle;

typedef void (*VisitDirDoneFunc) (GList *files, GList *dirs, GError *error, gpointer data);

void                visit_dir_handle_free           (VisitDirHandle   *handle);
VisitDirHandle *    visit_dir_async                 (VisitDirHandle   *handle,
						     const char       *directory, 
						     const char       *filter_pattern, 
						     gboolean          recursive,
						     gboolean          follow_links,
						     gboolean          same_fs,
						     gboolean          no_backup_files,
						     gboolean          no_dot_files,
						     gboolean          ignorecase,
						     VisitDirDoneFunc  done_func,
						     gpointer          done_data);
void                visit_dir_async_interrupt       (VisitDirHandle   *handle,
						     DoneFunc          f,
						     gpointer          data);
VisitDirHandle *    get_wildcard_file_list_async    (const char       *directory, 
						     const char       *filter_pattern, 
						     gboolean          recursive,
						     gboolean          follow_links,
						     gboolean          same_fs,
						     gboolean          no_backup_files,
						     gboolean          no_dot_files,
						     gboolean          ignorecase,
						     VisitDirDoneFunc  done_func,
						     gpointer          done_data);
VisitDirHandle *    get_directory_file_list_async   (const char       *directory,
						     const char       *base_dir,
						     VisitDirDoneFunc  done_func,
						     gpointer          done_data); 
VisitDirHandle *    get_items_file_list_async       (GList            *item_list,
						     const char       *base_dir,
						     VisitDirDoneFunc  done_func,
						     gpointer          done_data);

/**/

typedef void        (*FilesProgressCallback)     (goffset    current_file,
                                                  goffset    total_files,
                                                  GFile     *source,
                                                  GFile     *destination,
                                                  goffset    current_num_bytes,
                                                  goffset    total_num_bytes,
                                                  gpointer   user_data);
typedef void        (*FilesDoneCallback)         (GError    *error,
						  gpointer   user_data);
						  
void                gio_copy_files_async         (GList                 *sources,
						  GList                 *destinations,
						  GFileCopyFlags         flags,
						  int                    io_priority,
						  GCancellable          *cancellable,
						  FilesProgressCallback  progress_callback,
						  gpointer               progress_callback_data,
						  FilesDoneCallback      callback,
						  gpointer               user_data);
void                gio_copy_uris_async          (GList                 *sources,
						  GList                 *destinations,
						  GFileCopyFlags         flags,
						  int                    io_priority,
						  GCancellable          *cancellable,
						  FilesProgressCallback  progress_callback,
						  gpointer               progress_callback_data,
						  FilesDoneCallback      callback,
						  gpointer               user_data);		  
void                gio_copy_directory_async     (GFile                 *source,
						  GFile                 *destination,
						  GFileCopyFlags         flags,
						  int                    io_priority,
						  GCancellable          *cancellable,
						  FilesProgressCallback  progress_callback,
						  gpointer               progress_callback_data,
						  FilesDoneCallback      callback,
						  gpointer               user_data);

#endif /* _FILE_LIST_H */
