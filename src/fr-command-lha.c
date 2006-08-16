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
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-lha.h"

static void fr_command_lha_class_init  (FRCommandLhaClass *class);
static void fr_command_lha_init        (FRCommand         *afile);
static void fr_command_lha_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *month, 
		    char *mday, 
		    char *time_or_year)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm     tm = {0, };
	char        **fields;

	/* date */

	if (month != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month) == 0) {
				tm.tm_mon = i;
				break;
			}
	}
	tm.tm_mday = atoi (mday);
	if (strchr (time_or_year, ':') == NULL)
		tm.tm_year = atoi (time_or_year) - 1900;
	else {
		time_t     now;
		struct tm *tm_now;

		now = time (NULL);
		tm_now = localtime (&now);
		if (tm_now != NULL)
			tm.tm_year = tm_now->tm_year;

		/* time */

		fields = g_strsplit (time_or_year, ":", 2);
		if (fields[0] != NULL) {
			tm.tm_hour = atoi (fields[0]) - 1;
			if (fields[1] != NULL) 
				tm.tm_min = atoi (fields[1]);
		}
		g_strfreev (fields);
	}

	return mktime (&tm);
}


static char **
split_line_lha (char *line)
{
	char       **fields;
	int          n_fields = 7;
	const char  *scan, *field_end;
	int          i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields] = NULL;

	i = 0;

	if (strncmp (line, "[MS-DOS]", 8) == 0) {
		fields[i++] = g_strdup ("");
		fields[i++] = g_strdup ("");
		line += strlen ("[MS-DOS]");

	} else if (strncmp (line, "[generic]", 9) == 0) {
		fields[i++] = g_strdup ("");
		fields[i++] = g_strdup ("");
		line += strlen ("[generic]");
	}

	scan = eat_spaces (line);
	for (; i < n_fields; i++) {
		field_end = strchr (scan, ' ');
		if (field_end != NULL) {
			fields[i] = g_strndup (scan, field_end - scan);
			scan = eat_spaces (field_end);
		}
	}

	return fields;
}


static const char *
get_last_field_lha (char *line)
{
	int         i;
	const char *field;
	int         n = 7;

	if (strncmp (line, "[MS-DOS]", 8) == 0) 
		n--;

	if (strncmp (line, "[generic]", 9) == 0) 
		n--;

	field = eat_spaces (line);
	for (i = 0; i < n; i++) {
		field = strchr (field, ' ');
		field = eat_spaces (field);
	}

	return field;
}


static void
process_line (char     *line, 
	      gpointer  data)
{
	FileData    *fdata;
	FRCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;

	g_return_if_fail (line != NULL);

	fdata = file_data_new ();

	fields = split_line_lha (line);
	fdata->size = g_ascii_strtoull (fields[2], NULL, 10);
	fdata->modified = mktime_from_string (fields[4], 
					      fields[5], 
					      fields[6]);
	g_strfreev (fields);

	/* Full path */

	name_field = get_last_field_lha (line);

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

	if (*fdata->name == 0)
		file_data_free (fdata);
	else
		comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
fr_command_lha_list (FRCommand  *comm,
		     const char *password)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line,
				      comm);

	fr_process_begin_command (comm->process, "lha");
	fr_process_add_arg (comm->process, "lq");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_lha_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList *scan;

	fr_process_begin_command (comm->process, "lha");
	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);
	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");
	fr_process_add_arg (comm->process, comm->e_filename);
	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, (gchar*) scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_lha_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "lha");
	fr_process_add_arg (comm->process, "d");
	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_lha_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;
	char   options[5];
	int    i = 0;

	fr_process_begin_command (comm->process, "lha");
	
	if (dest_dir != NULL) 
		fr_process_set_working_dir (comm->process, dest_dir);

	options[i++] = 'x';
	options[i++] = 'f'; /* Always overwrite.
			     * The overwrite option is handled in 
			     * fr_archive_extract,
			     * this is because lha asks the user whether he
			     * wants to overwrite a file. */

	if (junk_paths)
		options[i++] = 'i';
	
	options[i++] = 0;
	fr_process_add_arg (comm->process, options);
	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void 
fr_command_lha_class_init (FRCommandLhaClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_lha_finalize;

        afc->list         = fr_command_lha_list;
	afc->add          = fr_command_lha_add;
	afc->delete       = fr_command_lha_delete;
	afc->extract      = fr_command_lha_extract;
}

 
static void 
fr_command_lha_init (FRCommand *comm)
{
	comm->file_type = FR_FILE_TYPE_LHA;

	comm->propAddCanUpdate             = TRUE; 
	comm->propAddCanReplace            = TRUE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;
}


static void 
fr_command_lha_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_LHA (object));

	 /* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_lha_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandLhaClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_lha_class_init,
			NULL,
			NULL,
			sizeof (FRCommandLha),
			0,
			(GInstanceInitFunc) fr_command_lha_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandLha",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_lha_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	if (!is_program_in_path("lha")) {
		return NULL;
	}

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_LHA, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
