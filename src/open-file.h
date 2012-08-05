/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001-2008 The Free Software Foundation, Inc.
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

#ifndef OPEN_FILE_H
#define OPEN_FILE_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <time.h>

typedef struct {
	GFile        *extracted_file;
	GFile        *temp_dir;
	time_t        last_modified;
	GFileMonitor *monitor;
} OpenFile;

#define FR_TYPE_OPEN_FILE (open_file_get_type ())

GType       open_file_get_type (void);
OpenFile *  open_file_new      (const char *original_path,
			        GFile      *extracted_file,
			        GFile      *temp_dir);
OpenFile *  open_file_copy     (OpenFile   *src);
void        open_file_free     (OpenFile   *ofile);

#endif /* OPEN_FILE_H */
