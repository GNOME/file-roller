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

#ifdef HAVE_FNMATCH
#include <fnmatch.h>
#endif /* HAVE_FNMATCH */
#include <string.h>
#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include "file-utils.h"

#ifndef HAVE_FNMATCH
#include "fnmatch.h"
#endif /* ! HAVE_FNMATCH */

typedef enum {
        GNOME_VFS_DIRECTORY_FILTER_DEFAULT = 0,
        GNOME_VFS_DIRECTORY_FILTER_NODOTFILES = 1 << 1,
        GNOME_VFS_DIRECTORY_FILTER_IGNORECASE = 1 << 2,
        GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES = 1 << 3
} GnomeVFSDirectoryFilterOptions;

typedef struct {
	char                           **patterns;
	int                              fnmatch_flags;
	GnomeVFSDirectoryFilterOptions   options;
} Filter;


static Filter * 
filter_new (const gchar                    *pattern, 
	    GnomeVFSDirectoryFilterOptions  options)
{
	Filter *new;

        new = g_new (Filter, 1);

        new->patterns = search_util_get_patterns (pattern);
        new->fnmatch_flags = 0;
	new->options = options;
        if (options & GNOME_VFS_DIRECTORY_FILTER_IGNORECASE)
                new->fnmatch_flags |= FNM_CASEFOLD;

        return new;
}


static void
filter_destroy (Filter *filter)
{
        g_return_if_fail (filter != NULL);

	if (filter->patterns != NULL)
		g_strfreev (filter->patterns);
        g_free (filter);
}


static gboolean
filter_apply (Filter     *filter,
	      const char *name)
{
	const char *file_name;

        g_return_val_if_fail (filter != NULL, FALSE);
        g_return_val_if_fail (name != NULL, FALSE);

	file_name = file_name_from_path (name);

	if ((filter->options & GNOME_VFS_DIRECTORY_FILTER_NODOTFILES)
	    && ((name[0] == '.') || (strstr (name, "/.") != NULL)))
		return FALSE;

	if ((filter->options & GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES)
	    && (name[strlen (name) - 1] == '~'))
		return FALSE;

        return match_patterns (filter->patterns, file_name);
}


static gboolean
filter_apply_from_info (Filter           *filter,
			GnomeVFSFileInfo *info)
{
	g_return_val_if_fail (info != NULL, FALSE);
	return filter_apply (filter, info->name);
}


typedef struct {
	gchar       *start_from;
	GnomeVFSURI *uri;
	GList       *files;
	Filter      *filter;
} WCSearchData;


static WCSearchData *
wc_search_data_new (const gchar *directory)
{
	WCSearchData *data;
	gchar        *escaped;

	data = g_new (WCSearchData, 1);

	data->start_from = g_strdup (directory);

	escaped = gnome_vfs_escape_path_string (directory);
	data->uri = gnome_vfs_uri_new (escaped);
	g_free (escaped);

	data->files = NULL;
	data->filter = NULL;

	return data;
}


static void
wc_search_data_free (WCSearchData *data)
{
	if (data == NULL)
		return;

	if (data->start_from) {
		g_free (data->start_from);
		data->start_from = NULL;
	}
	
	if (data->uri != NULL)
		gnome_vfs_uri_unref (data->uri);

	g_free (data);
}


static void
wc_add_file (GnomeVFSFileInfo *info,
	     WCSearchData     *data)
{
	if ((info->type != GNOME_VFS_FILE_TYPE_DIRECTORY)
	    && filter_apply_from_info (data->filter, info)) 
		data->files = g_list_prepend (data->files, 
					      g_strdup (info->name));
}


static gboolean 
wc_visit_cb (const gchar      *rel_path,
	     GnomeVFSFileInfo *info,
	     gboolean          recursing_will_loop,
	     gpointer          callback_data,
	     gboolean         *recurse)
{
	WCSearchData *data = callback_data;

	if ((info->type != GNOME_VFS_FILE_TYPE_DIRECTORY)
	    && filter_apply (data->filter, rel_path)) 
		data->files = g_list_prepend (data->files, 
					      g_strdup (rel_path));

	*recurse = ! recursing_will_loop;

	return TRUE;
}


GList *
get_wildcard_file_list (const char  *directory, 
			const char  *filter_pattern, 
			gboolean     recursive,
			gboolean     follow_links,
			gboolean     same_fs,
			gboolean     no_backup_files,
			gboolean     no_dot_files,
			gboolean     ignorecase)
{
	WCSearchData                   *data;
	GnomeVFSDirectoryFilterOptions  filter_options;
	GnomeVFSResult                  result;
	GnomeVFSFileInfoOptions         info_options; 
	GnomeVFSDirectoryVisitOptions   visit_options;
	GList                          *list = NULL;

	if ((directory == NULL) || (filter_pattern == NULL))
                return NULL;

	data = wc_search_data_new (directory);

	/* file filter */

	filter_options = GNOME_VFS_DIRECTORY_FILTER_DEFAULT;
	if (no_backup_files)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES;
	if (no_dot_files)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NODOTFILES;
	if (ignorecase)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_IGNORECASE;

	data->filter = filter_new (filter_pattern, filter_options);

	/* info options */

	info_options = GNOME_VFS_FILE_INFO_DEFAULT;
	if (follow_links)
		info_options |= GNOME_VFS_FILE_INFO_FOLLOW_LINKS;

	if (! recursive) { 	/* non recursive case */
		GList *info_list;
		result = gnome_vfs_directory_list_load (&info_list,
							directory,
							info_options);
		if (result != GNOME_VFS_OK)
			list = NULL;
		else {
			g_list_foreach (info_list, (GFunc) wc_add_file, data);
			list = data->files;
			gnome_vfs_file_info_list_free (info_list);
		}
	} else { 		/* recursive case */
		/* visit options. */
		
		visit_options =  GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK;
		if (same_fs)
			visit_options |= GNOME_VFS_DIRECTORY_VISIT_SAMEFS;
		
		result = gnome_vfs_directory_visit_uri (data->uri,
							info_options,
							visit_options,
							wc_visit_cb,
							data);
		if (result != GNOME_VFS_OK) {
			path_list_free (data->files);
			list = NULL;
		} else
			list = data->files;
	}

	filter_destroy (data->filter);
	wc_search_data_free (data);

	return list;
}


/* -- get_directory_file_list -- */


typedef struct {
	gchar       *directory;
	gchar       *base_dir;
	GnomeVFSURI *uri;
	GList       *files;
	Filter      *filter;
} DirSearchData;


static DirSearchData *
dir_search_data_new (const char *base_dir,
		     const char *directory)
{
	DirSearchData *data;
	char          *escaped;
	char          *full_path;

	data = g_new (DirSearchData, 1);

	data->directory = g_strdup (directory);
	data->base_dir = g_strdup (base_dir);

	full_path = g_strconcat (base_dir, "/", directory, NULL);
	escaped = gnome_vfs_escape_path_string (full_path);
	data->uri = gnome_vfs_uri_new (escaped);
	g_free (escaped);
	g_free (full_path);

	data->files = NULL;
	data->filter = NULL;

	return data;
}


static void
dir_search_data_free (DirSearchData *data)
{
	if (data == NULL)
		return;

	if (data->directory) {
		g_free (data->directory);
		data->directory = NULL;
	}

	if (data->base_dir) {
		g_free (data->base_dir);
		data->base_dir = NULL;
	}
	
	if (data->uri != NULL)
		gnome_vfs_uri_unref (data->uri);

	g_free (data);
}


static gboolean 
dir_visit_cb (const gchar      *rel_path,
	      GnomeVFSFileInfo *info,
	      gboolean          recursing_will_loop,
	      gpointer          callback_data,
	      gboolean         *recurse)
{
	DirSearchData *data = callback_data;

	if ((info->type != GNOME_VFS_FILE_TYPE_DIRECTORY)
	    && filter_apply (data->filter, rel_path))
		data->files = g_list_prepend (data->files, 
					      g_strconcat (data->directory,
							   "/",
							   rel_path,
							   NULL));

	*recurse = ! recursing_will_loop;

	return TRUE;
}


GList *
get_directory_file_list (const char *directory,
			 const char *base_dir)
{
	DirSearchData *                data;
	GnomeVFSDirectoryFilterOptions filter_options;
	GnomeVFSResult                 result;
	GnomeVFSFileInfoOptions        info_options; 
	GnomeVFSDirectoryVisitOptions  visit_options;
	GList *                        list = NULL;

	data = dir_search_data_new (base_dir, directory);

	/* file filter */

	filter_options = (GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES
			  | GNOME_VFS_DIRECTORY_FILTER_NODOTFILES);
	data->filter = filter_new ("*", filter_options);

	/* options. */

	info_options = GNOME_VFS_FILE_INFO_FOLLOW_LINKS;
	visit_options = (GNOME_VFS_DIRECTORY_VISIT_LOOPCHECK
			 | GNOME_VFS_DIRECTORY_VISIT_SAMEFS);

	result = gnome_vfs_directory_visit_uri (data->uri,
						info_options,
						visit_options,
						dir_visit_cb,
						data);

	filter_destroy (data->filter);
	list = data->files;
	dir_search_data_free (data);

	return list;
}
