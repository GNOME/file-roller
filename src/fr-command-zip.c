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

#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-zip.h"

#define EMPTY_ARCHIVE_WARNING        "Empty zipfile."

static void fr_command_zip_class_init  (FrCommandZipClass *class);
static void fr_command_zip_init        (FrCommand         *afile);
static void fr_command_zip_finalize    (GObject           *object);

/* Parent Class */

static FrCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *datetime_s)
{
	struct tm  tm = {0, };
	char      *date;
	char      *time;
	char      *year;
	char      *month;
	char      *day;
	char      *hour;
	char      *min;
	char      *sec;

	tm.tm_isdst = -1;

	/* date */

	date = datetime_s;
	year = g_strndup (date, 4);
	month = g_strndup (date + 4, 2);
	day = g_strndup (date + 6, 2);
	tm.tm_year = atoi (year) - 1900;
	tm.tm_mon = atoi (month) - 1;
	tm.tm_mday = atoi (day);
	g_free (year);
	g_free (month);
	g_free (day);

	/* time */

	time = datetime_s + 9;
	hour = g_strndup (time, 2);
	min = g_strndup (time + 2, 2);
	sec = g_strndup (time + 4, 2);
	tm.tm_hour = atoi (hour);
	tm.tm_min = atoi (min);
	tm.tm_sec = atoi (sec);
	g_free(hour);
	g_free(min);
	g_free(sec);

	return mktime (&tm);
}


static char *
fr_command_zip_escape (FrCommand     *comm,
		       const char    *str)
{
	char *estr;
	char *estr2;

	estr = escape_str (str, "\\");
	estr2 = shell_escape (estr);

	g_free (estr);

	return estr2;

}


static char*
zip_escape (const char *str)
{
	return escape_str (str, "*?[]");
}


static char*
prepend_path_separator (const char *str)
{
	if (*str == '-' || g_str_has_prefix (str, "\\-"))
		return g_strconcat (".", G_DIR_SEPARATOR_S, str, NULL);
	else
		return g_strdup (str);
}


static char*
prepend_path_separator_zip_escape (const char *str)
{
	char *tmp1, *tmp2;

	tmp2 = prepend_path_separator (str);
	tmp1 = zip_escape (tmp2);
	g_free (tmp2);

	return tmp1;
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FileData    *fdata;
	FrCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;
	gint         line_l;

	g_return_if_fail (line != NULL);

	/* check whether unzip gave the empty archive warning. */

	if (FR_COMMAND_ZIP (comm)->is_empty)
		return;

	line_l = strlen (line);

	if (line_l == 0)
		return;

	if (strcmp (line, EMPTY_ARCHIVE_WARNING) == 0) {
		FR_COMMAND_ZIP (comm)->is_empty = TRUE;
		return;
	}

	/* ignore lines that do not describe a file or a
	 * directory. */
	if ((line[0] != '?') && (line[0] != 'd') && (line[0] != '-'))
		return;

	/**/

	fdata = file_data_new ();

	fields = split_line (line, 7);
	fdata->size = g_ascii_strtoull (fields[3], NULL, 10);
	fdata->modified = mktime_from_string (fields[6]);
	fdata->encrypted = (*fields[4] == 'B') || (*fields[4] == 'T');
	g_strfreev (fields);

	/* Full path */

	name_field = get_last_field (line, 8);

	if (*name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	fdata->link = NULL;

	fdata->dir = line[0] == 'd';
	if (fdata->dir)
		fdata->name = dir_name_from_path (fdata->full_path);
	else
		fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else
		fr_command_add_file (comm, fdata);
}


static void
add_filename_arg (FrCommand *comm)
{
	char *temp = prepend_path_separator (comm->e_filename);
	fr_process_add_arg (comm->process, temp);
	g_free (temp);
}


static void
add_password_arg (FrCommand     *comm,
		  const char    *password,
		  gboolean       always_specify)
{
	if (always_specify || ((password != NULL) && (password[0] != '\0'))) {
		char *arg;
		char *quoted_arg;
		
		arg = g_strdup_printf ("-P %s", password);
		quoted_arg = g_shell_quote (arg);

		fr_process_add_arg (comm->process, quoted_arg);

		g_free (quoted_arg);
		g_free (arg);
	}
}


static void
fr_command_zip_list (FrCommand  *comm,
		     const char *password)
{
	FR_COMMAND_ZIP (comm)->is_empty = FALSE;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      list__process_line,
				      comm);

	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-ZTs");
	add_filename_arg (comm);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
process_line__common (char     *line,
		      gpointer  data)
{
	FrCommand  *comm = FR_COMMAND (data);

	if (line == NULL)
		return;

	fr_command_message (comm, line);

	if (comm->n_files != 0) {
		double fraction = (double) ++comm->n_file / (comm->n_files + 1);
		fr_command_progress (comm, fraction);
	}
}


static void
fr_command_zip_add (FrCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");

	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);

	/* preserve links. */
	fr_process_add_arg (comm->process, "-y");

	if (update)
		fr_process_add_arg (comm->process, "-u");

	add_password_arg (comm, password, FALSE);

	switch (compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-3"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-6"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-9"); break;
	}

	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_delete (FrCommand *comm,
		       GList     *file_list)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");
	fr_process_add_arg (comm->process, "-d");
	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator_zip_escape ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_extract (FrCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "unzip");

	if (dest_dir != NULL) {
		char *e_dest_dir = fr_command_escape (comm, dest_dir);
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

	add_password_arg (comm, password, TRUE);

	add_filename_arg (comm);

	for (scan = file_list; scan; scan = scan->next) {
		char *temp = prepend_path_separator_zip_escape ((char*) scan->data);
		fr_process_add_arg (comm->process, temp);
		g_free (temp);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_test (FrCommand   *comm,
		     const char  *password)
{
	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-t");
	add_password_arg (comm, password, TRUE);
	add_filename_arg (comm);
	fr_process_end_command (comm->process);
}


static void
fr_command_zip_handle_error (FrCommand   *comm,
			     FRProcError *error)
{
	if (error->type == FR_PROC_ERROR_COMMAND_ERROR) {
		if (error->status <= 1)
			error->type = FR_PROC_ERROR_NONE;
		else if ((error->status == 82) || (error->status == 5))
			error->type = FR_PROC_ERROR_ASK_PASSWORD;
		else {
			GList *output;
			GList *scan;
			
			if (comm->action == FR_ACTION_TESTING_ARCHIVE)
				output = comm->process->raw_output;
			else
				output = comm->process->raw_error;
			
			for (scan = g_list_last (output); scan; scan = scan->prev) {
				char *line = scan->data;
				
				if (strstr (line, "incorrect password") != NULL) {
					error->type = FR_PROC_ERROR_ASK_PASSWORD;
					break;
				}
			}
		}
	}
}


static void
fr_command_zip_class_init (FrCommandZipClass *class)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
	FrCommandClass *afc;

	parent_class = g_type_class_peek_parent (class);
	afc = (FrCommandClass*) class;

	gobject_class->finalize = fr_command_zip_finalize;

	afc->list           = fr_command_zip_list;
	afc->add            = fr_command_zip_add;
	afc->delete         = fr_command_zip_delete;
	afc->extract        = fr_command_zip_extract;
	afc->test           = fr_command_zip_test;
	afc->handle_error   = fr_command_zip_handle_error;

	afc->escape         = fr_command_zip_escape;
}


static void
fr_command_zip_init (FrCommand *comm)
{
	comm->file_type = FR_FILE_TYPE_ZIP;

	comm->propAddCanUpdate             = TRUE;
	comm->propAddCanReplace            = TRUE;
	comm->propAddCanStoreFolders       = TRUE;
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
	static GType type = 0;

	if (! type) {
		GTypeInfo type_info = {
			sizeof (FrCommandZipClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_zip_class_init,
			NULL,
			NULL,
			sizeof (FrCommandZip),
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


FrCommand *
fr_command_zip_new (FrProcess  *process,
		    const char *filename)
{
	FrCommand *comm;

	if (!is_program_in_path ("zip") &&
	    !is_program_in_path ("unzip")) {
		return NULL;
	}

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_ZIP, NULL));
	fr_command_construct (comm, process, filename);

	return comm;
}
