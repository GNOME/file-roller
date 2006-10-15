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

#include <config.h>
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
#include "fr-command-rpm.h"

static void fr_command_rpm_class_init  (FRCommandRpmClass *class);
static void fr_command_rpm_init        (FRCommand         *afile);
static void fr_command_rpm_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *month, 
		    char *mday,
		    char *year)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm     tm = {0, };

	if (month != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month) == 0) {
				tm.tm_mon = i;
				break;
			}
	}
	tm.tm_mday = atoi (mday);
	if (strchr (year, ':') != NULL) {
		char **fields = g_strsplit (year, ":", 2);
        	if (n_fields (fields) == 2) {
	        	time_t      now;
        		struct tm  *now_tm;

	  		tm.tm_hour = atoi (fields[0]) - 1;
	  		tm.tm_min = atoi (fields[1]);
	  
	  		now = time(NULL);
	  		now_tm = localtime (&now);
	  		tm.tm_year = now_tm->tm_year;
        	}
	} else
		tm.tm_year = atoi (year) - 1900;

	return mktime (&tm);
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FileData    *fdata;
	FRCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;

	g_return_if_fail (line != NULL);

	if (line[0] == 'd') /* Ignore directories. */
		return;

	fdata = file_data_new ();

#ifdef __sun
	fields = split_line (line, 9);
	fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
	fdata->modified = mktime_from_string (fields[5], fields[6], fields[8]);
	g_strfreev (fields);

	name_field = get_last_field (line, 10);
#else
	fields = split_line (line, 8);
	fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
	fdata->modified = mktime_from_string (fields[5], fields[6], fields[7]);
	g_strfreev (fields);

	name_field = get_last_field (line, 9);
#endif /* __sun */

	fields = g_strsplit (name_field, " -> ", 2);

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (name_field, " link to ", 2);
	}

	if (*(fields[0]) == '/') {
		fdata->full_path = g_strdup (fields[0]);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", fields[0], NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	if (fields[1] != NULL)
		fdata->link = g_strdup (fields[1]);
	g_strfreev (fields);

	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else 
		comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
fr_command_rpm_list (FRCommand  *comm,
		     const char *password)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_process_begin_command (comm->process, "rpm2cpio");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_add_arg (comm->process, "| cpio -itv");
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_rpm_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_begin_command (comm->process, "rpm2cpio");
	if (dest_dir != NULL)
                fr_process_set_working_dir (comm->process, dest_dir);
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_add_arg (comm->process, "| cpio -idu");
	for (scan = file_list; scan; scan = scan->next) {
		char *filename = (char*) scan->data;
		fr_process_add_arg (comm->process, filename);
	}
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void 
fr_command_rpm_class_init (FRCommandRpmClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_rpm_finalize;

        afc->list           = fr_command_rpm_list;
	afc->extract        = fr_command_rpm_extract;
}

 
static void 
fr_command_rpm_init (FRCommand *comm)
{
	comm->file_type = FR_FILE_TYPE_RPM;

	comm->propCanModify                = FALSE;
	comm->propAddCanUpdate             = FALSE;
	comm->propAddCanReplace            = FALSE;
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;
}


static void 
fr_command_rpm_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_RPM (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_rpm_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandRpmClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_rpm_class_init,
			NULL,
			NULL,
			sizeof (FRCommandRpm),
			0,
			(GInstanceInitFunc) fr_command_rpm_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandRpm",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_rpm_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	if (!is_program_in_path("rpm2cpio")) {
		return NULL;
	}

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_RPM, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
