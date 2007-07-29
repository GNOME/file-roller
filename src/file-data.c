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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "file-data.h"
#include "file-utils.h"

#define DESCRIPTION_UNKNOWN _("Unknown type")
#define DESCRIPTION_SYMLINK _("Symbolic link")


static GHashTable *mime_type_hash = NULL;


FileData *
file_data_new (void)
{
	FileData *fdata;

	fdata = g_new0 (FileData, 1);
	fdata->mime_type = 0;
	fdata->free_original_path = FALSE;
	fdata->dir_size = 0;
	
	if (mime_type_hash == NULL)
		mime_type_hash = g_hash_table_new_full (g_int_hash, 
							g_int_equal,
							NULL,
							(GDestroyNotify) g_free);

	return fdata;
}


void
file_data_release_data (void)
{
	if (mime_type_hash != NULL)
		g_hash_table_destroy (mime_type_hash);
}


void
file_data_free (FileData *fdata)
{
	if (fdata->free_original_path)
		g_free (fdata->original_path);
	g_free (fdata->full_path);
	g_free (fdata->name);
	g_free (fdata->path);
	g_free (fdata->link);
	g_free (fdata->list_name);
	g_free (fdata);
}


FileData *
file_data_copy (FileData *src)
{
	FileData *fdata;

	fdata = g_new0 (FileData, 1);

	fdata->original_path = g_strdup (src->original_path);
	fdata->free_original_path = TRUE;
	
	fdata->full_path = g_strdup (src->full_path);
	fdata->link = g_strdup (src->link);
	fdata->size = src->size;
	fdata->modified = src->modified;
	fdata->name = g_strdup (src->name);
	fdata->path = g_strdup (src->path);
	fdata->mime_type = src->mime_type;
	fdata->encrypted = src->encrypted;
	fdata->dir = src->dir;
	fdata->dir_size = src->dir_size;

	fdata->list_dir = src->list_dir;
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


static void
file_data_update_mime_type (FileData *fdata)
{
	const char *mime_type;

	mime_type = get_file_mime_type (fdata->full_path, TRUE);

	if (mime_type == NULL) {
		fdata->mime_type = 0;
		return;
	}

	fdata->mime_type = g_str_hash ((gconstpointer) mime_type);
	if (g_hash_table_lookup (mime_type_hash, (gconstpointer) &fdata->mime_type) == NULL)
		g_hash_table_insert (mime_type_hash, (gpointer) &fdata->mime_type, g_strdup (mime_type));
}


const char *
file_data_get_mime_type (const FileData *fdata)
{
	const char *mime_type;

	if (fdata->mime_type == 0)
		file_data_update_mime_type ((FileData*)fdata);
	if (fdata->mime_type == 0)
		return GNOME_VFS_MIME_TYPE_UNKNOWN;

	mime_type = g_hash_table_lookup (mime_type_hash, (gconstpointer) &fdata->mime_type);
	if (mime_type == NULL)
		mime_type = GNOME_VFS_MIME_TYPE_UNKNOWN;

	return mime_type;
}


const char *
file_data_get_mime_type_description (const FileData *fdata)
{
	const char *desc;

	if (fdata->link != NULL)
		return DESCRIPTION_SYMLINK;

	desc = gnome_vfs_mime_get_description (file_data_get_mime_type (fdata));
	if (desc == NULL)
		desc = DESCRIPTION_UNKNOWN;

	return desc;
}


gboolean
file_data_is_dir (const FileData *fdata)
{
	return fdata->dir || fdata->list_dir;
}
