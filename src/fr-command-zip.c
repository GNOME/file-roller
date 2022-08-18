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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-zip.h"

#define EMPTY_ARCHIVE_WARNING  "Empty zipfile."
#define ZIP_SPECIAL_CHARACTERS "[]*?!^-\\"


typedef struct
{
	gboolean   is_empty;
} FrCommandZipPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrCommandZip, fr_command_zip, fr_command_get_type ())


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


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrCommandZip        *comm = data;
	FrCommandZipPrivate *priv = fr_command_zip_get_instance_private (comm);
	FrFileData *fdata;
	char               **fields;
	const char          *name_field;
	size_t               line_l;

	g_return_if_fail (line != NULL);

	/* check whether unzip gave the empty archive warning. */

	if (priv->is_empty)
		return;

	line_l = strlen (line);

	if (line_l == 0)
		return;

	if (strcmp (line, EMPTY_ARCHIVE_WARNING) == 0) {
		priv->is_empty = TRUE;
		return;
	}

	/* ignore lines that do not describe a file or a
	 * directory. */
	if ((line[0] != '?') && (line[0] != 'd') && (line[0] != '-'))
		return;

	/**/

	fdata = fr_file_data_new ();

	fields = _g_str_split_line (line, 7);
	fdata->size = g_ascii_strtoull (fields[3], NULL, 10);
	fdata->modified = mktime_from_string (fields[6]);
	fdata->encrypted = (*fields[4] == 'B') || (*fields[4] == 'T');
	g_strfreev (fields);

	/* Full path */

	name_field = _g_str_get_last_field (line, 8);

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
		fdata->name = _g_path_get_dir_name (fdata->full_path);
	else
		fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
	fdata->path = _g_path_remove_level (fdata->full_path);

	if (*fdata->name == 0)
		fr_file_data_free (fdata);
	else
		fr_archive_add_file (FR_ARCHIVE (comm), fdata);
}


static void
add_password_arg (FrCommand  *comm,
		  const char *password)
{
	if ((password != NULL) && (password[0] != '\0')) {
		fr_process_add_arg (comm->process, "-P");
		fr_process_add_arg (comm->process, password);
	}
}


static void
list__begin (gpointer data)
{
	FrCommandZip        *comm = data;
	FrCommandZipPrivate *priv = fr_command_zip_get_instance_private (comm);

	priv->is_empty = FALSE;
}


static gboolean
fr_command_zip_list (FrCommand  *comm)
{
	fr_process_set_out_line_func (comm->process, list__process_line, comm);

	fr_process_begin_command (comm->process, "unzip");
	fr_process_set_begin_func (comm->process, list__begin, comm);
	fr_process_add_arg (comm->process, "-ZTs");
	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
process_line__common (char     *line,
		      gpointer  data)
{
	FrCommand *comm = FR_COMMAND (data);
	FrArchive *archive = FR_ARCHIVE (comm);

	if (line == NULL)
		return;

	if (fr_archive_progress_get_total_files (archive) > 1)
		fr_archive_progress (archive, fr_archive_progress_inc_completed_files (archive, 1));
	else
		fr_archive_message (archive, line);
}


static void
fr_command_zip_add (FrCommand  *comm,
		    const char *from_file,
		    GList      *file_list,
		    const char *base_dir,
		    gboolean    update,
		    gboolean    follow_links)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");

	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);

	if (! follow_links)
		fr_process_add_arg (comm->process, "-y");

	if (update)
		fr_process_add_arg (comm->process, "-u");

	add_password_arg (comm, FR_ARCHIVE (comm)->password);

	switch (FR_ARCHIVE (comm)->compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-3"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-6"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-9"); break;
	}

	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, "--");

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_delete (FrCommand  *comm,
		       const char *from_file,
		       GList      *file_list)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "zip");
	fr_process_add_arg (comm->process, "-d");

	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, "--");

	for (scan = file_list; scan; scan = scan->next) {
		char *escaped;

 		escaped = _g_str_escape (scan->data, ZIP_SPECIAL_CHARACTERS);
 		fr_process_add_arg (comm->process, escaped);
 		g_free (escaped);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_extract (FrCommand  *comm,
			const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__common,
				      comm);

	fr_process_begin_command (comm->process, "unzip");

	if (dest_dir != NULL) {
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, dest_dir);
	}
	if (overwrite)
		fr_process_add_arg (comm->process, "-o");
	else
		fr_process_add_arg (comm->process, "-n");
	if (skip_older)
		fr_process_add_arg (comm->process, "-u");
	if (junk_paths)
		fr_process_add_arg (comm->process, "-j");
	add_password_arg (comm, FR_ARCHIVE (comm)->password);

	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *escaped;

 		escaped = _g_str_escape (scan->data, ZIP_SPECIAL_CHARACTERS);
 		fr_process_add_arg (comm->process, escaped);
 		g_free (escaped);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_zip_test (FrCommand   *comm)
{
	fr_process_begin_command (comm->process, "unzip");
	fr_process_add_arg (comm->process, "-t");
	add_password_arg (comm, FR_ARCHIVE (comm)->password);
	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_zip_handle_error (FrCommand *comm,
			     FrError   *error)
{
	if (error->type == FR_ERROR_NONE)
		return;

	if ((error->status == 82) || (error->status == 5))
		fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
	else {
		int i;

		for (i = 1; i <= 2; i++) {
			GList *output;
			GList *scan;

			output = (i == 1) ? comm->process->err.raw : comm->process->out.raw;

			for (scan = g_list_last (output); scan; scan = scan->prev) {
				char *line = scan->data;

				if (strstr (line, "incorrect password") != NULL) {
					fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
					return;
				}
			}
		}

		/* ignore warnings */

		if (error->status <= 1)
			fr_error_clear_gerror (error);
	}
}


const char *zip_mime_type[] = {
	"application/epub+zip",
	"application/vnd.android.package-archive",
	"application/x-cbz",
	"application/x-chrome-extension",
	"application/x-ear",
	"application/x-ms-dos-executable",
	"application/x-war",
	"application/zip",
	NULL
};


static const char **
fr_command_zip_get_mime_types (FrArchive *archive)
{
	return zip_mime_type;
}


static FrArchiveCaps
fr_command_zip_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("zip", check_command)) {
		if (strcmp (mime_type, "application/x-ms-dos-executable") == 0)
			capabilities |= FR_ARCHIVE_CAN_READ;
		else
			capabilities |= FR_ARCHIVE_CAN_WRITE | FR_ARCHIVE_CAN_ENCRYPT;
	}
	if (_g_program_is_available ("unzip", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_zip_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return FR_PACKAGES ("zip,unzip");
}


static void
fr_command_zip_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_ZIP (object));

	if (G_OBJECT_CLASS (fr_command_zip_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_zip_parent_class)->finalize (object);
}


static void
fr_command_zip_class_init (FrCommandZipClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_zip_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_zip_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_zip_get_mime_types;
	archive_class->get_capabilities = fr_command_zip_get_capabilities;
	archive_class->get_packages     = fr_command_zip_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_zip_list;
	command_class->add              = fr_command_zip_add;
	command_class->delete           = fr_command_zip_delete;
	command_class->extract          = fr_command_zip_extract;
	command_class->test             = fr_command_zip_test;
	command_class->handle_error     = fr_command_zip_handle_error;
}


static void
fr_command_zip_init (FrCommandZip *self)
{
	FrCommandZipPrivate *priv = fr_command_zip_get_instance_private (self);
	FrArchive           *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreFolders       = TRUE;
	base->propAddCanStoreLinks         = TRUE;
	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder      = TRUE;
	base->propExtractCanJunkPaths      = TRUE;
	base->propPassword                 = TRUE;
	base->propTest                     = TRUE;

	priv->is_empty = FALSE;
}
