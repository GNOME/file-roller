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

#include "file-data.h"


FileData *
file_data_new ()
{
	return g_new0 (FileData, 1);
}


void
file_data_free (FileData *fdata)
{
	if (fdata->full_path)
		g_free (fdata->full_path);
	if (fdata->name)
		g_free (fdata->name);
	if (fdata->path)
		g_free (fdata->path);
	if (fdata->link)
		g_free (fdata->link);

	if (fdata->list_name)
		g_free (fdata->list_name);
	
	g_free (fdata);
}


FileData *
file_data_copy (FileData *src)
{
	FileData *fdata;

	fdata = g_new (FileData, 1);

	fdata->original_path = g_strdup (src->original_path);
	fdata->full_path = g_strdup (src->full_path);
	fdata->link = g_strdup (src->link);
	fdata->size = src->size;
	fdata->modified = src->modified;
	fdata->name = g_strdup (src->name);
	fdata->path = g_strdup (src->path);
	fdata->type = src->type;

	fdata->is_dir = src->is_dir;
	fdata->list_name = g_strdup (src->list_name);

	return fdata;
}


GType
file_data_get_type (void)
{
	static GType type = 0;
  
	if (type == 0)
		type = g_boxed_type_register_static ("FRFileData", (GBoxedCopyFunc) file_data_copy, (GBoxedFreeFunc) file_data_free);
  
	return type;
}

