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

#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-7z.h"

static void fr_command_7z_class_init  (FrCommand7zClass *class);
static void fr_command_7z_init        (FrCommand         *afile);
static void fr_command_7z_finalize    (GObject           *object);

/* Parent Class */

static FrCommandClass *parent_class = NULL;


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

/*
static void
change_to_unix_dir_separator (char *path)
{
	char *c;

	for (c = path; *c != 0; c++)
		if ((*c == '\\') && (*(c+1) != '\\'))
			*c = '/';
}


static char*
to_dos (const char *path)
{
	return str_substitute (path, "/", "\\\\");
}
*/

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
	FrCommand   *comm = FR_COMMAND (data);
	FrCommand7z *p7z_comm = FR_COMMAND_7Z (comm);
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

	if (g_strv_length (fields) < 4) {
		g_strfreev (fields);
		file_data_free (fdata);
		return;
	}

	fdata->size = g_ascii_strtoull (fields[3], NULL, 10);
	fdata->modified = mktime_from_string (fields[0], fields[1]);
	fdata->dir = fields[2][0] == 'D';
	g_strfreev (fields);

	name_field = g_strdup (line + p7z_comm->name_index);
	fdata->free_original_path = TRUE;
	fdata->original_path = g_strdup (name_field);
	fdata->full_path = g_strconcat ((fdata->original_path[0] != '/') ? "/" : "", 
					fdata->original_path,
					(fdata->dir && (fdata->original_path[strlen (fdata->original_path - 1)] != '/')) ? "/" : "",
					NULL);
	g_free (name_field);

	fdata->link = NULL;

	if (fdata->dir)
		fdata->name = dir_name_from_path (fdata->full_path);
	else
		fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);

	fr_command_add_file (comm, fdata);
}


static void
fr_command_7z_begin_command (FrCommand *comm)
{
	if (is_program_in_path ("7za"))
		fr_process_begin_command (comm->process, "7za");
	else if (is_program_in_path ("7zr"))
		fr_process_begin_command (comm->process, "7zr");
	else
		fr_process_begin_command (comm->process, "7z");
}


static void
add_password_arg (FrCommand     *comm,
		  const char    *password,
		  gboolean       always_specify)
{
	if (always_specify || ((password != NULL) && (*password != 0))) {
		char *arg;
		char *e_password;

		e_password = escape_str (password, "\"*?[]'`()$!;");
		if (e_password != NULL) {
			arg = g_strconcat ("-p\"", e_password, "\"", NULL);
			g_free (e_password);
		} 
		else
			arg = g_strdup ("-p\"\"");

		fr_process_add_arg (comm->process, arg);
		g_free (arg);
	}
}


static void
fr_command_7z_list (FrCommand  *comm,
		    const char *password)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      list__process_line,
				      comm);

	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "l");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	add_password_arg (comm, password, FALSE);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_7z_add (FrCommand     *comm,
		   GList         *file_list,
		   const char    *base_dir,
		   gboolean       update,
		   gboolean       recursive,
		   const char    *password,
		   FrCompression  compression)
{
	GList *scan;

	fr_command_7z_begin_command (comm);

	if (base_dir != NULL) {
		fr_process_set_working_dir (comm->process, base_dir);
		fr_process_add_arg_concat (comm->process, "-w", base_dir, NULL);
	}

	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");

	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-l");
	add_password_arg (comm, password, FALSE);

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

	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;
		fr_process_add_arg (comm->process, filename);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_delete (FrCommand *comm,
		      GList     *file_list)
{
	GList *scan;

	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "d");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");

	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_extract (FrCommand  *comm,
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
	add_password_arg (comm, password, FALSE);
	
	if (dest_dir != NULL) 
		fr_process_add_arg_concat (comm->process, "-o", dest_dir, NULL);

	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_7z_test (FrCommand   *comm,
		    const char  *password)
{
	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "t");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	add_password_arg (comm, password, FALSE);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_7z_handle_error (FrCommand   *comm,
			    FrProcError *error)
{
	if (error->type == FR_PROC_ERROR_COMMAND_ERROR) {
		if (error->status <= 1)
			error->type = FR_PROC_ERROR_NONE;
	}
}


static void
fr_command_7z_set_mime_type (FrCommand  *comm,
			     const char *mime_type)
{
	FR_COMMAND_CLASS (parent_class)->set_mime_type (comm, mime_type);
	
	comm->capabilities |= FR_COMMAND_CAP_ARCHIVE_MANY_FILES;
	if (is_program_in_path ("7za")) 
		comm->capabilities |= FR_COMMAND_CAP_READ_WRITE;
	else if (is_program_in_path ("7zr")) 
		comm->capabilities |= FR_COMMAND_CAP_READ_WRITE;
	else if (is_program_in_path ("7z")) 
		comm->capabilities |= FR_COMMAND_CAP_READ_WRITE;
}


static void
fr_command_7z_class_init (FrCommand7zClass *class)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
	FrCommandClass *afc;

	parent_class = g_type_class_peek_parent (class);
	afc = (FrCommandClass*) class;

	gobject_class->finalize = fr_command_7z_finalize;

	afc->list           = fr_command_7z_list;
	afc->add            = fr_command_7z_add;
	afc->delete         = fr_command_7z_delete;
	afc->extract        = fr_command_7z_extract;
	afc->test           = fr_command_7z_test;
	afc->handle_error   = fr_command_7z_handle_error;
	afc->set_mime_type  = fr_command_7z_set_mime_type;
}


static void
fr_command_7z_init (FrCommand *comm)
{
	comm->propAddCanUpdate             = TRUE;
	comm->propAddCanReplace            = TRUE;
	comm->propAddCanStoreFolders       = TRUE;
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = TRUE;
	comm->propPassword                 = TRUE;
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
			sizeof (FrCommand7zClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_7z_class_init,
			NULL,
			NULL,
			sizeof (FrCommand7z),
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
