/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2008 Free Software Foundation, Inc.
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
#include <gio/gio.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "file-list.h"


#define N_FILES_PER_REQUEST 128
#define SPECIAL_DIR(x) (! strcmp (x, "..") || ! strcmp (x, "."))


/* -- filter -- */


typedef enum {
	FILTER_DEFAULT = 0,
	FILTER_NODOTFILES = 1 << 1,
	FILTER_IGNORECASE = 1 << 2,
	FILTER_NOBACKUPFILES = 1 << 3
} FilterOptions;


typedef struct {
	char           *pattern;
	char          **patterns;
	FilterOptions   options;
	GRegex        **regexps;
} Filter;


static Filter *
filter_new (const char    *pattern,
	    FilterOptions  options)
{
	Filter             *filter;
	GRegexCompileFlags  flags;
	int                 i;
	
	filter = g_new (Filter, 1);

	filter->pattern = g_strdup (pattern);
	filter->patterns = search_util_get_patterns (pattern);
	filter->options = options;

	if (filter->options & FILTER_IGNORECASE)
		flags = G_REGEX_CASELESS;
	else
		flags = 0;
		
	filter->regexps = g_new0 (GRegex*, n_fields (filter->patterns) + 1);
	for (i = 0; filter->patterns[i] != NULL; i++) 
		filter->regexps[i] = g_regex_new (filter->patterns[i],
					          flags,
					          G_REGEX_MATCH_NOTEMPTY,
					          NULL);
	filter->regexps[i] = NULL;
	
	return filter;
}


static void
filter_destroy (Filter *filter)
{	
	g_return_if_fail (filter != NULL);

	g_free (filter->pattern);
	if (filter->patterns != NULL)
		g_strfreev (filter->patterns);
	
	if (filter->regexps != NULL) {
		int i;
		for (i = 0; filter->regexps[i] != NULL; i++)
			 g_regex_unref (filter->regexps[i]);
		g_free (filter->regexps);
	}
	
	g_free (filter);
}


static gboolean
match_regexps (GRegex     **regexps,
	       const char  *string)
{
	gboolean matched;
	int      i;
	
	if ((regexps == NULL) || (regexps[0] == NULL))
		return TRUE;

	if (string == NULL)
		return FALSE;
	
	matched = FALSE;
	for (i = 0; regexps[i] != NULL; i++)
		if (g_regex_match (regexps[i], string, 0, NULL)) {
			matched = TRUE;
			break;
		}
		
	return matched;
}


static gboolean
filter_apply (Filter     *filter,
	      const char *name)
{
	const char *file_name;
	char       *utf8_name;
	gboolean    matched;

	g_return_val_if_fail (filter != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	file_name = file_name_from_path (name);

	if ((filter->options & FILTER_NODOTFILES)
	    && ((file_name[0] == '.') || (strstr (file_name, "/.") != NULL)))
		return FALSE;

	if ((filter->options & FILTER_NOBACKUPFILES)
	    && (file_name[strlen (file_name) - 1] == '~'))
		return FALSE;
	
	utf8_name = g_filename_to_utf8 (file_name, -1, NULL, NULL, NULL);
	matched = match_regexps (filter->regexps, utf8_name);
	g_free (utf8_name);

	return matched;
}


/* -- path list async -- */


typedef struct _PathListData PathListData;
typedef void (*PathListDoneFunc) (PathListData *dld, gpointer data);


struct _PathListData {
	GFile            *directory;
	GCancellable     *cancellable;
	GFileEnumerator  *enumerator;
	GError           *error;
	GList            *files;               /* char* items. */
	GList            *dirs;                /* char* items. */
	PathListDoneFunc  done_func;
	gpointer          done_data;
	gboolean          interrupted;
	DoneFunc          interrupt_func;
	gpointer          interrupt_data;
};


typedef struct {
	PathListData *pld;
} PathListHandle;


static PathListData *
path_list_data_new (void)
{
	return (PathListData *) g_new0 (PathListData, 1);
}


static void
path_list_data_free (PathListData *pld)
{
	g_return_if_fail (pld != NULL);

	if (pld->directory != NULL)
		g_object_unref (pld->directory);
	if (pld->cancellable != NULL)
		g_object_unref (pld->cancellable);
	if (pld->enumerator != NULL)
		g_object_unref (pld->enumerator);
	g_clear_error (&(pld->error));
	path_list_free (pld->files);
	path_list_free (pld->dirs);
	g_free (pld);
}


static void
path_list_data_done (PathListData *pld)
{
	if (pld->interrupted) {
		if (pld->interrupt_func) 
			pld->interrupt_func (pld->interrupt_data);
		path_list_data_free (pld);
		return;
	}
	
	if (pld->done_func) {
		/* pld must be deallocated in the pld->done_func function if
		 * the operation was not stopped with 
		 * path_list_async_interrupt */
		pld->done_func (pld, pld->done_data);
		
	}
	else
		path_list_data_free (pld);
}


static void
path_list_handle_free (PathListHandle *handle)
{
	if (handle->pld != NULL)
		path_list_data_free (handle->pld);
	g_free (handle);
}


static void  
path_list_async_next_files_ready (GObject      *source_object,
			          GAsyncResult *result,
			          gpointer      user_data)
{
	PathListData *pld = user_data;
	GList        *files, *scan;
	char         *directory_uri;
	char         *name;

	files = g_file_enumerator_next_files_finish (pld->enumerator,
                                                     result,
                                                     &(pld->error));
	if (files == NULL) {
		path_list_data_done (pld);
		return;
	}
	
	directory_uri = g_file_get_uri (pld->directory);	
	for (scan = files; scan; scan = scan->next) {
		GFileInfo *info = scan->data;
		
		g_print ("=0=> %s\n", g_file_info_get_name (info));
		
		switch (g_file_info_get_file_type (info)) {
		case G_FILE_TYPE_REGULAR:
			name = g_uri_escape_string (g_file_info_get_name (info), G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT, FALSE);
			pld->files = g_list_prepend (pld->files, g_strconcat (directory_uri, "/", name, NULL));
			g_print ("   [F] \n");
			break;
		case G_FILE_TYPE_DIRECTORY:
			name = g_uri_escape_string (g_file_info_get_name (info), G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT, FALSE);
			pld->dirs = g_list_prepend (pld->dirs, g_strconcat (directory_uri, "/", name, NULL));
			g_print ("   [D] \n");
			break;
		default:
			break;
		}
	}
	g_free (directory_uri);
	
	g_file_enumerator_next_files_async (pld->enumerator,
                                            N_FILES_PER_REQUEST,
                                            G_PRIORITY_DEFAULT,
                                            pld->cancellable,
                                            path_list_async_next_files_ready,
                                            pld);
}


static void  
path_list_async_new_ready (GObject      *source_object,
			   GAsyncResult *result,
			   gpointer      user_data)
{
	PathListData *pld = user_data;
	
	pld->enumerator = g_file_enumerate_children_finish (pld->directory, result, &(pld->error));
	if (pld->enumerator == NULL) {
		path_list_data_done (pld);
		return;
	}
	
	g_file_enumerator_next_files_async (pld->enumerator,
                                            N_FILES_PER_REQUEST,
                                            G_PRIORITY_DEFAULT,
                                            pld->cancellable,
                                            path_list_async_next_files_ready,
                                            pld);
}


static PathListHandle *
path_list_async_new (const char       *uri,
		     gboolean          follow_links,
		     PathListDoneFunc  f,
		     gpointer          data)
{
	PathListData   *pld;
	PathListHandle *pl_handle;
	
	pld = path_list_data_new ();
	pld->directory = g_file_new_for_uri (uri); 
	pld->done_func = f;
	pld->done_data = data;
	pld->cancellable = g_cancellable_new (); 
	
	g_file_enumerate_children_async (pld->directory,
					 "standard::name,standard::type",
					 G_FILE_QUERY_INFO_NONE,
					 G_PRIORITY_DEFAULT,
                                         pld->cancellable,
					 path_list_async_new_ready,
					 pld);
					 
	pl_handle = g_new (PathListHandle, 1);
	pl_handle->pld = pld;
	
	return pl_handle;
}


static void
path_list_async_interrupt (PathListHandle   *handle,
			   DoneFunc          f,
			   gpointer          data)
{
	g_cancellable_cancel (handle->pld->cancellable);
	handle->pld->interrupted = TRUE;
	handle->pld->interrupt_func = f;
	handle->pld->interrupt_data = data;

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
	return TRUE;
	
	/* FIXME: reimplement using gio
	 
	GnomeVFSURI    *uri1, *uri2;
	GnomeVFSResult  result;
	gboolean        same;

	uri1 = gnome_vfs_uri_new (path1);
	uri2 = gnome_vfs_uri_new (path2);

	result = gnome_vfs_check_same_fs_uris (uri1, uri2, &same);

	gnome_vfs_uri_unref (uri1);
	gnome_vfs_uri_unref (uri2);

	return (result == GNOME_VFS_OK) && same;
	*/
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
			} 
			else
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
		path_list_data_free (pld);
		return;
	}

	if (pld->error != NULL) {
		char *uri, *name;

		uri = g_file_get_uri (pld->directory);
		name = g_filename_display_name (uri);
		g_warning ("Error reading directory %s: %s.", name, pld->error->message);
		g_free (name);
		g_free (uri);

		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		visit_dir_data_free (vdd);
		path_list_data_free (pld);
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
			} 
			else
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
		visit_dir_data_free (vdd);
		path_list_data_free (pld);
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
		} 
		else {
			g_free (sub_dir);
			sub_dir = NULL;
		}
	}

	if (sub_dir == NULL) {
		if (vdd->done_func)
			(* vdd->done_func) (vdd->files, vdd->done_data);
		visit_dir_data_free (vdd);
	} 
	else
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
	VisitDirData  *vdd;
	FilterOptions  filter_options;

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

	filter_options = FILTER_DEFAULT;
	if (no_backup_files)
		filter_options |= FILTER_NOBACKUPFILES;
	if (no_dot_files)
		filter_options |= FILTER_NODOTFILES;
	if (ignorecase)
		filter_options |= FILTER_IGNORECASE;

	if (filter_pattern != NULL)
		vdd->filter = filter_new (filter_pattern, filter_options);

	/* handle */

	if (handle == NULL)
		handle = g_new0 (VisitDirHandle, 1);
	handle->vdd_data = vdd;

	if (include_directories) {
		char *dir;
		
		/* Always include the base directory, this way empty base 
		 * directories are added to the archive as well.  */
		
		dir = g_strdup (vdd->base_dir);
		vdd->files = g_list_prepend (vdd->files, dir);
		g_hash_table_insert (vdd->dirnames, dir, GINT_TO_POINTER (1));
	}

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


/* -- get_file_list_data -- */


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


/* -- get_wildcard_file_list_async & get_directory_file_list_async -- */


static void
get_directory_file_list_cb (GList    *files, 
			    gpointer  data)
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

			if (path_in_path (gfl_data->base_dir, full_path)) {
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
	gfl_data->base_dir = g_strdup (gfl_data->directory);
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
				get_directory_file_list_cb,
				gfl_data);
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

			if (path_in_path (gfl_data->base_dir, full_path)) {
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
