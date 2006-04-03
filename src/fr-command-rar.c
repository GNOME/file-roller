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
#include "fr-command-rar.h"

static void fr_command_rar_class_init  (FRCommandRarClass *class);
static void fr_command_rar_init        (FRCommand         *afile);
static void fr_command_rar_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


static gboolean
have_rar (void)
{
	return is_program_in_path ("rar");
}


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
		tm.tm_mday = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_mon = atoi (fields[1]) - 1;
			if (fields[2] != NULL)
				tm.tm_year = 100 + atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 2);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]) - 1;
		if (fields[1] != NULL) 
			tm.tm_min = atoi (fields[1]);
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static void
process_line (char     *line, 
	      gpointer  data)
{
	FRCommand     *comm = FR_COMMAND (data);
	FRCommandRar  *rar_comm = FR_COMMAND_RAR (comm);
	char         **fields;
	const char    *name_field;
	gboolean       encrypted;      /* unused */

	g_return_if_fail (line != NULL);

	if (! rar_comm->list_started) {
		if (strncmp (line, "--------", 8) == 0) {
			rar_comm->list_started = TRUE;
			rar_comm->odd_line = TRUE;
		}
		return;
	}

	if (strncmp (line, "--------", 8) == 0) {
		rar_comm->list_started = FALSE;
		return;
	}

	if (rar_comm->odd_line) {
		FileData *fdata;

		rar_comm->fdata = fdata = file_data_new ();

		/* read file name. */
	  
		encrypted = (line[0] == '*') ? TRUE : FALSE;
		
		name_field = line + 1;

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

	} else {
		FileData *fdata;
		
		fdata = rar_comm->fdata;

		/* read file info. */
		
		fields = split_line (line, 6);

		if ((fdata->name == NULL)
		    || (*fdata->name == '\0') 
		    || (fields[5][1] == 'D') 
		    || (fields[5][0] == 'd')) {
			file_data_free (fdata);
		} else {
			fdata->size = g_ascii_strtoull (fields[0], NULL, 10);
			fdata->modified = mktime_from_string (fields[3], fields[4]); 
			comm->file_list = g_list_prepend (comm->file_list, fdata);
		}
		g_strfreev (fields);

		rar_comm->fdata = NULL;
	}

	rar_comm->odd_line = ! rar_comm->odd_line;
}


static void
add_password_arg (FRCommand	*comm,
		  const char	*password,
		  gboolean	disable_query)
{
	if (password != NULL && password[0] != '\0') {
		char *arg, *e_password;
		
		e_password = escape_str (password, "\"*?[]'`()$!;");
		g_assert (e_password != NULL);  /* since password is non-NULL */
		
		arg = g_strconcat ("-p\"", e_password, "\"", NULL);
		
		fr_process_add_arg (comm->process, arg);
		
		g_free (e_password);
		g_free (arg);
	} else if (disable_query) {
		fr_process_add_arg (comm->process, "-p-");
	}
}


static void
fr_command_rar_list (FRCommand *comm)
{
	FR_COMMAND_RAR (comm)->list_started = FALSE;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line,
				      comm);

	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");
	fr_process_add_arg (comm->process, "v");
	fr_process_add_arg (comm->process, "-c-");

	/* Fixme: listing rar archives fails when headers and data
	          are encrypted (-hp option) without a password. */
	add_password_arg (comm, NULL, TRUE);
	
	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--"); 

	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_rar_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList *scan;

	fr_process_begin_command (comm->process, "rar");

	if (base_dir != NULL) 
		fr_process_set_working_dir (comm->process, base_dir);

	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");

	switch (compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-m1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-m2"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-m3"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-m5"); break;
	}

	add_password_arg (comm, password, FALSE);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-idp");
	
	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--"); 

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, (gchar*) scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_rar_delete (FRCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "rar");
	fr_process_add_arg (comm->process, "d");

	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--"); 

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_rar_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");

	fr_process_add_arg (comm->process, "x");

	if (overwrite)
		fr_process_add_arg (comm->process, "-o+");
	else
		fr_process_add_arg (comm->process, "-o-");	

	if (skip_older)
		fr_process_add_arg (comm->process, "-u");

	if (junk_paths)
		fr_process_add_arg (comm->process, "-ep");
	
	add_password_arg (comm, password, TRUE);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-idp");
	
	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--"); 

	fr_process_add_arg (comm->process, comm->e_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	if (dest_dir != NULL) {
		char *e_dest_dir = fr_command_escape (comm, dest_dir);
		fr_process_add_arg (comm->process, e_dest_dir);
		g_free (e_dest_dir);
	}
	
	fr_process_end_command (comm->process);
}


static void
fr_command_rar_test (FRCommand   *comm,
		     const char  *password)
{
	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");
	
	fr_process_add_arg (comm->process, "t");
	
	add_password_arg (comm, password, TRUE);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-idp");

	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--");
	
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_rar_handle_error (FRCommand   *comm, 
			     FRProcError *error)
{
	if (error->type == FR_PROC_ERROR_GENERIC) {
		if (error->status <= 1)
			error->type = FR_PROC_ERROR_NONE;
		else if (error->status == 3)
			error->type = FR_PROC_ERROR_ASK_PASSWORD;
	}
}


static void 
fr_command_rar_class_init (FRCommandRarClass *class)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_rar_finalize;

        afc->list         = fr_command_rar_list;
	afc->add          = fr_command_rar_add;
	afc->delete       = fr_command_rar_delete;
	afc->extract      = fr_command_rar_extract;
	afc->test         = fr_command_rar_test;
	afc->handle_error = fr_command_rar_handle_error;
}

 
static void 
fr_command_rar_init (FRCommand *comm)
{
	comm->file_type = FR_FILE_TYPE_RAR;

	comm->propAddCanUpdate             = TRUE; 
	comm->propAddCanReplace            = TRUE; 
	comm->propExtractCanAvoidOverwrite = TRUE;
	comm->propExtractCanSkipOlder      = TRUE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = TRUE;
	comm->propTest                     = TRUE;
}


static void 
fr_command_rar_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_RAR (object));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_rar_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandRarClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_rar_class_init,
			NULL,
			NULL,
			sizeof (FRCommandRar),
			0,
			(GInstanceInitFunc) fr_command_rar_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandRar",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_rar_new (FRProcess  *process,
		    const char *filename)
{
	FRCommand *comm;

	if ((!is_program_in_path("rar")) && (!is_program_in_path("unrar"))) 
		return NULL;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_RAR, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
