/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001-2006 The Free Software Foundation, Inc.
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
#include <glib/gi18n.h>
#include <gio/gio.h>
#include "glib-utils.h"
#include "file-utils.h"
#include "fr-file-data.h"


G_DEFINE_BOXED_TYPE(FrFileData, fr_file_data, fr_file_data_copy, fr_file_data_free)


FrFileData *
fr_file_data_new (void)
{
	FrFileData *fdata;

	fdata = g_new0 (FrFileData, 1);
	fdata->content_type = NULL;
	fdata->free_original_path = FALSE;
	fdata->dir_size = 0;

	return fdata;
}


void
fr_file_data_free (FrFileData *fdata)
{
	if (fdata == NULL)
		return;
	if (fdata->free_original_path)
		g_free (fdata->original_path);
	g_free (fdata->full_path);
	g_free (fdata->name);
	g_free (fdata->path);
	g_free (fdata->content_type);
	g_free (fdata->link);
	g_free (fdata->list_name);
	g_free (fdata->sort_key);
	g_free (fdata);
}


FrFileData *
fr_file_data_copy (FrFileData *src)
{
	FrFileData *fdata;

	fdata = g_new0 (FrFileData, 1);

	fdata->original_path = g_strdup (src->original_path);
	fdata->free_original_path = TRUE;

	fdata->full_path = g_strdup (src->full_path);
	fdata->link = g_strdup (src->link);
	fdata->size = src->size;
	fdata->modified = src->modified;
	fdata->name = g_strdup (src->name);
	fdata->path = g_strdup (src->path);
	fdata->content_type = g_strdup (src->content_type);
	fdata->encrypted = src->encrypted;
	fdata->dir = src->dir;
	fdata->dir_size = src->dir_size;

	fdata->list_dir = src->list_dir;
	fdata->list_name = g_strdup (src->list_name);
	fdata->sort_key = g_strdup (src->sort_key);

	return fdata;
}


void
fr_file_data_update_content_type (FrFileData *fdata)
{
	g_free (fdata->content_type);

	if (fdata->dir)
		fdata->content_type = g_strdup (MIME_TYPE_DIRECTORY);
	else
		fdata->content_type = g_content_type_guess (fdata->full_path, NULL, 0, NULL);
}


gboolean
fr_file_data_is_dir (FrFileData *fdata)
{
	return fdata->dir || fdata->list_dir;
}


void
fr_file_data_set_list_name (FrFileData *fdata,
			 const char *value)
{
	g_free (fdata->list_name);
	fdata->list_name = g_strdup (value);

	g_free (fdata->sort_key);
	if (fdata->list_name != NULL)
		fdata->sort_key = g_utf8_collate_key_for_filename (fdata->list_name, -1);
	else
		fdata->sort_key = NULL;
}


int
fr_file_data_compare_by_path (gconstpointer a,
			   gconstpointer b)
{
	FrFileData *data_a = *((FrFileData **) a);
	FrFileData *data_b = *((FrFileData **) b);

	return strcmp (data_a->full_path, data_b->full_path);
}


int
fr_find_path_in_file_data_array (GPtrArray  *array,
			      const char *path)
{
	size_t    path_l;
	int       left, right, p, cmp = -1;
	FrFileData *fd;

	if (path == NULL)
		return -1;

	path_l = strlen (path);
	left = 0;
	right = array->len;
	while (left < right) {
		p = left + ((right - left) / 2);
		fd = (FrFileData *) g_ptr_array_index (array, p);

		cmp = strcmp (path, fd->original_path);
		if (cmp != 0) {
			/* consider '/path/to/dir' and '/path/to/dir/' the same path */

			size_t original_path_l = strlen (fd->original_path);
			if ((path_l == original_path_l - 1) && (fd->original_path[original_path_l - 1] == '/')) {
				int cmp2 = strncmp (path, fd->original_path, original_path_l - 1);
				if (cmp2 == 0)
					cmp = cmp2;
			}
		}

		if (cmp == 0)
			return p;
		else if (cmp < 0)
			right = p;
		else
			left = p + 1;
	}

	return -1;
}
