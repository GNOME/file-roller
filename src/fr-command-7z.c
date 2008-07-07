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


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrCommand    *comm = FR_COMMAND (data);
	FrCommand7z  *p7z_comm = FR_COMMAND_7Z (comm);
	char        **fields;
	FileData     *fdata;

	g_return_if_fail (line != NULL);

	if (! p7z_comm->list_started) {
		if (strncmp (line, "--------", 8) == 0)
			p7z_comm->list_started = TRUE;
		return;
	}

	if (strcmp (line, "") == 0) {
		if (p7z_comm->fdata != NULL) {
			fdata = p7z_comm->fdata;
			if (fdata->dir)
				fdata->name = dir_name_from_path (fdata->full_path);
			else
				fdata->name = g_strdup (file_name_from_path (fdata->full_path));
			fdata->path = remove_level_from_path (fdata->full_path);
			fr_command_add_file (comm, fdata);
			p7z_comm->fdata = NULL;
		}
		return;
	}

	if (p7z_comm->fdata == NULL)
		p7z_comm->fdata = file_data_new ();

	fields = g_strsplit (line, " = ", 2);

	if (g_strv_length (fields) < 2) {
		g_strfreev (fields);
		return;
	}

	fdata = p7z_comm->fdata;

	if (strcmp (fields[0], "Path") == 0) {
		fdata->free_original_path = TRUE;
		fdata->original_path = g_strdup (fields[1]);
		fdata->full_path = g_strconcat ((fdata->original_path[0] != '/') ? "/" : "",
						fdata->original_path,
						(fdata->dir && (fdata->original_path[strlen (fdata->original_path - 1)] != '/')) ? "/" : "",
						NULL);
	}
	else if (strcmp (fields[0], "Folder") == 0) {
		fdata->dir = (strcmp (fields[1], "+") == 0);
	}
	else if (strcmp (fields[0], "Size") == 0) {
		fdata->size = g_ascii_strtoull (fields[1], NULL, 10);
	}
	else if (strcmp (fields[0], "Modified") == 0) {
		char **modified_fields;

		modified_fields = g_strsplit (fields[1], " ", 2);
		fdata->modified = mktime_from_string (modified_fields[0], modified_fields[1]);
		g_strfreev (modified_fields);
	}
	else if (strcmp (fields[0], "Encrypted") == 0) {
		if (strcmp (fields[1], "+") == 0)
			fdata->encrypted = TRUE;
	}
	else if (strcmp (fields[0], "Method") == 0) {
		if (strstr (fields[1], "AES") != NULL)
			fdata->encrypted = TRUE;
	}
	else if (strcmp (fields[0], "Attributes") == 0) {
	}
	g_strfreev (fields);
}


static void
fr_command_7z_begin_command (FrCommand *comm)
{
	if (is_program_in_path ("7z"))
		fr_process_begin_command (comm->process, "7z");
	else if (is_program_in_path ("7za"))
		fr_process_begin_command (comm->process, "7za");
	else if (is_program_in_path ("7zr"))
		fr_process_begin_command (comm->process, "7zr");
}


static void
add_password_arg (FrCommand     *comm,
		  const char    *password,
		  gboolean       always_specify)
{
	if (always_specify || ((password != NULL) && (*password != 0))) {
		char *arg;

		arg = g_strconcat ("-p", password, NULL);
		fr_process_add_arg (comm->process, arg);
		g_free (arg);
	}
}


static void
fr_command_7z_list (FrCommand  *comm,
		    const char *password)
{
	FrCommand7z *p7z_comm = FR_COMMAND_7Z (comm);

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      list__process_line,
				      comm);

	fr_command_7z_begin_command (comm);
	fr_process_add_arg (comm->process, "l");
	fr_process_add_arg (comm->process, "-slt");
	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	add_password_arg (comm, password, FALSE);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	if (p7z_comm->fdata != NULL) {
		file_data_free (p7z_comm->fdata);
		p7z_comm->fdata = NULL;
	}
	p7z_comm->list_started = FALSE;
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

	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");

	if (base_dir != NULL) {
		fr_process_set_working_dir (comm->process, base_dir);
		fr_process_add_arg_concat (comm->process, "-w", base_dir, NULL);
	}

	if (is_mime_type (comm->mime_type, "application/zip"))
		fr_process_add_arg (comm->process, "-tzip");

	fr_process_add_arg (comm->process, "-bd");
	fr_process_add_arg (comm->process, "-y");
	fr_process_add_arg (comm->process, "-l");
	add_password_arg (comm, password, FALSE);
	if ((password != NULL) && (*password != 0) && comm->encrypt_header)
		fr_process_add_arg (comm->process, "-mhe=on");

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

	if (is_mime_type (comm->mime_type, "application/x-executable"))
		fr_process_add_arg (comm->process, "-sfx");

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

	if (is_mime_type (comm->mime_type, "application/x-executable"))
		fr_process_add_arg (comm->process, "-sfx");

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
	if (error->type != FR_PROC_ERROR_COMMAND_ERROR)
		return;

	if (error->status <= 1) {
		error->type = FR_PROC_ERROR_NONE;
	}
	else {
		GList *scan;

		for (scan = g_list_last (comm->process->out.raw); scan; scan = scan->prev) {
			char *line = scan->data;

			if ((strstr (line, "Wrong password?") != NULL)
			    || (strstr (line, "Enter password") != NULL))
			{
				error->type = FR_PROC_ERROR_ASK_PASSWORD;
				break;
			}
		}
	}
}


const char *sevenz_mime_types[] = { "application/x-7z-compressed",
				    "application/x-arj",
				    "application/x-cabinet",
				    "application/x-cd-image",
				    /*"application/x-cbr",*/
				    "application/x-cbz",
				    "application/x-executable",
				    "application/x-rar",
				    "application/zip",
				    NULL };


const char **
fr_command_7z_get_mime_types (FrCommand *comm)
{
	return sevenz_mime_types;
}


FrCommandCap
fr_command_7z_get_capabilities (FrCommand  *comm,
			        const char *mime_type)
{
	FrCommandCap capabilities;

	capabilities = FR_COMMAND_CAP_ARCHIVE_MANY_FILES;
	if (! is_program_in_path ("7za") && ! is_program_in_path ("7zr") && ! is_program_in_path ("7z"))
		return capabilities;

	if (is_mime_type (mime_type, "application/x-7z-compressed"))
		capabilities |= FR_COMMAND_CAP_READ_WRITE | FR_COMMAND_CAP_ENCRYPT | FR_COMMAND_CAP_ENCRYPT_HEADER;

	else if (is_program_in_path ("7z")) {
		capabilities |= FR_COMMAND_CAP_READ;
		if (is_mime_type (mime_type, "application/x-cbz")
		    || is_mime_type (mime_type, "application/x-executable")
		    || is_mime_type (mime_type, "application/zip"))
		{
			capabilities |= FR_COMMAND_CAP_WRITE | FR_COMMAND_CAP_ENCRYPT;
		}
	}

	return capabilities;
}


static void
fr_command_7z_class_init (FrCommand7zClass *class)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
	FrCommandClass *afc;

	parent_class = g_type_class_peek_parent (class);
	afc = (FrCommandClass*) class;

	gobject_class->finalize = fr_command_7z_finalize;

	afc->list             = fr_command_7z_list;
	afc->add              = fr_command_7z_add;
	afc->delete           = fr_command_7z_delete;
	afc->extract          = fr_command_7z_extract;
	afc->test             = fr_command_7z_test;
	afc->handle_error     = fr_command_7z_handle_error;
	afc->get_mime_types   = fr_command_7z_get_mime_types;
	afc->get_capabilities = fr_command_7z_get_capabilities;
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
