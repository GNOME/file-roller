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

#include <glib.h>
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-zip.h"

#define EMPTY_ARCHIVE_WARNING        "zipfile is empty"
#define EMPTY_ARCHIVE_WARNING_LENGTH 16

static void fr_command_zip_class_init  (FRCommandZipClass *class);
static void fr_command_zip_init        (FRCommand         *afile);
static void fr_command_zip_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *date_s, 
		    char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

	/* date */

	fields = g_strsplit (date_s, "-", 3);
	if (fields[0] != NULL) {
		tm.tm_mon = atoi (fields[0]) - 1;
		if (fields[1] != NULL) {
			tm.tm_mday = atoi (fields[1]);
			if (fields[2] != NULL) { 
				/* warning : this will work until 2075 ;) */
				int y = atoi (fields[2]);
				if (y >= 75)
					tm.tm_year = atoi (fields[2]);
				else
					tm.tm_year = 100 + atoi (fields[2]);
			}
		}
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 3);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_min = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static char*
zip_escape (const char *str)
{
	return escape_str (str, "*?[]");
}


static char*
prepend_path_separator (const char *str)
{
	if (*str == '-' || g_str_has_prefix (str, "\\-")) 
		return g_strconcat (".", G_DIR_SEPARATOR_S, str, NULL);
	else 
		return g_strdup (str);
}


static char*
prepend_path_separator_zip_escape (const char *str)
{
	char *tmp1, *tmp2;

	tmp2 = prepend_path_separator (str);
	tmp1 = zip_escape (tmp2);
	g_free (tmp2);
	
	return tmp1;
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FileData    *fdata;
	FRCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;
	gint         line_l;

	g_return_if_fail (line != NULL);

	/* check whether unzip gave the empty archive warning. */

	if (FR_COMMAND_ZIP (comm)->is_empty)
		return;

	line_l = strlen (line);
	if (line_l > EMPTY_ARCHIVE_WARNING_LENGTH) 
		if (strcmp (line + line_l - EMPTY_ARCHIVE_WARNING_LENGTH, 
			    EMPTY_ARCHIVE_WARNING) == 0) {
			FR_COMMAND_ZIP (comm)->is_empty = TRUE;
			return;
		}

	/**/

	fdata = file_data_new ();

	fields = split_line (line, 7);
	fdata->size = atol (fields[0]);
	fdata->modified = mktime_from_string (fields[4], fields[5]);
	g_strfreev (fields);

	/* Full path */

	name_field = get_last_field (line, 8);

	if (*name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	fdata->link = NULL;

	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else
		comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
add_filename_arg (FRCommand *comm) 
{
	char *temp = prepend_path_separator (comm->e_filename);
	fr_process_add_arg (comm->process, temp);
	g_free (temp);
}


static void
add_password_arg (FRCommand     *comm,
		  const char    *password)
{
	fr_process_add_arg (comm->process, "-P");

	if (password != NULL) {
		char *arg;
		char *e_password;

		e_password = escape_str (password, "\"");
		arg = g_strconcat ("\"", e_password, "\"", NULL);
		g_free (e_password);

		fr_process_add_arg (comm->process, arg);
		g_free (arg);
	} else
		fr_process_add_arg (comm->process, "\"\"");
}


static void
fr_command_zip_list (FRCommand *comm)
{


	FR_COMMAND_ZIP (comm)->is_empty = FALSE;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-qq");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-l");
	add_filename_arg (comm);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
process_line__common (char     *line, 
		      gpointer  data)
{
	FRCommand  *comm = FR_COMMAND (data);

	if (line == NULL)
		return;

	fr_command_message (comm, line);

	if (comm->n_files != 0) {
		double fraction = (double) comm->n_file++ / comm->n_files;
		fr_command_progress (comm, fraction);
	}

}


static void
fr_command_zip_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");

	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);

	/* preserve links. */
	fr_process_add_arg (comm->process, "-y");

	if (update)
		fr_process_add_arg (comm->process, "-u");

	add_password_arg (comm, password);

	switch (compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-3"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-6"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-9"); break;
	}

	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator_zip_escape ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");
	fr_process_add_arg (comm->process, "-d");
	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator_zip_escape ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "unzip");
	
	if (dest_dir != NULL) {
		char *e_dest_dir = shell_escape (dest_dir);
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, e_dest_dir);
		g_free (e_dest_dir);
	}

	if (overwrite)
		fr_process_add_arg (comm->process, "-o");
	else
		fr_process_add_arg (comm->process, "-n");

	if (skip_older)
		fr_process_add_arg (comm->process, "-u");

	if (junk_paths)
		fr_process_add_arg (comm->process, "-j");

	add_password_arg (comm, password);

	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator_zip_escape ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_test (FRCommand   *comm,
		     const char  *password)
{
	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-t");
	add_password_arg (comm, password);
	add_filename_arg (comm);
	fr_process_end_command (comm->process);
}


static void
fr_command_zip_handle_error (FRCommand   *comm, 
			     FRProcError *error)
{
	if (error->type == FR_PROC_ERROR_GENERIC) {
		if (error->status <= 1)
			error->type = FR_PROC_ERROR_NONE;
		else if (error->status == 82)
			error->type = FR_PROC_ERROR_ASK_PASSWORD;
	}
}


static void 
fr_command_zip_class_init (FRCommandZipClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_zip_finalize;

        afc->list           = fr_command_zip_list;
	afc->add            = fr_command_zip_add;
	afc->delete         = fr_command_zip_delete;
	afc->extract        = fr_command_zip_extract;
	afc->test           = fr_command_zip_test;
	afc->handle_error   = fr_command_zip_handle_error;
}

 
static void 
fr_command_zip_init (FRCommand *comm)
{
	comm->propAddCanUpdate             = TRUE; 
	comm->propAddCanReplace            = TRUE; 
	comm->propExtractCanAvoidOverwrite = TRUE;
	comm->propExtractCanSkipOlder      = TRUE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = TRUE;
	comm->propTest                     = TRUE;

	FR_COMMAND_ZIP (comm)->is_empty = FALSE;
}


static void 
fr_command_zip_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_ZIP (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_zip_get_type ()
{
        static guint type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandZipClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_zip_class_init,
			NULL,
			NULL,
			sizeof (FRCommandZip),
			0,
			(GInstanceInitFunc) fr_command_zip_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandZip",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_zip_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_ZIP, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
