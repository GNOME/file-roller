/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2008 Free Software Foundation, Inc.
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

#ifndef _GIO_UTILS_H
#define _GIO_UTILS_H

#include <glib.h>
#include <gio/gio.h>


typedef enum {
	DIR_OP_CONTINUE,
	DIR_OP_SKIP,
	DIR_OP_STOP
} DirOp;


typedef enum {
        FILE_LIST_DEFAULT = 0,
        FILE_LIST_RECURSIVE = 1 << 0,
        FILE_LIST_NO_FOLLOW_LINKS = 1 << 1,
        FILE_LIST_NO_BACKUP_FILES = 1 << 2,
        FILE_LIST_NO_HIDDEN_FILES = 1 << 3
} FileListFlags;


/* FileInfo */

typedef struct {
	GFile     *file;
	GFileInfo *info;
} FileInfo;

/* FileFilter */

typedef struct _FileFilter FileFilter;

FileFilter *  file_filter_new        (const char        *pattern);
void          file_filter_unref      (FileFilter        *filter);
gboolean      file_filter_matches    (FileFilter        *filter,
				      GFile             *file);
gboolean      file_filter_empty      (FileFilter        *filter);

/* callback types */

typedef gboolean  (*FilterMatchCallback)  (GFile     *file,
					   GFileInfo *info,
					   gpointer   user_data);
typedef DirOp (*StartDirCallback)    (GFile                 *directory,
				      GFileInfo             *info,
				      GError               **error,
				      gpointer               user_data);
typedef void (*ForEachChildCallback) (GFile                 *file,
				      GFileInfo             *info,
				      gpointer               user_data);
typedef void (*ForEachDoneCallback)  (GError                *error,
				      gpointer               data);
typedef void (*ListReadyCallback)    (GList                 *files,
				      GList                 *dirs,
				      GError                *error,
				      gpointer               user_data);
typedef void (*InfoReadyCallback)    (GList                 *files, /* FileInfo list */
				      GError                *error,
				      gpointer               user_data);
typedef void (*CopyProgressCallback) (goffset                current_file,
                                      goffset                total_files,
                                      GFile                 *source,
                                      GFile                 *destination,
                                      goffset                current_num_bytes,
                                      goffset                total_num_bytes,
                                      gpointer               user_data);
typedef void (*CopyDoneCallback)     (GError                *error,
				      gpointer               user_data);

/* asynchronous recursive list functions */

void   g_directory_foreach_child     (GFile                 *directory,
				      gboolean               recursive,
				      gboolean               follow_links,
				      const char            *attributes,
				      GCancellable          *cancellable,
				      StartDirCallback       start_dir_func,
				      ForEachChildCallback   for_each_file_func,
				      ForEachDoneCallback    done_func,
				      gpointer               user_data);
void   _g_file_list_query_info_async (GList                 *file_list, /* GFile list */
				      FileListFlags          flags,
                     	     	      const char            *attributes,
                     	     	      GCancellable          *cancellable,
                     	     	      FilterMatchCallback    directory_filter_func,
                     	     	      FilterMatchCallback    file_filter_func,
                     	     	      InfoReadyCallback      ready_callback,
                     	     	      gpointer               user_data);

/* asynchronous copy functions */

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
void   g_copy_file_async             (GFile                 *source,
				      GFile                 *destination,
				      GFileCopyFlags         flags,
				      int                    io_priority,
				      GCancellable          *cancellable,
				      CopyProgressCallback   progress_callback,
				      gpointer               progress_callback_data,
				      CopyDoneCallback       callback,
				      gpointer               user_data);
void   g_copy_uri_async              (const char            *source,
				      const char            *destination,
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
gboolean g_load_file_in_buffer       (GFile                 *file,
				      void                  *buffer,
				      gsize                  size,
				      GError               **error);

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
 */
#define g_directory_list_all_async(directory, base_dir, recursive, cancellable, done_func, done_data) \
    g_directory_list_async ((directory), (base_dir), (recursive), TRUE, FALSE, FALSE, NULL, NULL, NULL, FALSE, (cancellable), (done_func), (done_data))

#endif /* _GIO_UTILS_H */
