/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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

#ifndef FR_ARCHIVE_LIBARCHIVE_H
#define FR_ARCHIVE_LIBARCHIVE_H

#include <glib.h>
#include "fr-archive.h"

#define FR_TYPE_ARCHIVE_LIBARCHIVE            (fr_archive_libarchive_get_type ())
#define FR_ARCHIVE_LIBARCHIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_ARCHIVE_LIBARCHIVE, FrArchiveLibarchive))
#define FR_ARCHIVE_LIBARCHIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_ARCHIVE_LIBARCHIVE, FrArchiveLibarchiveClass))
#define FR_IS_ARCHIVE_LIBARCHIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_ARCHIVE_LIBARCHIVE))
#define FR_IS_ARCHIVE_LIBARCHIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_ARCHIVE_LIBARCHIVE))
#define FR_ARCHIVE_LIBARCHIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_ARCHIVE_LIBARCHIVE, FrArchiveLibarchiveClass))

typedef struct _FrArchiveLibarchive        FrArchiveLibarchive;
typedef struct _FrArchiveLibarchiveClass   FrArchiveLibarchiveClass;
typedef struct _FrArchiveLibarchivePrivate FrArchiveLibarchivePrivate;

struct _FrArchiveLibarchive {
	FrArchive  __parent;
	FrArchiveLibarchivePrivate *priv;
};

struct _FrArchiveLibarchiveClass {
	FrArchiveClass __parent_class;
};

GType  fr_archive_libarchive_get_type  (void);

#endif /* FR_ARCHIVE_LIBARCHIVE_H */
