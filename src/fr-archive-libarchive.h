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

#define FR_TYPE_ARCHIVE_LIBARCHIVE (fr_archive_libarchive_get_type ())
G_DECLARE_FINAL_TYPE (FrArchiveLibarchive, fr_archive_libarchive, FR, ARCHIVE_LIBARCHIVE, FrArchive)

struct _FrArchiveLibarchive {
	FrArchive __parent;
};

#endif /* FR_ARCHIVE_LIBARCHIVE_H */
