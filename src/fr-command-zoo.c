/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2003 Free Software Foundation, Inc.
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
#include "fr-command-zoo.h"

static void fr_command_zoo_class_init  (FRCommandZooClass *class);
static void fr_command_zoo_init        (FRCommand         *afile);
static void fr_command_zoo_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_zoo_string (char *mday_s,
			char *month_s,
			char *year_s,
			char *time_s)
{
	struct tm  tm = {0, };
	char **fields;
	int year;

	/* This will break in 2075 */
	year = atoi (year_s);
	if (year >= 75) {
		tm.tm_year = year;
	} else {
		tm.tm_year = 100 + year;
	}

	if (g_ascii_strncasecmp(month_s, "Jan", 3) == 0) {
		tm.tm_mon = 0;
	} else if (g_ascii_strncasecmp(month_s, "Feb", 3) == 0) {
		tm.tm_mon = 1;
	} else if (g_ascii_strncasecmp(month_s, "Mar", 3) == 0) {
		tm.tm_mon = 2;
	} else if (g_ascii_strncasecmp(month_s, "Apr", 3) == 0) {
		tm.tm_mon = 3;
	} else if (g_ascii_strncasecmp(month_s, "May", 3) == 0) {
		tm.tm_mon = 4;
	} else if (g_ascii_strncasecmp(month_s, "Jun", 3) == 0) {
		tm.tm_mon = 5;
	} else if (g_ascii_strncasecmp(month_s, "Jul", 3) == 0) {
		tm.tm_mon = 6;
	} else if (g_ascii_strncasecmp(month_s, "Aug", 3) == 0) {
		tm.tm_mon = 7;
	} else if (g_ascii_strncasecmp(month_s, "Sep", 3) == 0) {
		tm.tm_mon = 8;
	} else if (g_ascii_strncasecmp(month_s, "Oct", 3) == 0) {
		tm.tm_mon = 9;
	} else if (g_ascii_strncasecmp(month_s, "Nov", 3) == 0) {
		tm.tm_mon = 10;
	} else if (g_ascii_strncasecmp(month_s, "Dec", 3) == 0) {
		tm.tm_mon = 11;
	}

	tm.tm_mday = atoi (mday_s);

	fields = g_strsplit (time_s, ":", 3);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_min  = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec  = atoi (fields[2]);
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
split_zoo_line (char *line)
{
	char **fields;
	char *scan, *field_end;
	int i;

	if (line[0] == '-') {
		return NULL;
	}

	fields = g_new0 (char *, 6);
	fields[5] = NULL;

	// Get Length
	scan = eat_spaces (line);
	field_end = strchr (scan, ' ');
	fields[0] = g_strndup (scan, field_end - scan);
	scan = eat_spaces (field_end);

	// Toss CF, Size Now
	for (i = 0; i < 2; i++) {
		field_end = strchr (scan, ' ');
		scan = eat_spaces (field_end);
	}

	// Get Day, Month, Year, Time
	for (i = 1; i < 5; i++) {
		if (i == 2 && g_ascii_strncasecmp (scan, "file", 4) == 0) {
			g_strfreev(fields);
			return NULL;
		}
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
	int   n = 6;

	field = eat_spaces (line);
	for (i = 0; i < n; i++) {
		field = strchr (field, ' ');
		field = eat_spaces (field);
	}
	field = strchr (field, ' ');
	if (g_ascii_strncasecmp (field, " C ", 3) == 0) {
		field = eat_spaces (field);
		field = strchr (field, ' ');
		field = eat_spaces (field);
	} else 
		field = eat_spaces (field);

	return field;
}


static void
process_zoo_line (char     *line, 
		  gpointer  data)
{
	FileData   *fdata;
	FRCommand  *zoo_comm = FR_COMMAND (data);
	char      **fields;
	char       *name_field;

	g_return_if_fail (line != NULL);
	if (line[0] == '-')
		return;

	fields = split_zoo_line (line);
	if (fields == NULL)
		return;

	fdata = file_data_new ();

	fdata->size = atol (fields[0]);
	fdata->modified = mktime_from_zoo_string (fields[1],
						  fields[2],
						  fields[3],
						  fields[4]);
	g_strfreev (fields);

	/* Full path */

	name_field = get_last_field (line);
	if (*(name_field) == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else
		zoo_comm->file_list = g_list_prepend (zoo_comm->file_list, fdata);
}


static void
fr_command_zoo_list (FRCommand *zoo_comm)
{
	fr_process_set_out_line_func (FR_COMMAND (zoo_comm)->process,
				      process_zoo_line,
				      zoo_comm);

	fr_process_clear (zoo_comm->process);
	fr_process_begin_command (zoo_comm->process, "zoo");
	fr_process_add_arg (zoo_comm->process, "lq");
	fr_process_add_arg (zoo_comm->process, zoo_comm->e_filename);
	fr_process_end_command (zoo_comm->process);
	fr_process_start (zoo_comm->process);
}


static void
fr_command_zoo_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList        *scan;

	/* Add files. */

	fr_process_begin_command (comm->process, "zoo");

	fr_process_set_working_dir (comm->process, base_dir);

	if (update) 
		fr_process_add_arg (comm->process, "auP");
	else
		fr_process_add_arg (comm->process, "aP");

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_zoo_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList        *scan;

	/* Delete files. */

	fr_process_begin_command (comm->process, "zoo");
	fr_process_add_arg (comm->process, "DP");
	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_zoo_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_begin_command (comm->process, "zoo");

	if (overwrite)
		fr_process_add_arg (comm->process, "xO");
	else
		fr_process_add_arg (comm->process, "x");

	fr_process_add_arg (comm->process, comm->e_filename);

	if (dest_dir != NULL)
		fr_process_set_working_dir (comm->process, dest_dir);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_zoo_test (FRCommand   *comm,
		     const char  *password)
{
	fr_process_begin_command (comm->process, "zoo");
	fr_process_add_arg (comm->process, "-test");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
}


static void 
fr_command_zoo_class_init (FRCommandZooClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_zoo_finalize;

        afc->list         = fr_command_zoo_list;
	afc->add          = fr_command_zoo_add;
	afc->delete       = fr_command_zoo_delete;
	afc->extract      = fr_command_zoo_extract;
	afc->test         = fr_command_zoo_test;
}

 
static void 
fr_command_zoo_init (FRCommand *comm)
{
	comm->propAddCanUpdate             = TRUE;
	comm->propAddCanReplace            = FALSE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = TRUE;
}


static void 
fr_command_zoo_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_ZOO (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_zoo_get_type ()
{
        static guint type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandZooClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_zoo_class_init,
			NULL,
			NULL,
			sizeof (FRCommandZoo),
			0,
			(GInstanceInitFunc) fr_command_zoo_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandZoo",
					       &type_info,
					       0);
        }

        return type;
}

FRCommand *
fr_command_zoo_new (FRProcess         *process,
		    const char        *filename)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_ZOO, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
