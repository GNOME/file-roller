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

#ifndef FILE_DATA_H
#define FILE_DATA_H

#include <glib.h>
#include <glib-object.h>
#include <time.h>

typedef struct {
	char       *original_path;    /* path read from command line. */
	char       *full_path;        /* "/" + original_path. */
	char       *link;
	guint64     size;
	time_t      modified;

	char       *name;             /* The file name. */
	char       *path;             /* The directory. */
	gboolean    encrypted;        /* whether the file is encrypted. */

	/* Additional data. */

	gboolean    is_dir;           /* Whether this entry is used to show
				       * a directory. */
	char       *list_name;        /* The string visualized in the list
				       * view. */

	/* Private data */

	guint       mime_type;
} FileData;

#define FR_TYPE_FILE_DATA (file_data_get_type ())

GType           file_data_get_type                  (void);
FileData *      file_data_new                       (void);
FileData *      file_data_copy                      (FileData *src);
void            file_data_free                      (FileData *fdata);
const char *    file_data_get_mime_type             (const FileData *fdata);
const char *    file_data_get_mime_type_description (const FileData *fdata);

void            file_data_release_data (void);

#endif /* FILE_DATA_H */
