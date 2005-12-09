/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
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
#include "fr-command-7z.h"

static void fr_command_7z_class_init  (FRCommand7zClass *class);
static void fr_command_7z_init        (FRCommand         *afile);
static void fr_command_7z_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *date_s, 
		    char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

	tm.tm_isdst = -1;

	/* date */

	fields = g_strsplit (date_s, "-", 3);
	if (fields[0] != NULL) {
		tm.tm_year = atoi (fields[0]) - 1900;
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


static void
change_to_unix_dir_separator (char *path)
{
	char *c;
	
	for (c = path; *c != 0; c++)
		if (*c == '\\')
			*c = '/';
}


static int
str_rfind (const char *str, 
	   int         c)
{
	char *tmp = strrchr (str, c);
	return (tmp == NULL) ? -1 : (tmp - str);
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FRCommand   *comm = FR_COMMAND (data);
	FRCommand7z *p7z_comm = FR_COMMAND_7Z (comm);
	FileData    *fdata;
	char        *name_field;
	char       **fields;

	g_return_if_fail (line != NULL);

	if (! p7z_comm->list_started) {
		if (strncmp (line, "--------", 8) == 0) {
			p7z_comm->name_index = str_rfind (line, ' ') + 1;
			p7z_comm->list_started = TRUE;
		}
		return;
	}

	if (strncmp (line, "--------", 8) == 0) {
		p7z_comm->list_started = FALSE;
		return;
	}

	fdata = file_data_new ();

	fields = split_line (line, 4);

	if (fields[2][0] == 'D') { /* skip directories */
		g_strfreev (fields);
		file_data_free (fdata);
		return;
	}

	fdata->size = g_ascii_strtoull (fields[3], NULL, 10);
	fdata->modified = mktime_from_string (fields[0], fields[1]); 
	g_strfreev (fields);

	name_field = g_strdup (line + p7z_comm->name_index);

	change_to_unix_dir_separator (name_field);
	
	if (*name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}
	g_free (name_field);

	fdata->link = NULL;
		
	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);

	comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
fr_command_7z_begin_command (FRCommand *comm)
{
	if (is_program_in_path ("7za"))
		fr_process_begin_command (comm->process, "7za");
	else
		fr_process_begin_command (comm->process, "7z");
}


static void
fr_command_7z_list (FRCommand *comm)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      list__process_line,
				      comm);

	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "l");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_7z_add (FRCommand     *comm,
		   GList         *file_list,
		   const char    *base_dir,
		   gboolean       update,
		   const char    *password,
		   FRCompression  compression)
{
	GList *scan;

	fr_command_7z_begin_command (comm);

	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);

	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");

	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");

	switch (compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-mx=1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-mx=5"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-mx=5"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-mx=7"); break;
	}

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = str_substitute ((char*) scan->data, "/", "\\\\");
		fr_process_add_arg (comm->process, filename);
		g_free (filename);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_delete (FRCommand *comm,
		      GList     *file_list)
{
	GList *scan;

	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "d");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = str_substitute ((char*) scan->data, "/", "\\\\");
		fr_process_add_arg (comm->process, filename);
		g_free (filename);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_extract (FRCommand  *comm,
		       GList      *file_list,
		       const char *dest_dir,
		       gboolean    overwrite,
		       gboolean    skip_older,
		       gboolean    junk_paths,
		       const char *password)
{
	GList *scan;

	fr_command_7z_begin_command (comm);

	if (junk_paths)
		fr_process_add_arg (comm->process, "e");
	else
		fr_process_add_arg (comm->process, "x");

	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	
	if (dest_dir != NULL) {
		char *e_dest_dir = fr_command_escape (comm, dest_dir);
		char *opt = g_strconcat ("-o", e_dest_dir, NULL);
		fr_process_add_arg (comm->process, opt);
		g_free (opt);
		g_free (e_dest_dir);
	}

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = str_substitute ((char*) scan->data, "/", "\\\\");
		fr_process_add_arg (comm->process, filename);
		g_free (filename);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_test (FRCommand   *comm,
		    const char  *password)
{
	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "t");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
}


static void 
fr_command_7z_class_init (FRCommand7zClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_7z_finalize;

        afc->list           = fr_command_7z_list;
	afc->add            = fr_command_7z_add;
	afc->delete         = fr_command_7z_delete;
	afc->extract        = fr_command_7z_extract;
	afc->test           = fr_command_7z_test;
}

 
static void 
fr_command_7z_init (FRCommand *comm)
{
	comm->file_type = FR_FILE_TYPE_7ZIP;

	comm->propAddCanUpdate             = TRUE; 
	comm->propAddCanReplace            = TRUE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = TRUE;
}


static void 
fr_command_7z_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_7Z (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_7z_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommand7zClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_7z_class_init,
			NULL,
			NULL,
			sizeof (FRCommand7z),
			0,
			(GInstanceInitFunc) fr_command_7z_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommand7z",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_7z_new (FRProcess  *process,
		   const char *filename)
{
	FRCommand *comm;

	if ((!is_program_in_path("7za")) &&
	    (!is_program_in_path("7z"))) {
		return NULL;
	}

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_7Z, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
