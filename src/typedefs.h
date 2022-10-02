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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TYPEDEFS_H
#define TYPEDEFS_H

#include <glib.h>
#include <glib-object.h>

typedef enum {
	FR_CLIPBOARD_OP_CUT,
	FR_CLIPBOARD_OP_COPY
} FrClipboardOp;

typedef enum { /*< skip >*/
	FR_WINDOW_SORT_BY_NAME = 0,
	FR_WINDOW_SORT_BY_SIZE = 1,
	FR_WINDOW_SORT_BY_TYPE = 2,
	FR_WINDOW_SORT_BY_TIME = 3,
	FR_WINDOW_SORT_BY_PATH = 4
} FrWindowSortMethod;

typedef enum {
	FR_WINDOW_LIST_MODE_FLAT,
	FR_WINDOW_LIST_MODE_AS_DIR
} FrWindowListMode;

typedef enum {
	FR_COMPRESSION_VERY_FAST,
	FR_COMPRESSION_FAST,
	FR_COMPRESSION_NORMAL,
	FR_COMPRESSION_MAXIMUM
} FrCompression;

typedef enum {
	FR_OVERWRITE_YES,
	FR_OVERWRITE_NO,
	FR_OVERWRITE_ASK
} FrOverwrite;

typedef enum { /*< skip >*/
	FR_ARCHIVE_CAN_DO_NOTHING = 0,
	FR_ARCHIVE_CAN_READ = 1 << 0,
	FR_ARCHIVE_CAN_WRITE = 1 << 1,
	FR_ARCHIVE_CAN_STORE_MANY_FILES = 1 << 2,
	FR_ARCHIVE_CAN_ENCRYPT = 1 << 3,
	FR_ARCHIVE_CAN_ENCRYPT_HEADER = 1 << 4,
	FR_ARCHIVE_CAN_CREATE_VOLUMES = 1 << 5
} FrArchiveCaps;

#define FR_ARCHIVE_CAN_READ_WRITE (FR_ARCHIVE_CAN_READ | FR_ARCHIVE_CAN_WRITE)

typedef struct {
	const char    *mime_type;
	FrArchiveCaps  current_capabilities;
	FrArchiveCaps  potential_capabilities;
} FrMimeTypeCap;

typedef struct {
	const char *mime_type;
	const char *packages;
} FrMimeTypePackages;

typedef struct {
	int        ref;
	GType      type;
	GPtrArray *caps;  /* array of FrMimeTypeCap */
	GPtrArray *packages;  /* array of FrMimeTypePackages */
} FrRegisteredArchive;

typedef struct {
	const char    *mime_type;
	char          *default_ext;
	FrArchiveCaps  capabilities;
} FrMimeTypeDescription;

typedef struct {
	char       *ext;
	const char *mime_type;
} FrExtensionType;

typedef enum {
	FR_FILE_SELECTOR_MODE_FILES,
	FR_FILE_SELECTOR_MODE_FOLDER
} FrFileSelectorMode;

#endif /* TYPEDEFS_H */
