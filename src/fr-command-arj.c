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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-arj.h"

static void fr_command_arj_class_init  (FRCommandArjClass *class);
static void fr_command_arj_init        (FRCommand         *afile);
static void fr_command_arj_finalize    (GObject           *object);

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
		/* warning : this will work until 2075 ;) */
		int y = atoi (fields[0]);
		if (y >= 75)
			tm.tm_year = y;
		else
			tm.tm_year = 100 + y;

		tm.tm_mon = atoi (fields[1]) - 1;
		tm.tm_mday = atoi (fields[2]);
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
	fields[n_fields] = NULL;

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
	int   n = 2;

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
	FRCommand     *comm = FR_COMMAND (data);
	FRCommandArj  *arj_comm = FR_COMMAND_ARJ (comm);

	g_return_if_fail (line != NULL);

	if (! arj_comm->list_started) {
		if (strncmp (line, "--------", 8) == 0) {
			arj_comm->list_started = TRUE;
			arj_comm->line_no = 1;
		}
		return;
	}

	if (strncmp (line, "--------", 8) == 0) {
		arj_comm->list_started = FALSE;
		return;
	}

	if (arj_comm->line_no == 4) {
		arj_comm->line_no = 1;
		return;

	} else if (arj_comm->line_no == 1) { /* Read the filename. */
		FileData   *fdata;
		const char *name_field;

		arj_comm->fdata = fdata = file_data_new ();

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

	} else if (arj_comm->line_no == 2) { /* Read file size and date. */
		FileData  *fdata;
		char     **fields;

		fdata = arj_comm->fdata;

		/* read file info. */
		
		fields = split_line (line, 7);
		fdata->size = atol (fields[2]);
		fdata->modified = mktime_from_string (fields[5], fields[6]); 
		g_strfreev (fields);
		
		if (*fdata->name == 0)
			file_data_free (fdata);
		else
			comm->file_list = g_list_prepend (comm->file_list, fdata);
		arj_comm->fdata = NULL;
	}

	arj_comm->line_no++;
}


static void
fr_command_arj_list (FRCommand *comm)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_process_clear (comm->process);
	fr_process_begin_command (comm->process, "arj");
	fr_process_add_arg (comm->process, "v");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_arj_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList *scan;

	fr_process_begin_command (comm->process, "arj");

	fr_process_add_arg (comm->process, "a");

	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);

	/* preserve links. */
	fr_process_add_arg (comm->process, "-y");

	if (update)
		fr_process_add_arg (comm->process, "-u");

	if (password != NULL) {
		char *swtch = g_strconcat ("-g/", password, NULL);
		fr_process_add_arg (comm->process, swtch);
		g_free (swtch);
	}

	switch (compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-m3"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-m2"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-m1"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-m1"); break;
	}

	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-");

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, (gchar*) scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_arj_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "arj");
	fr_process_add_arg (comm->process, "d");

	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-");

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_arj_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_begin_command (comm->process, "arj");

	if (junk_paths)
		fr_process_add_arg (comm->process, "e");
	else
		fr_process_add_arg (comm->process, "x");

	if (dest_dir != NULL) {
		char *e_dest_dir = shell_escape (dest_dir);
		char *swtch = g_strconcat ("-ht/", e_dest_dir, NULL);
		fr_process_add_arg (comm->process, swtch);
		g_free (swtch);
		g_free (e_dest_dir);
	}

	if (! overwrite)
		fr_process_add_arg (comm->process, "-n");

	if (skip_older)
		fr_process_add_arg (comm->process, "-u");

	if (password != NULL) {
		char *swtch = g_strconcat ("-g/", password, NULL);
		fr_process_add_arg (comm->process, swtch);
		g_free (swtch);
	}

	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-");

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_arj_test (FRCommand   *comm,
		     const char  *password)
{
	fr_process_begin_command (comm->process, "arj");
	fr_process_add_arg (comm->process, "t");
	if (password != NULL) {
		char *swtch = g_strconcat ("-g/", password, NULL);
		fr_process_add_arg (comm->process, swtch);
		g_free (swtch);
	}
	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_arj_handle_error (FRCommand   *comm, 
			     FRProcError *error)
{
	/* FIXME
	if ((error->type == FR_PROC_ERROR_GENERIC) 
	    && (error->status <= 1))
		error->type = FR_PROC_ERROR_NONE;
	*/
}


static void 
fr_command_arj_class_init (FRCommandArjClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_arj_finalize;

        afc->list           = fr_command_arj_list;
	afc->add            = fr_command_arj_add;
	afc->delete         = fr_command_arj_delete;
	afc->extract        = fr_command_arj_extract;
	afc->test           = fr_command_arj_test;
	afc->handle_error   = fr_command_arj_handle_error;
}

 
static void 
fr_command_arj_init (FRCommand *comm)
{
	comm->propAddCanUpdate             = TRUE; 
	comm->propAddCanReplace            = TRUE; 
	comm->propExtractCanAvoidOverwrite = TRUE;
	comm->propExtractCanSkipOlder      = TRUE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = TRUE;
	comm->propTest                     = TRUE;

	FR_COMMAND_ARJ (comm)->list_started = FALSE;
	FR_COMMAND_ARJ (comm)->fdata = FALSE;
}


static void 
fr_command_arj_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_ARJ (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_arj_get_type ()
{
        static guint type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandArjClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_arj_class_init,
			NULL,
			NULL,
			sizeof (FRCommandArj),
			0,
			(GInstanceInitFunc) fr_command_arj_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandArj",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_arj_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_ARJ, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
