/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2008 Free Software Foundation, Inc.
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
#include <string.h>
#include <glib.h>
#include <gio/gio.h>
#include "file-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"

#define N_FILES_PER_REQUEST 128


/* FileInfo */


FileInfo *
file_info_new (GFile     *file,
	       GFileInfo *info)
{
	FileInfo *data;

	data = g_new0 (FileInfo, 1);
	data->file = g_file_dup (file);
	data->info = g_file_info_dup (info);

	return data;
}


void
file_info_free (FileInfo *file_info)
{
	if (file_info == NULL)
		return;
	_g_object_unref (file_info->file);
	_g_object_unref (file_info->info);
	g_free (file_info);
}


void
file_info_list_free (GList *list)
{
	g_list_free_full (list, (GDestroyNotify) file_info_free);
}


/* -- filter -- */


struct _FileFilter {
	int      ref_count;
	char    *pattern;
	GRegex **regexps;
};


FileFilter *
file_filter_new (const char *pattern)
{
	FileFilter *filter;

	filter = g_new0 (FileFilter, 1);
	filter->ref_count = 1;
	if ((pattern != NULL) && (strcmp (pattern, "*") != 0))
		filter->pattern = g_strdup (pattern);

	filter->regexps = _g_regexp_split_from_patterns (pattern, G_REGEX_CASELESS);

	return filter;
}


void
file_filter_unref (FileFilter *filter)
{
	if (filter == NULL)
		return;
	if (--filter->ref_count > 0)
		return;

	g_free (filter->pattern);
	_g_regexp_freev (filter->regexps);
	g_free (filter);
}


gboolean
file_filter_matches (FileFilter *filter,
		     GFile      *file)
{
	char     *file_name;
	char     *utf8_name;
	gboolean  matched;

	g_return_val_if_fail (file != NULL, FALSE);

	if (filter->pattern == NULL)
		return TRUE;

	file_name = g_file_get_basename (file);
	utf8_name = g_filename_to_utf8 (file_name, -1, NULL, NULL, NULL);
	matched = _g_regexp_matchv (filter->regexps, utf8_name, 0);

	g_free (utf8_name);
	g_free (file_name);

	return matched;
}


gboolean
file_filter_empty (FileFilter *filter)
{
	return ((filter->pattern == NULL) || (strcmp (filter->pattern, "*") == 0));
}


/* -- g_directory_foreach_child -- */


static void
_g_info_data_freev (FileInfo **data)
{
	if (*data != NULL)
		file_info_free (*data);
	*data = NULL;
}


typedef struct {
	GFile                *base_directory;
	gboolean              recursive;
	gboolean              follow_links;
	StartDirCallback      start_dir_func;
	ForEachChildCallback  for_each_file_func;
	ForEachDoneCallback   done_func;
	gpointer              user_data;

	/* private */

	FileInfo            *current;
	GHashTable           *already_visited;
	GList                *to_visit;
	char                 *attributes;
	GCancellable         *cancellable;
	GFileEnumerator      *enumerator;
	GError               *error;
	guint                 source_id;
	GList                *children;
	GList                *current_child;
} ForEachChildData;


static void
for_each_child_data_free (ForEachChildData *fec)
{
	if (fec == NULL)
		return;

	g_object_unref (fec->base_directory);
	if (fec->already_visited != NULL)
		g_hash_table_destroy (fec->already_visited);
	_g_info_data_freev (&(fec->current));
	g_free (fec->attributes);
	if (fec->to_visit != NULL) {
		g_list_free_full (fec->to_visit, (GDestroyNotify) file_info_free);
	}
	_g_object_unref (fec->cancellable);
	g_free (fec);
}


static gboolean
for_each_child_done_cb (gpointer user_data)
{
	ForEachChildData *fec = user_data;

	g_source_remove (fec->source_id);
	_g_info_data_freev (&(fec->current));
	if (fec->done_func)
		fec->done_func (fec->error, fec->user_data);
	for_each_child_data_free (fec);

	return FALSE;
}


static void
for_each_child_done (ForEachChildData *fec)
{
	fec->source_id = g_idle_add (for_each_child_done_cb, fec);
}


static void for_each_child_start_current (ForEachChildData *fec);


static void
for_each_child_start (ForEachChildData *fec)
{
	for_each_child_start_current (fec);
}


static void
for_each_child_set_current (ForEachChildData *fec,
			    FileInfo        *data)
{
	_g_info_data_freev (&(fec->current));
	fec->current = data;
}


static void
for_each_child_start_next_sub_directory (ForEachChildData *fec)
{
	FileInfo *child = NULL;

	if (fec->to_visit != NULL) {
		GList *tmp;

		child = (FileInfo *) fec->to_visit->data;
		tmp = fec->to_visit;
		fec->to_visit = g_list_remove_link (fec->to_visit, tmp);
		g_list_free (tmp);
	}

	if (child != NULL) {
		for_each_child_set_current (fec, child);
		for_each_child_start (fec);
	}
	else
		for_each_child_done (fec);
}


static void
for_each_child_close_enumerator (GObject      *source_object,
				 GAsyncResult *result,
		      		 gpointer      user_data)
{
	ForEachChildData *fec = user_data;
	GError           *error = NULL;

	if (! g_file_enumerator_close_finish (fec->enumerator,
					      result,
					      &error))
	{
		if (fec->error == NULL)
			fec->error = g_error_copy (error);
		g_clear_error (&error);
	}

	if ((fec->error == NULL) && fec->recursive)
		for_each_child_start_next_sub_directory (fec);
	else
		for_each_child_done (fec);
}


static void for_each_child_next_files_ready (GObject      *source_object,
					     GAsyncResult *result,
					     gpointer      user_data);


static void
for_each_child_read_next_files (ForEachChildData *fec)
{
	_g_object_list_unref (fec->children);
	fec->children = NULL;
	g_file_enumerator_next_files_async (fec->enumerator,
					    N_FILES_PER_REQUEST,
					    G_PRIORITY_DEFAULT,
					    fec->cancellable,
					    for_each_child_next_files_ready,
					    fec);
}


static void
for_each_child_compute_child (ForEachChildData *fec,
			      GFile            *file,
			      GFileInfo        *info)
{
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		char *id;

		/* avoid to visit a directory more than ones */

		id = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILE));
		if (id == NULL)
			id = g_file_get_uri (file);

		if (g_hash_table_lookup (fec->already_visited, id) == NULL) {
			g_hash_table_insert (fec->already_visited, g_strdup (id), GINT_TO_POINTER (1));
			fec->to_visit = g_list_append (fec->to_visit, file_info_new (file, info));
		}

		g_free (id);
	}

	fec->for_each_file_func (file, info, fec->user_data);
}


static void
for_each_child_next_files_ready (GObject      *source_object,
				 GAsyncResult *result,
				 gpointer      user_data)
{
	ForEachChildData *fec = user_data;
	GList            *scan;

	fec->children = g_file_enumerator_next_files_finish (fec->enumerator,
							     result,
							     &(fec->error));

	if (fec->children == NULL) {
		g_file_enumerator_close_async (fec->enumerator,
					       G_PRIORITY_DEFAULT,
					       g_cancellable_is_cancelled (fec->cancellable) ? NULL : fec->cancellable,
					       for_each_child_close_enumerator,
					       fec);
		return;
	}

	for (scan = fec->children; scan; scan = scan->next) {
		GFileInfo *child_info = scan->data;
		GFile     *child_file;

		child_file = g_file_get_child (fec->current->file, g_file_info_get_name (child_info));
		for_each_child_compute_child (fec, child_file, child_info);

		g_object_unref (child_file);
	}

	for_each_child_read_next_files (fec);
}


static void
for_each_child_ready (GObject      *source_object,
		      GAsyncResult *result,
		      gpointer      user_data)
{
	ForEachChildData *fec = user_data;

	fec->enumerator = g_file_enumerate_children_finish (G_FILE (source_object), result, &(fec->error));
	if (fec->enumerator == NULL) {
		for_each_child_done (fec);
		return;
	}

	g_file_enumerator_next_files_async (fec->enumerator,
					    N_FILES_PER_REQUEST,
					    G_PRIORITY_DEFAULT,
					    fec->cancellable,
					    for_each_child_next_files_ready,
					    fec);
}


static void
for_each_child_start_current (ForEachChildData *fec)
{
	if (fec->start_dir_func != NULL) {
		DirOp  op;

		op = fec->start_dir_func (fec->current->file, fec->current->info, &(fec->error), fec->user_data);
		switch (op) {
		case DIR_OP_SKIP:
			for_each_child_start_next_sub_directory (fec);
			return;
		case DIR_OP_STOP:
			for_each_child_done (fec);
			return;
		case DIR_OP_CONTINUE:
			break;
		}
	}

	g_file_enumerate_children_async (fec->current->file,
					 fec->attributes,
					 fec->follow_links ? G_FILE_QUERY_INFO_NONE : G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					 G_PRIORITY_DEFAULT,
					 fec->cancellable,
					 for_each_child_ready,
					 fec);
}


static void
directory_info_ready_cb (GObject      *source_object,
			 GAsyncResult *result,
			 gpointer      user_data)
{
	ForEachChildData *fec = user_data;
	GFileInfo        *info;
	FileInfo        *child;

	info = g_file_query_info_finish (G_FILE (source_object), result, &(fec->error));
	if (info == NULL) {
		for_each_child_done (fec);
		return;
	}

	child = file_info_new (fec->base_directory, info);
	g_object_unref (info);

	for_each_child_set_current (fec, child);
	for_each_child_start_current (fec);
}


/**
 * g_directory_foreach_child:
 * @directory: The directory to visit.
 * @recursive: Whether to traverse the @directory recursively.
 * @follow_links: Whether to dereference the symbolic links.
 * @attributes: The GFileInfo attributes to read.
 * @cancellable: An optional @GCancellable object, used to cancel the process.
 * @start_dir_func: the function called for each sub-directory, or %NULL if
 *   not needed.
 * @for_each_file_func: the function called for each file.  Can't be %NULL.
 * @done_func: the function called at the end of the traversing process.
 *   Can't be %NULL.
 * @user_data: data to pass to @done_func
 *
 * Traverse the @directory's filesystem structure calling the
 * @for_each_file_func function for each file in the directory; the
 * @start_dir_func function on each directory before it's going to be
 * traversed, this includes @directory too; the @done_func function is
 * called at the end of the process.
 * Some traversing options are available: if @recursive is TRUE the
 * directory is traversed recursively; if @follow_links is TRUE, symbolic
 * links are dereferenced, otherwise they are returned as links.
 * Each callback uses the same @user_data additional parameter.
 */
void
g_directory_foreach_child (GFile                *directory,
			   gboolean              recursive,
			   gboolean              follow_links,
			   const char           *attributes,
			   GCancellable         *cancellable,
			   StartDirCallback      start_dir_func,
			   ForEachChildCallback  for_each_file_func,
			   ForEachDoneCallback   done_func,
			   gpointer              user_data)
{
	ForEachChildData *fec;

	g_return_if_fail (for_each_file_func != NULL);

	fec = g_new0 (ForEachChildData, 1);

	fec->base_directory = g_file_dup (directory);
	fec->recursive = recursive;
	fec->follow_links = follow_links;
	fec->attributes = g_strconcat ("standard::name,standard::type,id::file",
				       (((attributes != NULL) && (strcmp (attributes, "") != 0)) ? "," : NULL),
				       attributes,
				       NULL);
	fec->cancellable = _g_object_ref (cancellable);
	fec->start_dir_func = start_dir_func;
	fec->for_each_file_func = for_each_file_func;
	fec->done_func = done_func;
	fec->user_data = user_data;
	fec->already_visited = g_hash_table_new_full (g_str_hash,
						      g_str_equal,
						      g_free,
						      NULL);

	g_file_query_info_async (fec->base_directory,
				 fec->attributes,
				 G_FILE_QUERY_INFO_NONE,
				 G_PRIORITY_DEFAULT,
				 fec->cancellable,
				 directory_info_ready_cb,
				 fec);
}


/* -- _g_file_list_query_info_async -- */


typedef struct {
	GList               *file_list;
	FileListFlags        flags;
	char                *attributes;
	GCancellable        *cancellable;
	FilterMatchCallback  directory_filter_func;
	FilterMatchCallback  file_filter_func;
	InfoReadyCallback    callback;
	gpointer             user_data;
	GList               *current;
	GList               *files;
} QueryData;


static void
query_data_free (QueryData *query_data)
{
	_g_object_list_unref (query_data->file_list);
	file_info_list_free (query_data->files);
	_g_object_unref (query_data->cancellable);
	g_free (query_data->attributes);
	g_free (query_data);
}


static void query_info__query_current (QueryData *query_data);


static void
query_info__query_next (QueryData *query_data)
{
	query_data->current = query_data->current->next;
	query_info__query_current (query_data);
}


static void
query_data__done_cb (GError   *error,
		     gpointer  user_data)
{
	QueryData *query_data = user_data;

	if (error != NULL) {
		query_data->callback (NULL, error, query_data->user_data);
		query_data_free (query_data);
		return;
	}

	query_info__query_next (query_data);
}


static void
query_data__for_each_file_cb (GFile      *file,
			      GFileInfo  *info,
			      gpointer    user_data)
{
	QueryData *query_data = user_data;

	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
		return;
	if ((query_data->flags & FILE_LIST_NO_BACKUP_FILES) && g_file_info_get_is_backup (info))
		return;
	if ((query_data->flags & FILE_LIST_NO_HIDDEN_FILES) && g_file_info_get_is_hidden (info))
		return;
	if ((query_data->file_filter_func != NULL) && query_data->file_filter_func (file, info, query_data->user_data))
		return;

	query_data->files = g_list_prepend (query_data->files, file_info_new (file, info));
}


static DirOp
query_data__start_dir_cb (GFile       *file,
		          GFileInfo   *info,
		          GError     **error,
		          gpointer     user_data)
{
	QueryData *query_data = user_data;

	if ((query_data->flags & FILE_LIST_NO_BACKUP_FILES) && g_file_info_get_is_backup (info))
		return DIR_OP_SKIP;
	if ((query_data->flags & FILE_LIST_NO_HIDDEN_FILES) && g_file_info_get_is_hidden (info))
		return DIR_OP_SKIP;
	if ((query_data->directory_filter_func != NULL) && query_data->directory_filter_func (file, info, query_data->user_data))
		return DIR_OP_SKIP;

	query_data->files = g_list_prepend (query_data->files, file_info_new (file, info));

	return DIR_OP_CONTINUE;
}


static void
query_data_info_ready_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	QueryData *query_data = user_data;
	GError    *error = NULL;
	GFileInfo *info;

	info = g_file_query_info_finish ((GFile *) source_object, result, &error);
	if (info == NULL) {
		query_info__query_next (query_data);
		/*
		query_data->callback (NULL, error, query_data->user_data);
		query_data_free (query_data);
		*/
		return;
	}

	if ((query_data->flags & FILE_LIST_RECURSIVE) && (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)) {
		g_directory_foreach_child (G_FILE (query_data->current->data),
					   TRUE,
					   (query_data->flags & FILE_LIST_NO_FOLLOW_LINKS) == 0,
					   query_data->attributes,
					   query_data->cancellable,
					   query_data__start_dir_cb,
					   query_data__for_each_file_cb,
					   query_data__done_cb,
					   query_data);
	}
	else {
		query_data->files = g_list_prepend (query_data->files, file_info_new ((GFile *) query_data->current->data, info));
		query_info__query_next (query_data);
	}

	g_object_unref (info);
}


static void
query_info__query_current (QueryData *query_data)
{
	GFileQueryInfoFlags flags;

	if (query_data->current == NULL) {
		query_data->files = g_list_reverse (query_data->files);
		query_data->callback (query_data->files, NULL, query_data->user_data);
		query_data_free (query_data);
		return;
	}

	flags = G_FILE_QUERY_INFO_NONE;
	if (query_data->flags & FILE_LIST_NO_FOLLOW_LINKS)
		flags |= G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;

	g_file_query_info_async ((GFile *) query_data->current->data,
				 query_data->attributes,
				 flags,
				 G_PRIORITY_DEFAULT,
				 query_data->cancellable,
				 query_data_info_ready_cb,
				 query_data);
}


void
_g_file_list_query_info_async (GList               *file_list,
			       FileListFlags        flags,
			       const char          *attributes,
			       GCancellable        *cancellable,
			       FilterMatchCallback  directory_filter_func,
			       FilterMatchCallback  file_filter_func,
			       InfoReadyCallback    ready_callback,
			       gpointer             user_data)
{
	QueryData *query_data;

	query_data = g_new0 (QueryData, 1);
	query_data->file_list = _g_object_list_ref (file_list);
	query_data->flags = flags;
	query_data->attributes = g_strconcat ("standard::name,standard::type,standard::is-hidden,standard::is-backup,id::file",
					      (((attributes != NULL) && (strcmp (attributes, "") != 0)) ? "," : NULL),
					      attributes,
					      NULL);
	query_data->cancellable = _g_object_ref (cancellable);
	query_data->directory_filter_func = directory_filter_func;
	query_data->file_filter_func = file_filter_func;
	query_data->callback = ready_callback;
	query_data->user_data = user_data;

	query_data->current = query_data->file_list;
	query_info__query_current (query_data);
}


/* -- g_copy_files_async -- */


typedef struct {
	GList                 *sources;
	GList                 *destinations;
	GFileCopyFlags         flags;
	int                    io_priority;
	GCancellable          *cancellable;
	CopyProgressCallback   progress_callback;
	gpointer               progress_callback_data;
	CopyDoneCallback       callback;
	gpointer               user_data;

	GList                 *source;
	GList                 *destination;
	int                    n_file;
	int                    tot_files;
} CopyFilesData;


static CopyFilesData*
copy_files_data_new (GList                 *sources,
		     GList                 *destinations,
		     GFileCopyFlags         flags,
		     int                    io_priority,
		     GCancellable          *cancellable,
		     CopyProgressCallback   progress_callback,
		     gpointer               progress_callback_data,
		     CopyDoneCallback       callback,
		     gpointer               user_data)
{
	CopyFilesData *cfd;

	cfd = g_new0 (CopyFilesData, 1);
	cfd->sources = _g_file_list_dup (sources);
	cfd->destinations = _g_file_list_dup (destinations);
	cfd->flags = flags;
	cfd->io_priority = io_priority;
	cfd->cancellable = cancellable;
	cfd->progress_callback = progress_callback;
	cfd->progress_callback_data = progress_callback_data;
	cfd->callback = callback;
	cfd->user_data = user_data;

	cfd->source = cfd->sources;
	cfd->destination = cfd->destinations;
	cfd->n_file = 1;
	cfd->tot_files = g_list_length (cfd->sources);

	return cfd;
}


static void
copy_files_data_free (CopyFilesData *cfd)
{
	if (cfd == NULL)
		return;
	_g_file_list_free (cfd->sources);
	_g_file_list_free (cfd->destinations);
	g_free (cfd);
}


static void g_copy_current_file (CopyFilesData *cfd);


static void
g_copy_next_file (CopyFilesData *cfd)
{
	cfd->source = g_list_next (cfd->source);
	cfd->destination = g_list_next (cfd->destination);
	cfd->n_file++;

	g_copy_current_file (cfd);
}


static void
g_copy_files_ready_cb (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	CopyFilesData *cfd = user_data;
	GFile         *source = cfd->source->data;
	GError        *error = NULL;

	if (! g_file_copy_finish (source, result, &error)) {
		/* source and target are directories, ignore the error */
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE))
			g_clear_error (&error);
		/* source is directory, create target directory */
		if (g_error_matches (error, G_IO_ERROR,  G_IO_ERROR_WOULD_RECURSE)) {
			g_clear_error (&error);
			g_file_make_directory ((GFile*) cfd->destination->data,
					       cfd->cancellable,
					       &error);
		}
	}

	if (error) {
		if (cfd->callback)
			cfd->callback (error, cfd->user_data);
		g_clear_error (&error);
		copy_files_data_free (cfd);
		return;
	}

	g_copy_next_file (cfd);
}


static void
g_copy_files_progress_cb (goffset  current_num_bytes,
                          goffset  total_num_bytes,
                          gpointer user_data)
{
	CopyFilesData *cfd = user_data;

	if (cfd->progress_callback)
		cfd->progress_callback (cfd->n_file,
					cfd->tot_files,
					(GFile*) cfd->source->data,
					(GFile*) cfd->destination->data,
					current_num_bytes,
					total_num_bytes,
					cfd->progress_callback_data);
}


static void
g_copy_current_file (CopyFilesData *cfd)
{
	if ((cfd->source == NULL) || (cfd->destination == NULL)) {
		if (cfd->callback)
			cfd->callback (NULL, cfd->user_data);
		copy_files_data_free (cfd);
		return;
	}

	g_file_copy_async ((GFile*) cfd->source->data,
			   (GFile*) cfd->destination->data,
			   cfd->flags,
			   cfd->io_priority,
			   cfd->cancellable,
			   g_copy_files_progress_cb,
			   cfd,
			   g_copy_files_ready_cb,
			   cfd);
}


void
g_copy_files_async (GList                 *sources,
		    GList                 *destinations,
		    GFileCopyFlags         flags,
		    int                    io_priority,
		    GCancellable          *cancellable,
		    CopyProgressCallback   progress_callback,
		    gpointer               progress_callback_data,
		    CopyDoneCallback       callback,
		    gpointer               user_data)
{
	CopyFilesData *cfd;

	cfd = copy_files_data_new (sources,
				   destinations,
				   flags,
				   io_priority,
				   cancellable,
				   progress_callback,
				   progress_callback_data,
				   callback,
				   user_data);
	g_copy_current_file (cfd);
}


void
g_copy_file_async (GFile                 *source,
		   GFile                 *destination,
		   GFileCopyFlags         flags,
		   int                    io_priority,
		   GCancellable          *cancellable,
		   CopyProgressCallback   progress_callback,
		   gpointer               progress_callback_data,
		   CopyDoneCallback       callback,
		   gpointer               user_data)
{
	GList *source_files;
	GList *destination_files;

	source_files = g_list_append (NULL, (gpointer) source);
	destination_files = g_list_append (NULL, (gpointer) destination);

	g_copy_files_async (source_files,
			    destination_files,
			    flags,
			    io_priority,
			    cancellable,
			    progress_callback,
			    progress_callback_data,
			    callback,
			    user_data);

	g_list_free (source_files);
	g_list_free (destination_files);
}


/* -- g_directory_copy_async -- */


typedef struct {
	GFile                 *source;
	GFile                 *destination;
	GFileCopyFlags         flags;
	int                    io_priority;
	GCancellable          *cancellable;
	CopyProgressCallback   progress_callback;
	gpointer               progress_callback_data;
	CopyDoneCallback       callback;
	gpointer               user_data;
	GError                *error;

	GList                 *to_copy;
	GList                 *current;
	GFile                 *current_source;
	GFile                 *current_destination;
	int                    n_file, tot_files;
	guint                  source_id;
} DirectoryCopyData;


static void
directory_copy_data_free (DirectoryCopyData *dcd)
{
	if (dcd == NULL)
		return;

	if (dcd->source != NULL)
		g_object_unref (dcd->source);
	if (dcd->destination != NULL)
		g_object_unref (dcd->destination);
	if (dcd->current_source != NULL) {
		g_object_unref (dcd->current_source);
		dcd->current_source = NULL;
	}
	if (dcd->current_destination != NULL) {
		g_object_unref (dcd->current_destination);
		dcd->current_destination = NULL;
	}
	g_list_free_full (dcd->to_copy, (GDestroyNotify) file_info_free);
	g_free (dcd);
}


static gboolean
g_directory_copy_done (gpointer user_data)
{
	DirectoryCopyData *dcd = user_data;

	g_source_remove (dcd->source_id);

	if (dcd->callback)
		dcd->callback (dcd->error, dcd->user_data);
	if (dcd->error != NULL)
		g_clear_error (&(dcd->error));
	directory_copy_data_free (dcd);

	return FALSE;
}


static GFile *
get_destination_for_uri (DirectoryCopyData *dcd,
		         GFile             *file)
{
	GFile *destination_file;
	char  *relative_path;

	relative_path = g_file_get_relative_path (dcd->source, file);
	if (relative_path != NULL)
		destination_file = g_file_resolve_relative_path (dcd->destination, relative_path);
	else
		destination_file = g_file_dup (dcd->destination);

	g_free (relative_path);

	return destination_file;
}


static void g_directory_copy_current_child (DirectoryCopyData *dcd);


static gboolean
g_directory_copy_next_child (gpointer user_data)
{
	DirectoryCopyData *dcd = user_data;

	g_source_remove (dcd->source_id);

	dcd->current = g_list_next (dcd->current);
	dcd->n_file++;
	g_directory_copy_current_child (dcd);

	return FALSE;
}


static void
g_directory_copy_child_done_cb (GObject      *source_object,
                        	GAsyncResult *result,
 	                        gpointer      user_data)
{
	DirectoryCopyData *dcd = user_data;

	if (! g_file_copy_finish ((GFile*)source_object, result, &(dcd->error))) {
		dcd->source_id = g_idle_add (g_directory_copy_done, dcd);
		return;
	}

	dcd->source_id = g_idle_add (g_directory_copy_next_child, dcd);
}


static void
g_directory_copy_child_progress_cb (goffset  current_num_bytes,
                                    goffset  total_num_bytes,
                                    gpointer user_data)
{
	DirectoryCopyData *dcd = user_data;

	if (dcd->progress_callback)
		dcd->progress_callback (dcd->n_file,
					dcd->tot_files,
					dcd->current_source,
					dcd->current_destination,
					current_num_bytes,
					total_num_bytes,
					dcd->progress_callback_data);
}


static void
g_directory_copy_current_child (DirectoryCopyData *dcd)
{
	FileInfo *child;
	gboolean   async_op = FALSE;

	if (dcd->current == NULL) {
		dcd->source_id = g_idle_add (g_directory_copy_done, dcd);
		return;
	}

	if (dcd->current_source != NULL) {
		g_object_unref (dcd->current_source);
		dcd->current_source = NULL;
	}
	if (dcd->current_destination != NULL) {
		g_object_unref (dcd->current_destination);
		dcd->current_destination = NULL;
	}

	child = dcd->current->data;
	dcd->current_source = g_object_ref (child->file);
	dcd->current_destination = get_destination_for_uri (dcd, child->file);
	if (dcd->current_destination == NULL) {
		dcd->source_id = g_idle_add (g_directory_copy_next_child, dcd);
		return;
	}

	switch (g_file_info_get_file_type (child->info)) {
	case G_FILE_TYPE_DIRECTORY:
		/* FIXME: how to make a directory asynchronously ? */

		/* doesn't check the returned error for now, because when an
		 * error occurs the code is not returned (for example when
		 * a directory already exists the G_IO_ERROR_EXISTS code is
		 * *not* returned), so we cannot discriminate between warnings
		 * and fatal errors. (see bug #525155) */

		g_file_make_directory (dcd->current_destination,
				       NULL,
				       NULL);

		/*if (! g_file_make_directory (dcd->current_destination,
					     dcd->cancellable,
					     &(dcd->error)))
		{
			dcd->source_id = g_idle_add (g_directory_copy_done, dcd);
			return;
		}*/
		break;
	case G_FILE_TYPE_SYMBOLIC_LINK:
		/* FIXME: how to make a link asynchronously ? */

		g_file_make_symbolic_link (dcd->current_destination,
					   g_file_info_get_symlink_target (child->info),
					   NULL,
					   NULL);

		/*if (! g_file_make_symbolic_link (dcd->current_destination,
						 g_file_info_get_symlink_target (child->info),
						 dcd->cancellable,
						 &(dcd->error)))
		{
			dcd->source_id = g_idle_add (g_directory_copy_done, dcd);
			return;
		}*/
		break;
	case G_FILE_TYPE_REGULAR:
		g_file_copy_async (dcd->current_source,
				   dcd->current_destination,
				   dcd->flags,
				   dcd->io_priority,
				   dcd->cancellable,
				   g_directory_copy_child_progress_cb,
				   dcd,
				   g_directory_copy_child_done_cb,
				   dcd);
		async_op = TRUE;
		break;
	default:
		break;
	}

	if (! async_op)
		dcd->source_id = g_idle_add (g_directory_copy_next_child, dcd);
}


static gboolean
g_directory_copy_start_copying (gpointer user_data)
{
	DirectoryCopyData *dcd = user_data;

	g_source_remove (dcd->source_id);

	dcd->to_copy = g_list_reverse (dcd->to_copy);
	dcd->current = dcd->to_copy;
	dcd->n_file = 1;
	g_directory_copy_current_child (dcd);

	return FALSE;
}


static void
g_directory_copy_list_ready (GError   *error,
			     gpointer  user_data)
{
	DirectoryCopyData *dcd = user_data;

	if (error != NULL) {
		dcd->error = g_error_copy (error);
		dcd->source_id = g_idle_add (g_directory_copy_done, dcd);
		return;
	}

	dcd->source_id = g_idle_add (g_directory_copy_start_copying, dcd);
}


static void
g_directory_copy_for_each_file (GFile     *file,
				GFileInfo *info,
				gpointer   user_data)
{
	DirectoryCopyData *dcd = user_data;

	dcd->to_copy = g_list_prepend (dcd->to_copy, file_info_new (file, info));
	dcd->tot_files++;
}


static DirOp
g_directory_copy_start_dir (GFile      *file,
			    GFileInfo  *info,
			    GError    **error,
			    gpointer    user_data)
{
	DirectoryCopyData *dcd = user_data;

	dcd->to_copy = g_list_prepend (dcd->to_copy, file_info_new (file, info));
	dcd->tot_files++;

	return DIR_OP_CONTINUE;
}


void
g_directory_copy_async (GFile                 *source,
			GFile                 *destination,
			GFileCopyFlags         flags,
			int                    io_priority,
			GCancellable          *cancellable,
			CopyProgressCallback   progress_callback,
			gpointer               progress_callback_data,
			CopyDoneCallback       callback,
			gpointer               user_data)
{
	DirectoryCopyData *dcd;

	/* Creating GFile objects here will save us lot of effort in path construction */
	dcd = g_new0 (DirectoryCopyData, 1);
	dcd->source = g_object_ref (source);
	dcd->destination = g_object_ref (destination);
	dcd->flags = flags;
	dcd->io_priority = io_priority;
	dcd->cancellable = cancellable;
	dcd->progress_callback = progress_callback;
	dcd->progress_callback_data = progress_callback_data;
	dcd->callback = callback;
	dcd->user_data = user_data;

	g_directory_foreach_child (dcd->source,
			           TRUE,
			           TRUE,
			           NULL,
			           dcd->cancellable,
			           g_directory_copy_start_dir,
			           g_directory_copy_for_each_file,
			           g_directory_copy_list_ready,
			           dcd);
}


gboolean
g_load_file_in_buffer (GFile   *file,
	               void    *buffer,
	               gsize    size,
                       GError **error)
{
	GFileInputStream *istream;
	int               n;

	istream = g_file_read (file, NULL, error);
	if (istream == NULL)
		return FALSE;

	n = g_input_stream_read (G_INPUT_STREAM (istream), buffer, size, NULL, error);
	g_object_unref (istream);

	return (n >= 0);
}


/* -- _g_file_load_buffer_async -- */


#define MAX_BUFFER_SIZE_FOR_TMP_BUFFER 4096


typedef struct {
	GFile              *file;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
        gsize               requested_buffer_size;
	char               *buffer;
        gchar              *tmp_buffer;
        gsize               tmp_buffer_size;
        GInputStream       *stream;
        gsize               buffer_size;
} LoadBufferData;


static void
load_buffer_data_free (LoadBufferData *load_data)
{
	_g_object_unref (load_data->file);
	_g_object_unref (load_data->cancellable);
	_g_object_unref (load_data->result);
	g_free (load_data->buffer);
	g_free (load_data->tmp_buffer);
	_g_object_unref (load_data->stream);
	g_free (load_data);
}


static void
load_buffer_data_complete_with_error (LoadBufferData *load_data,
				      GError         *error)
{
	GSimpleAsyncResult *result;

	result = g_object_ref (load_data->result);
	g_simple_async_result_set_from_error (result, error);
	g_simple_async_result_complete_in_idle (result);

	g_object_unref (result);
}


static void
load_buffer_stream_read_ready_cb (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	LoadBufferData *load_data = user_data;
        GError         *error = NULL;
        gssize          count;

        count = g_input_stream_read_finish (load_data->stream, result, &error);
        if (count < 0) {
        	load_buffer_data_complete_with_error (load_data, error);
                return;
        }

        if (count > 0) {
		load_data->buffer = g_realloc (load_data->buffer, load_data->buffer_size + count + 1);
		memcpy (load_data->buffer + load_data->buffer_size, load_data->tmp_buffer, count);
		load_data->buffer_size += count;
        }

        if ((count == 0) || ((load_data->requested_buffer_size > 0) && (load_data->buffer_size >= load_data->requested_buffer_size))) {
                if (load_data->buffer != NULL)
                        ((char *)load_data->buffer)[load_data->buffer_size] = 0;
                g_simple_async_result_complete_in_idle (load_data->result);
                return;
        }

        g_input_stream_read_async (load_data->stream,
                                   load_data->tmp_buffer,
                                   load_data->tmp_buffer_size,
                                   G_PRIORITY_DEFAULT,
                                   load_data->cancellable,
                                   load_buffer_stream_read_ready_cb,
                                   load_data);
}


static void
load_buffer_read_ready_cb (GObject      *source_object,
			   GAsyncResult *result,
			   gpointer      user_data)
{
	LoadBufferData *load_data = user_data;
        GError         *error = NULL;

        load_data->stream = (GInputStream *) g_file_read_finish (G_FILE (source_object), result, &error);
        if (load_data->stream == NULL) {
        	load_buffer_data_complete_with_error (load_data, error);
                return;
        }

        g_input_stream_read_async (load_data->stream,
                                   load_data->tmp_buffer,
                                   load_data->tmp_buffer_size,
                                   G_PRIORITY_DEFAULT,
                                   load_data->cancellable,
                                   load_buffer_stream_read_ready_cb,
                                   load_data);
}


void
_g_file_load_buffer_async (GFile               *file,
			   gsize                requested_size,
			   GCancellable        *cancellable,
			   GAsyncReadyCallback  callback,
			   gpointer             user_data)
{
	LoadBufferData *load_data;

	g_return_if_fail (file != NULL);

	load_data = g_new0 (LoadBufferData, 1);
	load_data->file = g_object_ref (file);
	load_data->cancellable = _g_object_ref (cancellable);
	load_data->result = g_simple_async_result_new (G_OBJECT (file),
						       callback,
						       user_data,
						       _g_file_load_buffer_async);
	load_data->requested_buffer_size = requested_size;
	load_data->buffer = NULL;
	load_data->buffer_size = 0;
	if ((requested_size > 0) && (requested_size < MAX_BUFFER_SIZE_FOR_TMP_BUFFER))
		load_data->tmp_buffer_size = requested_size;
	else
		load_data->tmp_buffer_size = MAX_BUFFER_SIZE_FOR_TMP_BUFFER;
	load_data->tmp_buffer = g_new (char, load_data->tmp_buffer_size);
        g_simple_async_result_set_op_res_gpointer (load_data->result,
        					   load_data,
                                                   (GDestroyNotify) load_buffer_data_free);

        /* load a few bytes to guess the archive type */

	g_file_read_async (load_data->file,
			   G_PRIORITY_DEFAULT,
			   load_data->cancellable,
			   load_buffer_read_ready_cb,
			   load_data);
}


gboolean
_g_file_load_buffer_finish (GFile         *file,
			    GAsyncResult  *result,
			    char         **buffer,
			    gsize         *buffer_size,
			    GError       **error)
{
	GSimpleAsyncResult *simple;
	LoadBufferData     *load_data;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (file), _g_file_load_buffer_async), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	load_data = g_simple_async_result_get_op_res_gpointer (simple);
	if (buffer != NULL) {
		*buffer = load_data->buffer;
		load_data->buffer = NULL;
	}
	if (buffer_size != NULL)
		*buffer_size = load_data->buffer_size;

	return TRUE;
}


static gboolean
_g_file_make_directory_and_add_to_created_folders (GFile         *file,
						   GHashTable    *created_folders,
						   GCancellable  *cancellable,
						   GError       **error)
{
	gboolean result;

	result = g_file_make_directory (file, cancellable, error);
	if (result && (g_hash_table_lookup (created_folders, file) == NULL))
		g_hash_table_insert (created_folders, g_object_ref (file), GINT_TO_POINTER (1));

	return result;
}


gboolean
_g_file_make_directory_with_parents (GFile         *file,
				     GHashTable    *created_folders,
				     GCancellable  *cancellable,
				     GError       **error)
{
	GError *local_error = NULL;
	GFile  *work_file = NULL;
	GList  *list = NULL, *l;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	_g_file_make_directory_and_add_to_created_folders (file, created_folders, cancellable, &local_error);
	if ((local_error == NULL) || (local_error->code != G_IO_ERROR_NOT_FOUND)) {
		if (local_error != NULL)
			g_propagate_error (error, local_error);
		return local_error == NULL;
	}

	work_file = g_object_ref (file);
	while ((local_error != NULL) && (local_error->code == G_IO_ERROR_NOT_FOUND)) {
		GFile *parent_file;

		parent_file = g_file_get_parent (work_file);
		if (parent_file == NULL)
			break;

		g_clear_error (&local_error);
		_g_file_make_directory_and_add_to_created_folders (parent_file, created_folders, cancellable, &local_error);

		g_object_unref (work_file);
		work_file = g_object_ref (parent_file);

		if ((local_error != NULL) && (local_error->code == G_IO_ERROR_NOT_FOUND))
			list = g_list_prepend (list, parent_file);  /* Transfer ownership of ref */
		else
			g_object_unref (parent_file);
	}

	for (l = list; (local_error == NULL) && (l != NULL); l = l->next)
		_g_file_make_directory_and_add_to_created_folders ((GFile *) l->data, created_folders, cancellable, &local_error);

	_g_object_unref (work_file);
	_g_object_list_unref (list);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return _g_file_make_directory_and_add_to_created_folders (file, created_folders, cancellable, error);
}


char *
_g_file_get_display_name (GFile *file)
{
	GFileInfo *info;
	char *name = NULL;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info != NULL) {
		name = g_strdup (g_file_info_get_display_name (info));
		g_object_unref (info);
	}

	return (name != NULL) ? name : _g_file_get_display_basename (file);
}
