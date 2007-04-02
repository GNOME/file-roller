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

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <glib.h>
#include "fr-process.h"
#include "fr-command.h"
#include "file-list.h"

#define FR_TYPE_ARCHIVE            (fr_archive_get_type ())
#define FR_ARCHIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_ARCHIVE, FRArchive))
#define FR_ARCHIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_ARCHIVE, FRArchiveClass))
#define FR_IS_ARCHIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_ARCHIVE))
#define FR_IS_ARCHIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_ARCHIVE))
#define FR_ARCHIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_ARCHIVE, FRArchiveClass))

typedef struct _FRArchive       FRArchive;
typedef struct _FRArchiveClass  FRArchiveClass;

typedef gboolean (*FakeLoadFunc) (FRArchive *archive, gpointer data);

struct _FRArchive {
	GObject  __parent;

	char           *uri;
	gboolean        is_remote;
	char           *local_filename;
	char           *mime_type;
	FRCommand      *command;
	FRProcess      *process;
	FRProcError     error;
	gboolean        is_compressed_file;            /* Whether the file is an archive
							* or a compressed file. */
	gboolean        read_only;                     /* Whether archive is read-only
							* or not. */
	gboolean        can_create_compressed_file;
	FakeLoadFunc    fake_load_func;                /* If returns TRUE, archives are not read when
							* fr_archive_load is invoked, used
							* in batch mode. */
	gpointer        fake_load_data;
	FakeLoadFunc    add_is_stoppable_func;         /* Returns whether the add operation is
							* stoppable. */
	gpointer        add_is_stoppable_data;

	GnomeVFSAsyncHandle *xfer_handle;
};

struct _FRArchiveClass {
	GObjectClass __parent_class;

	/* -- Signals -- */

	void (*start)     (FRArchive   *archive,
			   FRAction     action); 
	void (*done)      (FRArchive   *archive,
			   FRAction     action, 
			   FRProcError *error);
	void (*progress)  (FRArchive   *archive,
			   double       fraction);
	void (*message)   (FRArchive   *archive,
			   const char  *msg);
	void (*stoppable) (FRArchive   *archive,
			   gboolean     value);
};

GType            fr_archive_get_type           (void);
FRArchive *      fr_archive_new                (void);
void             fr_archive_free               (FRArchive     *archive);
gboolean         fr_archive_new_file           (FRArchive     *archive,
						const char    *uri);
gboolean         fr_archive_load               (FRArchive     *archive,
						const char    *uri,
						const char    *password);
void             fr_archive_reload             (FRArchive     *archive,
						const char    *password);
void             fr_archive_rename             (FRArchive     *archive,
						const char    *new_uri);
void             fr_archive_add                (FRArchive     *archive,
						GList         *file_list,
						const char    *base_dir,
						const char    *dest_dir,
						gboolean       update,
						const char    *password,
						FRCompression  compression);
VisitDirHandle * fr_archive_add_with_wildcard  (FRArchive     *archive,
						const char    *include_files,
						const char    *exclude_files,
						const char    *base_dir,
						const char    *dest_dir,
						gboolean       update,
						gboolean       recursive,
						gboolean       follow_links,
						const char    *password,
						FRCompression  compression,
						DoneFunc       done_func,
						gpointer       done_data);
VisitDirHandle * fr_archive_add_directory      (FRArchive     *archive,
						const char    *directory,
						const char    *base_dir,
						const char    *dest_dir,
						gboolean       update,
						const char    *password,
						FRCompression  compression,
						DoneFunc       done_func,
						gpointer       done_data);
VisitDirHandle * fr_archive_add_items          (FRArchive     *archive,
						GList         *item_list,
						const char    *base_dir,
						const char    *dest_dir,
						gboolean       update,
						const char    *password,
						FRCompression  compression,
						DoneFunc       done_func,
						gpointer       done_data);
void             fr_archive_remove             (FRArchive     *archive,
						GList         *file_list,
						FRCompression  compression);
void             fr_archive_extract            (FRArchive     *archive,
						GList         *file_list,
						const char    *dest_dir,
						const char    *base_dir,
						gboolean       skip_older,
						gboolean       overwrite,
						gboolean       junk_path,
						const char    *password);
void             fr_archive_test               (FRArchive     *archive,
						const char    *password);
void             fr_archive_set_fake_load_func (FRArchive     *archive,
						FakeLoadFunc   func,
						gpointer       data);
gboolean         fr_archive_fake_load          (FRArchive     *archive);
void             fr_archive_set_add_is_stoppable_func
					       (FRArchive     *archive,
						FakeLoadFunc   func,
						gpointer       data);
void             fr_archive_stoppable          (FRArchive     *archive,
						gboolean       stoppable);
void             fr_archive_stop	       (FRArchive     *archive);

/* utils */

G_CONST_RETURN char * fr_archive_utils__get_file_name_ext (const char *filename);
gboolean              fr_archive_utils__file_is_archive   (const char *filename);

#endif /* ARCHIVE_H */
