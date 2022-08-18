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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-unstuff.h"
#include "glib-utils.h"


struct _FrCommandUnstuff
{
	FrCommand  parent_instance;

	char      *target_dir;
	FrFileData *fdata;
};


G_DEFINE_TYPE (FrCommandUnstuff, fr_command_unstuff, fr_command_get_type ())


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
	FrCommand        *comm = FR_COMMAND (data);
	FrCommandUnstuff *unstuff_comm = FR_COMMAND_UNSTUFF (comm);
	const char       *str_start;
	char             *filename, *real_filename;
	int               i;
	FrFileData *fdata;

	g_return_if_fail (line != NULL);

	if (strstr (line, "progressEvent - ")) {
		const char *ssize;
		guint size;

		ssize = strstr (line, "progressEvent - ")
			+ strlen ("progressEvent - ");
		if (ssize[0] == '\0')
			size = 0;
		else
			size = g_ascii_strtoull (ssize, NULL, 10);

		if (unstuff_comm->fdata != NULL)
			unstuff_comm->fdata->size = size;

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

	fdata = fr_file_data_new ();
	fdata->full_path = filename;
	fdata->original_path = filename;
	fdata->link = NULL;
	fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
	fdata->path = _g_path_remove_level (fdata->full_path);

	fdata->size = 0;
	fdata->modified = time (NULL);

	unstuff_comm->fdata = fdata;
	fr_archive_add_file (FR_ARCHIVE (comm), fdata);

	unlink (real_filename);
	g_free (real_filename);
}


static void
list__begin (gpointer data)
{
	FrCommandUnstuff *comm = data;

	comm->fdata = NULL;
}


static gboolean
fr_command_unstuff_list (FrCommand *comm)
{
	char *arg, *path;
	char *filename;
	char *path_dots;

	fr_process_set_out_line_func (comm->process, process_line, comm);

	fr_process_begin_command (comm->process, "unstuff");
	fr_process_set_begin_func (comm->process, list__begin, comm);
	fr_process_add_arg (comm->process, "--trace");

	/* Actually unpack everything in a temporary directory */
	path = _g_path_get_temp_work_dir (NULL);
	path_dots = unstuff_is_shit_with_filenames (path);
	g_free (path);

	arg = g_strdup_printf ("-d=%s", path_dots);
	FR_COMMAND_UNSTUFF (comm)->target_dir = path_dots;
	fr_process_add_arg (comm->process, arg);
	g_free (arg);

	filename = unstuff_is_shit_with_filenames (comm->filename);
	fr_process_add_arg (comm->process, filename);
	g_free (filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_unstuff_extract (FrCommand  *comm,
			    const char  *from_file,
			    GList      *file_list,
			    const char *dest_dir,
			    gboolean    overwrite,
			    gboolean    skip_older,
			    gboolean    junk_paths)
{
#if 0
	GList *scan;
#endif
	char  *filename;

	fr_process_begin_command (comm->process, "unstuff");

	if (dest_dir != NULL) {
		char *dest_dir_dots;
		char *arg;

		dest_dir_dots = unstuff_is_shit_with_filenames (dest_dir);
		arg = g_strdup_printf ("-d=%s", dest_dir_dots);
		fr_process_add_arg (comm->process, arg);
		FR_COMMAND_UNSTUFF (comm)->target_dir = NULL;
		g_free (arg);
		g_free (dest_dir_dots);
	}

	fr_process_add_arg (comm->process, "--trace");

	/* unstuff doesn't like file paths starting with /, that's so shite */
	filename = unstuff_is_shit_with_filenames (comm->filename);
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
fr_command_unstuff_handle_error (FrCommand   *comm,
				 FrError *error)
{
	if ((error->type != FR_ERROR_NONE)
	    && (error->status <= 1))
	{
		fr_error_clear_gerror (error);
	}
}


const char *unstuff_mime_type[] = { "application/x-stuffit", NULL };


static const char **
fr_command_unstuff_get_mime_types (FrArchive *archive)
{
	return unstuff_mime_type;
}


static FrArchiveCaps
fr_command_unstuff_get_capabilities (FrArchive  *archive,
			             const char *mime_type,
				     gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("unstuff", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_unstuff_get_packages (FrArchive  *archive,
			         const char *mime_type)
{
	return FR_PACKAGES ("unstaff");
}


static void
fr_command_unstuff_finalize (GObject *object)
{
	FrCommandUnstuff *self;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_UNSTUFF (object));

	self = FR_COMMAND_UNSTUFF (object);

	if (self->target_dir != NULL) {
		recursive_rmdir (self->target_dir);
		g_free (self->target_dir);
	}

	if (G_OBJECT_CLASS (fr_command_unstuff_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_unstuff_parent_class)->finalize (object);
}


static void
fr_command_unstuff_class_init (FrCommandUnstuffClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_unstuff_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_unstuff_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_unstuff_get_mime_types;
	archive_class->get_capabilities = fr_command_unstuff_get_capabilities;
	archive_class->get_packages     = fr_command_unstuff_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_unstuff_list;
	command_class->extract          = fr_command_unstuff_extract;
	command_class->handle_error     = fr_command_unstuff_handle_error;
}


static void
fr_command_unstuff_init (FrCommandUnstuff *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = FALSE;
	base->propAddCanReplace            = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = TRUE;
	base->propTest                     = FALSE;
}
