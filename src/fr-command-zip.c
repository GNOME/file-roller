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
		tm.tm_hour = atoi (fields[0]) - 1;
		if (fields[1] != NULL) {
			tm.tm_min = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static char *
eat_spaces (char *line)
{
	while ((*line == ' ') && (*line != 0))
		line++;
	return line;
}


static char **
split_line (char *line, 
	    int   n_fields)
{
	char **fields;
	char  *scan, *field_end;
	int    i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields + 1] = NULL;

	scan = eat_spaces (line);
	for (i = 0; i < n_fields; i++) {
		field_end = strchr (scan, ' ');
		fields[i] = g_strndup (scan, field_end - scan);
		scan = eat_spaces (field_end);
	}

	return fields;
}


static char *
get_last_field (char *line)
{
	int   i;
	char *field;
	int   n = 8;

	n--;
	field = eat_spaces (line);
	for (i = 0; i < n; i++) {
		field = strchr (field, ' ');
		field = eat_spaces (field);
	}

	return field;
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FileData   *fdata;
	FRCommand  *comm = FR_COMMAND (data);
	char      **fields;
	char       *name_field;
	gint        line_l;

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

	name_field = get_last_field (line);

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
fr_command_zip_list (FRCommand *comm)
{
	FR_COMMAND_ZIP (comm)->is_empty = FALSE;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_process_clear (comm->process);
	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-qq");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-l");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_zip_add (FRCommand   *comm,
		    GList       *file_list,
		    const char  *base_dir,
		    gboolean     update,
		    const char  *password)
{
	GList *scan;

	fr_process_begin_command (comm->process, "zip");

	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);

	/* preserve links. */
	fr_process_add_arg (comm->process, "-y");

	if (update)
		fr_process_add_arg (comm->process, "-u");

	if (password != NULL) {
		fr_process_add_arg (comm->process, "-P");
		fr_process_add_arg (comm->process, password);
	}

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, (gchar*) scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "zip");
	fr_process_add_arg (comm->process, "-d");
	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
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

	fr_process_begin_command (comm->process, "unzip");
	
	if (dest_dir != NULL) {
		gchar *e_dest_dir = shell_escape (dest_dir);
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

	if (password != NULL) {
		fr_process_add_arg (comm->process, "-P");
		fr_process_add_arg (comm->process, password);
	}

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_test (FRCommand   *comm,
		     const char  *password)
{
	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-t");

	if (password != NULL) {
		fr_process_add_arg (comm->process, "-P");
		fr_process_add_arg (comm->process, password);
	}

	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_zip_handle_error (FRCommand   *comm, 
			     FRProcError *error)
{
	if ((error->type == FR_PROC_ERROR_GENERIC) 
	    && (error->status <= 1))
		error->type = FR_PROC_ERROR_NONE;
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
