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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-unstuff.h"

static void fr_command_unstuff_class_init  (FRCommandUnstuffClass *class);
static void fr_command_unstuff_init        (FRCommand         *afile);
static void fr_command_unstuff_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;

/* recursive rmdir to remove the left-overs from unstuff */
static void
recursive_rmdir (const char *path)
{
	GDir *dir;
	const char *dirname;

	dir = g_dir_open (path, 0, NULL);
	if (dir == NULL)
		return;

	dirname = g_dir_read_name (dir);
	while (dirname != NULL)
	{
		char *full_path;

		if (strcmp (dirname, ".") == 0 || strcmp (dirname, "..") == 0)
			continue;

		full_path = g_build_filename (path, dirname, NULL);
		recursive_rmdir (full_path);
		g_free (full_path);

		dirname = g_dir_read_name (dir);
	}

	rmdir (path);

	g_dir_close (dir);
}


/* unstuff doesn't like file paths starting with /, that's so shite */
static char *
unstuff_is_shit_with_filenames (const char *orig)
{
	int i, num_slashes;
	char *current_dir, *filename;

	g_return_val_if_fail (orig != NULL, NULL);

	current_dir = g_get_current_dir ();
	i = num_slashes = 0;
	while (current_dir[i] != '\0') {
		if (current_dir[i]  == '/')
			num_slashes++;
		i++;
	}
	g_free (current_dir);

	/* 3 characters for each ../ plus filename length plus \0 */
	filename = g_malloc (3 * i + strlen (orig) + 1);
	i = 0;
	for ( ; num_slashes > 0 ; num_slashes--) {
		memcpy (filename + i, "../", 3);
		i+=3;
	}
	memcpy (filename + i, orig, strlen (orig) + 1);

	return filename;
}
	
static void
process_line (char     *line, 
	      gpointer  data)
{
	FRCommand        *comm = FR_COMMAND (data);
	FRCommandUnstuff *unstuff_comm = FR_COMMAND_UNSTUFF (comm);
	const char       *str_start;
	char             *filename, *real_filename;
	int               i;
	FileData         *fdata;

	g_return_if_fail (line != NULL);

	if (strstr (line, "progressEvent - ")) {
		const char *ssize;
		guint size;

		ssize = strstr (line, "progressEvent - ")
			+ strlen ("progressEvent - ");
		if (ssize[0] == '\0')
			size = 0;
		else
			size = atoi (ssize);

		if (comm->file_list != NULL) {
			fdata = (FileData *) comm->file_list->data;
			fdata->size = size;
		}
		return;
	}

	if (strstr (line, "fileEvent") == NULL)
		return;
	if (strstr (line, unstuff_comm->target_dir + 1) == NULL)
		return;

	/* Look for the filename, ends with a comma */
	str_start = strstr (line, unstuff_comm->target_dir + 1);
	str_start = str_start + strlen (unstuff_comm->target_dir) - 1;
	if (str_start[0] != '/')
		str_start--;
	if (str_start[0] == '.')
		str_start--;
	i = 0;
	while (str_start[i] != '\0' && str_start[i] != ',') {
		i++;
	}
	/* This is not supposed to happen */
	g_return_if_fail (str_start[i] != '\0');
	filename = g_strndup (str_start, i);

	/* Same thing with the real filename */
	str_start = strstr (line, unstuff_comm->target_dir);
	i = 0;
	while (str_start[i] != '\0' && str_start[i] != ',') {
		i++;
	}
	real_filename = g_strndup (str_start, i);

	fdata = file_data_new ();
	fdata->full_path = filename;
	fdata->link = NULL;
	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);
	fdata->size = 0;
	fdata->modified = time (NULL);

	comm->file_list = g_list_prepend (comm->file_list, fdata);

	unlink (real_filename);
	g_free (real_filename);
}


static void
fr_command_unstuff_list (FRCommand *comm)
{
	char *arg, *path, *template;
	char *filename;
	char *path_dots;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line,
				      comm);

	fr_process_begin_command (comm->process, "unstuff");
	fr_process_add_arg (comm->process, "--trace");

	/* Actually unpack everything in a temporary directory */
	path = get_temp_work_dir_name ();
	ensure_dir_exists (path, 0700);
	path_dots = unstuff_is_shit_with_filenames (path);
	g_free (path);

	arg = g_strdup_printf ("-d=%s", path_dots);
	FR_COMMAND_UNSTUFF (comm)->target_dir = path_dots;
	fr_process_add_arg (comm->process, arg);
	g_free (arg);

	filename = unstuff_is_shit_with_filenames (comm->e_filename);
	fr_process_add_arg (comm->process, filename);
	g_free (filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_unstuff_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
#if 0
	GList *scan;
#endif
	char  *filename;

	fr_process_begin_command (comm->process, "unstuff");
	fr_process_add_arg (comm->process, "x");

	if (dest_dir != NULL) {
		gchar *e_dest_dir = shell_escape (dest_dir);
		gchar *e_dest_dir_dots = unstuff_is_shit_with_filenames (e_dest_dir);
		gchar *arg = g_strdup_printf ("-d=%s", e_dest_dir_dots);
		fr_process_add_arg (comm->process, arg);
		FR_COMMAND_UNSTUFF (comm)->target_dir = NULL;
		g_free (arg);
		g_free (e_dest_dir);
		g_free (e_dest_dir_dots);
	}

	fr_process_add_arg (comm->process, "--trace");

	/* unstuff doesn't like file paths starting with /, that's so shite */
	filename = unstuff_is_shit_with_filenames (comm->e_filename);
	fr_process_add_arg (comm->process, filename);
	g_free (filename);

	/* FIXME it is not possible to unpack only some files */
#if 0
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
#endif

	fr_process_end_command (comm->process);
}


static void
fr_command_unstuff_handle_error (FRCommand   *comm, 
			     FRProcError *error)
{
	if ((error->type == FR_PROC_ERROR_GENERIC) 
	    && (error->status <= 1))
		error->type = FR_PROC_ERROR_NONE;
}


static void 
fr_command_unstuff_class_init (FRCommandUnstuffClass *class)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_unstuff_finalize;

        afc->list         = fr_command_unstuff_list;
	afc->add          = NULL;
	afc->delete       = NULL;
	afc->extract      = fr_command_unstuff_extract;
	afc->handle_error = fr_command_unstuff_handle_error;
}

 
static void 
fr_command_unstuff_init (FRCommand *comm)
{
	comm->propCanModify                = FALSE;
	comm->propAddCanUpdate             = FALSE;
	comm->propAddCanReplace            = FALSE;
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = TRUE;
	comm->propTest                     = FALSE;
}


static void 
fr_command_unstuff_finalize (GObject *object)
{
	FRCommandUnstuff *unstuff_comm = FR_COMMAND_UNSTUFF (object);
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_UNSTUFF (object));

	if (unstuff_comm->target_dir != NULL) {
		recursive_rmdir (unstuff_comm->target_dir);
		g_free (unstuff_comm->target_dir);
	}

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_unstuff_get_type ()
{
        static guint type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandUnstuffClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_unstuff_class_init,
			NULL,
			NULL,
			sizeof (FRCommandUnstuff),
			0,
			(GInstanceInitFunc) fr_command_unstuff_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandUnstuff",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_unstuff_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_UNSTUFF, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
