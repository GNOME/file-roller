/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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

#include <string.h>
#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "file-list.h"
#include "utf8-fnmatch.h"


#define SPECIAL_DIR(x) (! strcmp (x, "..") || ! strcmp (x, "."))


/* -- filter -- */


typedef enum {
	GNOME_VFS_DIRECTORY_FILTER_DEFAULT = 0,
	GNOME_VFS_DIRECTORY_FILTER_NODOTFILES = 1 << 1,
	GNOME_VFS_DIRECTORY_FILTER_IGNORECASE = 1 << 2,
	GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES = 1 << 3
} GnomeVFSDirectoryFilterOptions;

typedef struct {
	char                            *pattern;
	char                           **patterns;
	int                              fnmatch_flags;
	GnomeVFSDirectoryFilterOptions   options;
} Filter;


static Filter *
filter_new (const gchar                    *pattern,
	    GnomeVFSDirectoryFilterOptions  options)
{
	Filter *filter;

	filter = g_new (Filter, 1);

	filter->pattern = g_strdup (pattern);
	filter->patterns = search_util_get_patterns (pattern);
	filter->fnmatch_flags = 0;
	filter->options = options;
	if ((options & GNOME_VFS_DIRECTORY_FILTER_IGNORECASE) == GNOME_VFS_DIRECTORY_FILTER_IGNORECASE)
		filter->fnmatch_flags |= FNM_CASEFOLD;

	return filter;
}


static void
filter_set_options (Filter                         *filter,
		    GnomeVFSDirectoryFilterOptions  options)
{
	filter->options = options;
	if ((options & GNOME_VFS_DIRECTORY_FILTER_IGNORECASE) == GNOME_VFS_DIRECTORY_FILTER_IGNORECASE)
		filter->fnmatch_flags |= FNM_CASEFOLD;
}


static void
filter_destroy (Filter *filter)
{
	g_return_if_fail (filter != NULL);

	g_free (filter->pattern);
	if (filter->patterns != NULL)
		g_strfreev (filter->patterns);
	g_free (filter);
}


static gboolean
filter_apply (Filter     *filter,
	      const char *name)
{
	const char *file_name;
	char       *utf8_name;
	gboolean    retval;

	g_return_val_if_fail (filter != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	file_name = file_name_from_path (name);

	if ((filter->options & GNOME_VFS_DIRECTORY_FILTER_NODOTFILES)
	    && ((file_name[0] == '.') || (strstr (file_name, "/.") != NULL)))
		return FALSE;

	if ((filter->options & GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES)
	    && (file_name[strlen (file_name) - 1] == '~'))
		return FALSE;

	utf8_name = g_filename_to_utf8 (file_name, -1, NULL, NULL, NULL);
	retval = match_patterns (filter->patterns, utf8_name, filter->fnmatch_flags);
	g_free (utf8_name);

	return retval;
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
	    && filter_apply (data->filter, rel_path)) {
		data->files = g_list_prepend (data->files,
					      g_strdup (rel_path));
	}

	*recurse = ! recursing_will_loop;

	return TRUE;
}


static gboolean
pattern_present (char       **patterns,
		 const char  *pattern)
{
	gboolean found = FALSE;
	int      i;

	if ((patterns == NULL) || (patterns[0] == NULL))
		return FALSE;

	if (pattern == NULL)
		return FALSE;

	for (i = 0; (patterns[i] != NULL) && !found; i++)
		if (g_utf8_collate (patterns[i], pattern) == 0)
			found = TRUE;

	return found;
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

	data->filter = filter_new (filter_pattern, GNOME_VFS_DIRECTORY_FILTER_DEFAULT);

	filter_options = GNOME_VFS_DIRECTORY_FILTER_DEFAULT;
	if (no_backup_files && !pattern_present (data->filter->patterns, "*~"))
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES;
	if (no_dot_files && !pattern_present (data->filter->patterns, ".*"))
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NODOTFILES;
	if (ignorecase)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_IGNORECASE;
	filter_set_options (data->filter, filter_options);

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
	    && filter_apply (data->filter, rel_path)) {
		char *path = g_strconcat (data->directory,
					  "/",
					  rel_path,
					  NULL);
		data->files = g_list_prepend (data->files, path);
	}

	*recurse = ! recursing_will_loop;

	return TRUE;
}


GList *
get_directory_file_list (const char *directory,
			 const char *base_dir)
{
	DirSearchData *                data;
	GnomeVFSResult                 result;
	GnomeVFSFileInfoOptions        info_options;
	GnomeVFSDirectoryVisitOptions  visit_options;
	GList *                        list = NULL;

	data = dir_search_data_new (base_dir, directory);

	/* file filter */

	data->filter = filter_new ("*",
				   (GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES
				    | GNOME_VFS_DIRECTORY_FILTER_NODOTFILES));

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


/* -- path list async -- */


typedef struct _PathListData PathListData;
typedef void (*PathListDoneFunc) (PathListData *dld, gpointer data);


struct _PathListData {
	GnomeVFSURI      *uri;
	GnomeVFSResult    result;
	GList            *files;               /* char* items. */
	GList            *dirs;                /* char* items. */
	PathListDoneFunc  done_func;
	gpointer          done_data;
	DoneFunc          interrupt_func;
	gpointer          interrupt_data;
	gboolean          interrupted;
};


typedef struct {
	GnomeVFSAsyncHandle *vfs_handle;
	PathListData *pli_data;
} PathListHandle;


PathListData *      path_list_data_new           ();
void                path_list_data_free          (PathListData     *dli);
void                path_list_handle_free        (PathListHandle   *handle);
PathListHandle *    path_list_async_new          (const char       *uri,
						  gboolean          follow_links,
						  PathListDoneFunc  f,
						  gpointer          data);
void                path_list_async_interrupt    (PathListHandle   *handle,
						  DoneFunc          f,
						  gpointer          data);


PathListData *
path_list_data_new ()
{
	PathListData *pli;

	pli = g_new0 (PathListData, 1);

	pli->uri = NULL;
	pli->result = GNOME_VFS_OK;
	pli->files = NULL;
	pli->dirs = NULL;
	pli->done_func = NULL;
	pli->done_data = NULL;
	pli->interrupt_func = NULL;
	pli->interrupt_data = NULL;
	pli->interrupted = FALSE;

	return pli;
}


void
path_list_data_free (PathListData *pli)
{
	g_return_if_fail (pli != NULL);

	if (pli->uri != NULL)
		gnome_vfs_uri_unref (pli->uri);

	if (pli->files != NULL) {
		g_list_foreach (pli->files, (GFunc) g_free, NULL);
		g_list_free (pli->files);
	}

	if (pli->dirs != NULL) {
		g_list_foreach (pli->dirs, (GFunc) g_free, NULL);
		g_list_free (pli->dirs);
	}

	g_free (pli);
}


void
path_list_handle_free (PathListHandle *handle)
{
	if (handle->pli_data != NULL)
		path_list_data_free (handle->pli_data);
	g_free (handle);
}


static void
directory_load_cb (GnomeVFSAsyncHandle *handle,
		   GnomeVFSResult       result,
		   GList               *list,
		   guint                entries_read,
		   gpointer             data)
{
	PathListData *pli;
	GList        *node;

	pli = (PathListData *) data;
	pli->result = result;

	if (pli->interrupted) {
		if (pli->interrupt_func)
			pli->interrupt_func (pli->interrupt_data);
		path_list_data_free (pli);
		return;
	}

	for (node = list; node != NULL; node = node->next) {
		GnomeVFSFileInfo *info     = node->data;
		GnomeVFSURI      *full_uri = NULL;
		char             *str_uri;

		switch (info->type) {
		case GNOME_VFS_FILE_TYPE_REGULAR:
			full_uri = gnome_vfs_uri_append_file_name (pli->uri, info->name);
			str_uri = gnome_vfs_uri_to_string (full_uri, GNOME_VFS_URI_HIDE_NONE);
			pli->files = g_list_prepend (pli->files, str_uri);
			break;

		case GNOME_VFS_FILE_TYPE_DIRECTORY:
			if (SPECIAL_DIR (info->name))
				break;
			full_uri = gnome_vfs_uri_append_path (pli->uri, info->name);
			str_uri = gnome_vfs_uri_to_string (full_uri, GNOME_VFS_URI_HIDE_NONE);
			pli->dirs = g_list_prepend (pli->dirs,  str_uri);
			break;

		default:
			break;
		}

		if (full_uri != NULL)
			gnome_vfs_uri_unref (full_uri);
	}

	if ((result == GNOME_VFS_ERROR_EOF) || (result != GNOME_VFS_OK)) {
		if (pli->done_func)
			/* pli must be deallocated in pli->done_func */
			pli->done_func (pli, pli->done_data);
		else
			path_list_data_free (pli);

		return;
	}
}


PathListHandle *
path_list_async_new (const char       *uri,
		     gboolean          follow_links,
		     PathListDoneFunc  f,
		     gpointer          data)
{
	GnomeVFSAsyncHandle *handle = NULL;
	PathListData        *pli;
	PathListHandle      *pl_handle;

	pli = path_list_data_new ();
	pli->uri = gnome_vfs_uri_new (uri);
	pli->done_func = f;
	pli->done_data = data;

	gnome_vfs_async_load_directory_uri (
		&handle,
		pli->uri,
		follow_links ? GNOME_VFS_FILE_INFO_FOLLOW_LINKS: GNOME_VFS_FILE_INFO_DEFAULT,
		128 /* items_per_notification FIXME */,
		GNOME_VFS_PRIORITY_DEFAULT,
		directory_load_cb,
		pli);

	pl_handle = g_new (PathListHandle, 1);
	pl_handle->vfs_handle = handle;
	pl_handle->pli_data = pli;

	return pl_handle;
}


void
path_list_async_interrupt (PathListHandle   *handle,
			   DoneFunc          f,
			   gpointer          data)
{
	handle->pli_data->interrupted = TRUE;
	handle->pli_data->interrupt_func = f;
	handle->pli_data->interrupt_data = data;

	g_free (handle);
}


/* -- dir visit async -- */


struct _VisitDirData {
	char             *base_dir;
	char             *directory;
	gboolean          recursive;
	gboolean          follow_links;
	gboolean          same_fs;
	gboolean          include_directories;
	VisitDirDoneFunc  done_func;
	gpointer          done_data;
	DoneFunc          interrupt_func;
	gpointer          interrupt_data;
	gboolean          interrupted;

	GList            *dirs;
	GList            *files;

	/* private */

	Filter           *filter;
	GHashTable       *dirnames;
};


struct _VisitDirHandle {
	VisitDirData   *vdd_data;
};


static void
visit_dir_data_free (VisitDirData *vdd)
{
	if (vdd == NULL)
		return;

	g_free (vdd->base_dir);
	g_free (vdd->directory);

	if (vdd->dirs != NULL)
		path_list_free (vdd->dirs);
	if (vdd->files != NULL)
		path_list_free (vdd->files);

	if (vdd->filter)
		filter_destroy (vdd->filter);

	if (vdd->dirnames)
		g_hash_table_destroy (vdd->dirnames);

	g_free (vdd);
}


static void _visit_dir_async (const char *dir, VisitDirData *vdd);


static gboolean
same_fs (const char *path1,
	 const char *path2)
{
	GnomeVFSURI    *uri1, *uri2;
	GnomeVFSResult  result;
	gboolean        same;

	uri1 = gnome_vfs_uri_new (path1);
	uri2 = gnome_vfs_uri_new (path2);

	result = gnome_vfs_check_same_fs_uris (uri1, uri2, &same);

	gnome_vfs_uri_unref (uri1);
	gnome_vfs_uri_unref (uri2);

	return (result == GNOME_VFS_OK) && same;
}


static GList*
get_dir_list_from_file_list (VisitDirData *vdd,
			     GList        *files,
			     gboolean      is_dir_list)
{
	GList *scan;
	GList *dir_list = NULL;

	for (scan = files; scan; scan = scan->next) {
		char *filename = scan->data;
		char *dir_name;

		if (is_dir_list)
			dir_name = g_strdup (filename + strlen (vdd->base_dir) + 1);
		else
			dir_name = remove_level_from_path (filename + strlen (vdd->base_dir) + 1);

		while ((dir_name != NULL) && (dir_name[0] != '\0') && (strcmp (dir_name, "/") != 0)) {
			char *tmp;
			char *dir;

			/* avoid to insert duplicated folders */

			dir = g_strconcat (vdd->base_dir, "/", dir_name, NULL);
			if (g_hash_table_lookup (vdd->dirnames, dir) == NULL) {
				g_hash_table_insert (vdd->dirnames, dir, GINT_TO_POINTER (1));
				dir_list = g_list_prepend (dir_list, dir);
			} else
				g_free (dir);

			tmp = dir_name;
			dir_name = remove_level_from_path (tmp);
			g_free (tmp);
		}

		g_free (dir_name);
	}

	return dir_list;
}


static void
vd_path_list_done_cb (PathListData *pld,
		      gpointer      data)
{
	VisitDirData *vdd = data;
	GList        *scan;
	char         *sub_dir = NULL;

	if (vdd->interrupted) {
		if (vdd->interrupt_func)
			vdd->interrupt_func (vdd->interrupt_data);
		visit_dir_data_free (vdd);

		return;
	}

	if (pld->result != GNOME_VFS_ERROR_EOF) {
		char *path;

		path = gnome_vfs_uri_to_string (pld->uri, GNOME_VFS_URI_HIDE_NONE);
		g_warning ("Error reading directory %s.", path);
		g_free (path);

		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		visit_dir_data_free (vdd);

		return;
	}

	if (vdd->filter != NULL)
		for (scan = pld->files; scan; ) {
			char *path = scan->data;

			if (! filter_apply (vdd->filter, path)) {
				GList *next = scan->next;

				pld->files = g_list_remove_link (pld->files, scan);
				g_list_free (scan);
				g_free (path);

				scan = next;
			} else
				scan = scan->next;
		}

	if (vdd->include_directories)
		vdd->files = g_list_concat (get_dir_list_from_file_list (vdd, pld->files, FALSE), vdd->files);
	vdd->files = g_list_concat (pld->files, vdd->files);
	pld->files = NULL;

	if (vdd->include_directories)
		if (strcmp (vdd->filter->pattern, "*") == 0)
			vdd->files = g_list_concat (get_dir_list_from_file_list (vdd, pld->dirs, TRUE), vdd->files);

	if (! vdd->recursive) {
		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		path_list_data_free (pld);
		visit_dir_data_free (vdd);

		return;
	}

	vdd->dirs = g_list_concat (pld->dirs, vdd->dirs);
	pld->dirs = NULL;
	path_list_data_free (pld);

	if (vdd->dirs == NULL) {
		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		visit_dir_data_free (vdd);

		return;
	}

	while ((scan = vdd->dirs) != NULL) {
		sub_dir = (char*) scan->data;
		vdd->dirs = g_list_remove_link (vdd->dirs, scan);

		if (! vdd->same_fs || same_fs (vdd->directory, sub_dir)) {
			_visit_dir_async (sub_dir, vdd);
			break;
		} else {
			g_free (sub_dir);
			sub_dir = NULL;
		}
	}

	if (sub_dir == NULL) {
		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		visit_dir_data_free (vdd);

	} else
		g_free (sub_dir);
}


static void
_visit_dir_async (const char   *dir,
		  VisitDirData *vdd)
{
	PathListHandle *handle;
	handle = path_list_async_new (dir,
				      vdd->follow_links,
				      vd_path_list_done_cb,
				      vdd);
	g_free (handle);
}


VisitDirHandle *
visit_dir_async (VisitDirHandle   *handle,
		 const char       *directory,
		 const char       *filter_pattern,
		 gboolean          recursive,
		 gboolean          follow_links,
		 gboolean          same_fs,
		 gboolean          no_backup_files,
		 gboolean          no_dot_files,
		 gboolean          ignorecase,
		 gboolean          include_directories,
		 VisitDirDoneFunc  done_func,
		 gpointer          done_data)
{
	VisitDirData                   *vdd;
	GnomeVFSDirectoryFilterOptions  filter_options;

	vdd = g_new0 (VisitDirData, 1);
	vdd->base_dir = g_strdup (directory);
	vdd->directory = get_uri_from_path (directory);
	vdd->recursive = recursive;
	vdd->include_directories = include_directories;
	vdd->follow_links = follow_links;
	vdd->same_fs = same_fs;
	vdd->done_func = done_func;
	vdd->done_data = done_data;
	vdd->dirnames = g_hash_table_new (g_str_hash, g_str_equal);

	/* file filter */

	filter_options = GNOME_VFS_DIRECTORY_FILTER_DEFAULT;
	if (no_backup_files)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NOBACKUPFILES;
	if (no_dot_files)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_NODOTFILES;
	if (ignorecase)
		filter_options |= GNOME_VFS_DIRECTORY_FILTER_IGNORECASE;

	if (filter_pattern != NULL)
		vdd->filter = filter_new (filter_pattern, filter_options);

	/* handle */

	if (handle == NULL)
		handle = g_new0 (VisitDirHandle, 1);
	handle->vdd_data = vdd;

	_visit_dir_async (vdd->directory, vdd);

	return handle;
}


void
visit_dir_handle_free (VisitDirHandle   *handle)
{
	g_free (handle);
}


void
visit_dir_async_interrupt (VisitDirHandle   *handle,
			   DoneFunc          f,
			   gpointer          data)
{
	handle->vdd_data->interrupt_func = f;
	handle->vdd_data->interrupt_data = data;
	handle->vdd_data->interrupted = TRUE;

	visit_dir_handle_free (handle);
}





typedef struct {
	GList             *dir_list;
	GList             *current_dir;
	GList             *files;
	VisitDirHandle    *handle;

	char              *directory;
	char              *base_dir;
	gboolean           include_directories;
	VisitDirDoneFunc   done_func;
	gpointer           done_data;

	guint              visit_timeout;
} GetFileListData;



static void
get_file_list_data_free (GetFileListData *gfl_data)
{
	if (gfl_data == NULL)
		return;

	path_list_free (gfl_data->dir_list);
	g_free (gfl_data->directory);
	g_free (gfl_data->base_dir);
	g_free (gfl_data);
}


/* -- get_wildcard_file_list_async -- */


static void
get_wildcard_file_list_cb (GList *files, gpointer data)
{
	GetFileListData *gfl_data = data;
	GList           *rel_files = NULL;

	if (gfl_data->directory != NULL) {
		GList *scan;
		int    base_len = strlen (gfl_data->directory);

		for (scan = files; scan; scan = scan->next) {
			char *full_path = scan->data;

			if (path_in_path (gfl_data->directory, full_path)) {
				char *rel_path = g_strdup (full_path + base_len + 1);
				rel_files = g_list_prepend (rel_files, rel_path);
			}
		}
	}

	if (gfl_data->done_func)
		/* rel_files must be deallocated in pli->done_func */
		gfl_data->done_func (rel_files, gfl_data->done_data);
	else
		path_list_free (rel_files);

	get_file_list_data_free (gfl_data);
}


VisitDirHandle *
get_wildcard_file_list_async  (const char       *directory,
			       const char       *filter_pattern,
			       gboolean          recursive,
			       gboolean          follow_links,
			       gboolean          same_fs,
			       gboolean          no_backup_files,
			       gboolean          no_dot_files,
			       gboolean          ignorecase,
			       gboolean          include_directories,
			       VisitDirDoneFunc  done_func,
			       gpointer          done_data)
{
	GetFileListData *gfl_data;

	gfl_data = g_new0 (GetFileListData, 1);

	gfl_data->directory = get_uri_from_path (directory);
	gfl_data->done_func = done_func;
	gfl_data->done_data = done_data;

	return visit_dir_async (NULL,
				directory,
				filter_pattern,
				recursive,
				follow_links,
				same_fs,
				no_backup_files,
				no_dot_files,
				ignorecase,
				include_directories,
				get_wildcard_file_list_cb,
				gfl_data);
}


/* -- get_directory_file_list_async -- */


static void
get_directory_file_list_cb (GList *files, gpointer data)
{
	GetFileListData *gfl_data = data;
	GList           *rel_files = NULL;

	if (gfl_data->base_dir != NULL) {
		GList *scan;
		int    base_len;

		base_len = 0;
		if (strcmp (gfl_data->base_dir, "/") != 0)
			base_len = strlen (gfl_data->base_dir);

		for (scan = files; scan; scan = scan->next) {
			char *full_path = scan->data;

			if (strncmp (gfl_data->base_dir, full_path, base_len) == 0) {
				char *rel_path = g_strdup (full_path + base_len + 1);
				rel_files = g_list_prepend (rel_files, rel_path);
			}
		}
	}

	if (gfl_data->done_func)
		/* rel_files must be deallocated in pli->done_func */
		gfl_data->done_func (rel_files, gfl_data->done_data);
	else
		path_list_free (rel_files);

	get_file_list_data_free (gfl_data);
}


VisitDirHandle *
get_directory_file_list_async (const char       *directory,
			       const char       *base_dir,
			       gboolean          include_directories,
			       VisitDirDoneFunc  done_func,
			       gpointer          done_data)
{
	GetFileListData *gfl_data;
	char            *path;
	VisitDirHandle  *handle;

	gfl_data = g_new0 (GetFileListData, 1);

	gfl_data->directory = g_strdup (directory);
	gfl_data->base_dir  = g_strdup (base_dir);
	gfl_data->include_directories = include_directories;
	gfl_data->done_func = done_func;
	gfl_data->done_data = done_data;

	if (strcmp (base_dir, "/") == 0)
		path = g_strconcat (base_dir, directory, NULL);
	else
		path = g_strconcat (base_dir, "/", directory, NULL);

	handle = visit_dir_async (NULL,
				  path,
				  "*",
				  TRUE,
				  TRUE,
				  TRUE,
				  FALSE,
				  FALSE,
				  FALSE,
				  include_directories,
				  get_directory_file_list_cb,
				  gfl_data);
	g_free (path);

	return handle;
}


/* -- get_items_file_list_async -- */


static VisitDirHandle *visit_current_dir (GetFileListData *gfl_data);


static gboolean
visit_current_dir_idle_cb (gpointer data)
{
	GetFileListData *gfl_data = data;

	g_source_remove (gfl_data->visit_timeout);
	gfl_data->visit_timeout = 0;

	visit_current_dir (gfl_data);

	return FALSE;
}


static void
get_items_file_list_cb (GList *files, gpointer data)
{
	GetFileListData *gfl_data = data;

	if (gfl_data->base_dir != NULL) {
		GList *scan;
		int    base_len;

		base_len = 0;
		if (strcmp (gfl_data->base_dir, "/") != 0)
			base_len = strlen (gfl_data->base_dir);

		for (scan = files; scan; scan = scan->next) {
			char *full_path = scan->data;

			if (strncmp (gfl_data->base_dir, full_path, base_len) == 0) {
				char *rel_path = g_strdup (full_path + base_len + 1);
				gfl_data->files = g_list_prepend (gfl_data->files, rel_path);
			}
		}
	}

	gfl_data->current_dir = g_list_next (gfl_data->current_dir);
	gfl_data->visit_timeout = g_idle_add (visit_current_dir_idle_cb, gfl_data);
}


static VisitDirHandle *
visit_current_dir (GetFileListData *gfl_data)
{
	const char *directory;
	char       *path;

	if (gfl_data->current_dir == NULL) {
		if (gfl_data->done_func)
			/* gfl_data->files must be deallocated in gfl_data->done_func */
			gfl_data->done_func (gfl_data->files, gfl_data->done_data);
		else
			path_list_free (gfl_data->files);
		get_file_list_data_free (gfl_data);
		return NULL;
	}

	directory = file_name_from_path ((char*) gfl_data->current_dir->data);
	if (strcmp (gfl_data->base_dir, "/") == 0)
		path = g_strconcat (gfl_data->base_dir, directory, NULL);
	else
		path = g_strconcat (gfl_data->base_dir, "/", directory, NULL);

	visit_dir_async (gfl_data->handle,
			 path,
			 "*",
			 TRUE,
			 TRUE,
			 TRUE,
			 TRUE,
			 FALSE,
			 FALSE,
			 gfl_data->include_directories,
			 get_items_file_list_cb,
			 gfl_data);
	g_free (path);

	return gfl_data->handle;
}


VisitDirHandle *
get_items_file_list_async (GList            *item_list,
			   const char       *base_dir,
			   gboolean          include_directories,
			   VisitDirDoneFunc  done_func,
			   gpointer          done_data)
{
	GetFileListData *gfl_data;
	int              base_len;
	GList           *scan;

	g_return_val_if_fail (base_dir != NULL, NULL);

	gfl_data = g_new0 (GetFileListData, 1);

	base_len = 0;
	if (strcmp (base_dir, "/") != 0)
		base_len = strlen (base_dir);

	for (scan = item_list; scan; scan = scan->next) {
		char *path = scan->data;

		if (path_is_file (path)) {
			char *rel_path = g_strdup (path + base_len + 1);
			gfl_data->files = g_list_prepend (gfl_data->files, rel_path);
		}
		else if (path_is_dir (path))
			gfl_data->dir_list = g_list_prepend (gfl_data->dir_list, g_strdup (path));
	}

	gfl_data->current_dir = gfl_data->dir_list;
	gfl_data->base_dir = g_strdup (base_dir);
	gfl_data->include_directories = include_directories;
	gfl_data->done_func = done_func;
	gfl_data->done_data = done_data;
	gfl_data->handle = g_new0 (VisitDirHandle, 1);

	return visit_current_dir (gfl_data);
}
