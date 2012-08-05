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

#include <config.h>
#include "open-file.h"
#include "file-utils.h"
#include "glib-utils.h"


OpenFile *
open_file_new (const char *original_path,
	       GFile      *extracted_file,
	       GFile      *temp_dir)
{
	OpenFile *ofile;

	ofile = g_new0 (OpenFile, 1);
	ofile->extracted_file = g_object_ref (extracted_file);
	if (! g_file_query_exists (ofile->extracted_file, NULL)) {
		open_file_free (ofile);
		return NULL;
	}
	ofile->temp_dir = g_object_ref (temp_dir);
	ofile->last_modified = _g_file_get_file_mtime (ofile->extracted_file);

	return ofile;
}


void
open_file_free (OpenFile *ofile)
{
	if (ofile == NULL)
		return;
	if (ofile->monitor != NULL)
		g_object_unref (ofile->monitor);
	_g_object_unref (ofile->extracted_file);
	_g_object_unref (ofile->temp_dir);
	g_free (ofile);
}


OpenFile *
open_file_copy (OpenFile *src)
{
	OpenFile *ofile;

	ofile = g_new0 (OpenFile, 1);
	ofile->extracted_file = g_object_ref (src->extracted_file);
	ofile->temp_dir = g_object_ref (src->temp_dir);
	ofile->last_modified = src->last_modified;

	return ofile;
}


G_DEFINE_BOXED_TYPE (OpenFile, open_file, open_file_copy, open_file_free)
