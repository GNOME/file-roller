/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 The Free Software Foundation, Inc.
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
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-iso.h"

static void fr_command_iso_class_init  (FRCommandIsoClass *class);
static void fr_command_iso_init           (FRCommand         *afile);
static void fr_command_iso_finalize     (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


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
	tm.tm_year = atoi (year) - 1900;

	return mktime (&tm);
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FileData      *fdata;
	FRCommand     *comm = FR_COMMAND (data);
	FRCommandIso  *comm_iso = FR_COMMAND_ISO (comm);
	char         **fields;
	const char    *name_field;
	
	g_return_if_fail (line != NULL);

	if (line[0] == 'd') /* Ignore directories. */
		return;

	if (line[0] == 'D') {
		if (comm_iso->cur_path != NULL)
			g_free (comm_iso->cur_path);
		comm_iso->cur_path = g_strdup (get_last_field(line, 4));
		
	} else if (line[0] == '-') {
		/* Is file */
		fdata = file_data_new ();
		
		fields = split_line (line, 8);
		fdata->size = atol (fields[4]);
		fdata->modified = mktime_from_string (fields[5], fields[6], fields[7]);
		g_strfreev (fields);
		
		/* Full path */
		name_field = get_last_field (line, 12);
		fdata->original_path = fdata->full_path = g_strstrip (g_strconcat (comm_iso->cur_path, name_field, NULL));
		fdata->name = g_strdup (file_name_from_path (fdata->full_path));
		fdata->path = remove_level_from_path (fdata->full_path);
		fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);
		comm->file_list = g_list_prepend (comm->file_list, fdata);
	}
}


static void
fr_command_iso_list (FRCommand *comm)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_process_begin_command (comm->process, "isoinfo");
	fr_process_add_arg (comm->process, "-R -J -l -i");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_iso_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char       *scanpath = (char*)scan->data;
		const char *filename;
                char       *fullpath, *temppath = NULL;
	
		filename = file_name_from_path (scanpath);
		temppath = remove_level_from_path (scanpath+1); /* Add one to ignore initial '/' */
		if (temppath == NULL)
			temppath = g_strdup ("/");
		if (dest_dir != NULL) {
			fullpath = g_build_filename (dest_dir, temppath, NULL);
			g_free (temppath);
		} else 
			fullpath = temppath;

		ensure_dir_exists (fullpath, 0700);

		fr_process_begin_command (comm->process, "isoinfo");
		fr_process_set_working_dir (comm->process, fullpath);
		fr_process_add_arg (comm->process, "-R -J -i");
		fr_process_add_arg (comm->process, comm->e_filename);
		fr_process_add_arg (comm->process, "-x");
		fr_process_add_arg (comm->process, scanpath);
		fr_process_add_arg (comm->process, ">");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		fr_process_start (comm->process);
		
		g_free (fullpath);
	}
}


static void 
fr_command_iso_class_init (FRCommandIsoClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_iso_finalize;

        afc->list           = fr_command_iso_list;
	afc->extract        = fr_command_iso_extract;
}


static void 
fr_command_iso_init (FRCommand *comm)
{
	FRCommandIso *comm_iso = FR_COMMAND_ISO (comm);

	comm_iso->cur_path = NULL;
	
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
fr_command_iso_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_ISO (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_iso_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandIsoClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_iso_class_init,
			NULL,
			NULL,
			sizeof (FRCommandIso),
			0,
			(GInstanceInitFunc) fr_command_iso_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandIso",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_iso_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_ISO, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
