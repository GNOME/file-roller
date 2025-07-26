/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 The Free Software Foundation, Inc.
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
#define _XOPEN_SOURCE       /* See feature_test_macros(7) */
#define _XOPEN_SOURCE_EXTENDED 1  /* for strptime */
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-unarchiver.h"
#include "fr-error.h"

#define LSAR_SUPPORTED_FORMAT 2
#define LSAR_DATE_FORMAT "%Y-%m-%d %H:%M:%S %z"
#define UNARCHIVER_SPECIAL_CHARACTERS "["


struct _FrCommandUnarchiver
{
	FrCommand     parent_instance;
	GInputStream *stream;
	int           n_line;
};


G_DEFINE_TYPE (FrCommandUnarchiver, fr_command_unarchiver, fr_command_get_type ())


/* -- list -- */


static void
process_line (char     *line,
	      gpointer  data)
{
	FrCommandUnarchiver *unar_comm = FR_COMMAND_UNARCHIVER (data);
	g_memory_input_stream_add_data (G_MEMORY_INPUT_STREAM (unar_comm->stream), line, -1, NULL);
}


static time_t
mktime_from_string (const char *time_s)
{
	struct tm tm = {0, };
	tm.tm_isdst = -1;
	strptime (time_s, LSAR_DATE_FORMAT, &tm);
	return mktime (&tm);
}

static void
list_command_completed (gpointer data)
{
	FrCommandUnarchiver *unar_comm = FR_COMMAND_UNARCHIVER (data);
	JsonParser          *parser;
	GError              *error = NULL;

	parser = json_parser_new ();
	if (json_parser_load_from_stream (parser, unar_comm->stream, NULL, &error)) {
		JsonObject *root;

		root = json_node_get_object (json_parser_get_root (parser));

		if (json_object_get_int_member (root, "lsarFormatVersion") == LSAR_SUPPORTED_FORMAT) {
			JsonArray *content;

			content = json_object_get_array_member (root, "lsarContents");
			for (guint i = 0; i < json_array_get_length (content); i++) {
				JsonObject *entry;
				FrFileData *fdata;
				const char *filename;

				entry = json_array_get_object_element (content, i);
				fdata = fr_file_data_new ();
				if (json_object_has_member (entry, "XADFileSize"))
					fdata->size = json_object_get_int_member (entry, "XADFileSize");
				fdata->modified = mktime_from_string (json_object_get_string_member (entry, "XADLastModificationDate"));
				if (json_object_has_member (entry, "XADIsEncrypted"))
					fdata->encrypted = json_object_get_int_member (entry, "XADIsEncrypted") == 1;

				filename = json_object_get_string_member (entry, "XADFileName");
				if (*filename == '/') {
					fdata->full_path = g_strdup (filename);
					fdata->original_path = fdata->full_path;
				}
				else {
					fdata->full_path = g_strconcat ("/", filename, NULL);
					fdata->original_path = fdata->full_path + 1;
				}

				fdata->link = NULL;
				if (json_object_has_member (entry, "XADIsDirectory"))
					fdata->dir = json_object_get_int_member (entry, "XADIsDirectory") == 1;
				if (fdata->dir)
					fdata->name = _g_path_get_dir_name (fdata->full_path);
				else
					fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
				fdata->path = _g_path_remove_level (fdata->full_path);

				fr_archive_add_file (FR_ARCHIVE (unar_comm), fdata);
			}
		}
	}

	g_object_unref (parser);
}


static gboolean
fr_command_unarchiver_list (FrCommand  *comm)
{
	FrCommandUnarchiver *unar_comm = FR_COMMAND_UNARCHIVER (comm);

	_g_object_unref (unar_comm->stream);
	unar_comm->stream = g_memory_input_stream_new ();

	fr_process_set_out_line_func (comm->process, process_line, comm);

	fr_process_begin_command (comm->process, "lsar");
	fr_process_set_end_func (comm->process, list_command_completed, comm);
	fr_process_add_arg (comm->process, "-j");
	if ((FR_ARCHIVE (comm)->password != NULL) && (FR_ARCHIVE (comm)->password[0] != '\0'))
		fr_process_add_arg_concat (comm->process, "-password=", FR_ARCHIVE (comm)->password, NULL);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
process_line__extract (char     *line,
		       gpointer  data)
{
	FrCommand           *comm = FR_COMMAND (data);
	FrArchive           *archive = FR_ARCHIVE (comm);
	FrCommandUnarchiver *unar_comm = FR_COMMAND_UNARCHIVER (comm);

	if (line == NULL)
		return;

	unar_comm->n_line++;

	/* the first line is the name of the archive */
	if (unar_comm->n_line == 1)
		return;

	if (fr_archive_progress_get_total_files (archive) > 1)
		fr_archive_progress (archive, fr_archive_progress_inc_completed_files (archive, 1));
	else
		fr_archive_message (archive, line);
}


static void
fr_command_unarchiver_extract (FrCommand  *comm,
			       const char *from_file,
			       GList      *file_list,
			       const char *dest_dir,
			       gboolean    overwrite,
			       gboolean    skip_older,
			       gboolean    junk_paths)
{
	FrCommandUnarchiver *unar_comm = FR_COMMAND_UNARCHIVER (comm);
	GList               *scan;

	unar_comm->n_line = 0;

	fr_process_use_standard_locale (comm->process, TRUE);
	fr_process_set_out_line_func (comm->process,
				      process_line__extract,
				      comm);

	fr_process_begin_command (comm->process, "unar");

	if (overwrite)
		fr_process_add_arg (comm->process, "-f");
	else
		fr_process_add_arg (comm->process, "-s");

	fr_process_add_arg (comm->process, "-D");

	if ((FR_ARCHIVE (comm)->password != NULL) && (FR_ARCHIVE (comm)->password[0] != '\0'))
		fr_process_add_arg_concat (comm->process, "-password=", FR_ARCHIVE (comm)->password, NULL);

	if (dest_dir != NULL)
		fr_process_add_arg_concat (comm->process, "-output-directory=", dest_dir, NULL);

	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next) {
		char *escaped;

		escaped = _g_str_escape (scan->data, UNARCHIVER_SPECIAL_CHARACTERS);
		fr_process_add_arg (comm->process, escaped);
		g_free (escaped);
	}

	fr_process_end_command (comm->process);
}


static void
fr_command_unarchiver_handle_error (FrCommand   *comm,
				    FrError *error)
{
	GList *scan;

#if 0
	{
		for (scan = g_list_last (comm->process->err.raw); scan; scan = scan->prev)
			g_print ("%s\n", (char*)scan->data);
	}
#endif

	if (error->type == FR_ERROR_NONE)
		return;

	for (scan = g_list_last (comm->process->err.raw); scan; scan = scan->prev) {
		char *line = scan->data;

		if (strstr (line, "password") != NULL) {
			fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
			break;
		}
	}
}


const char *unarchiver_mime_type[] = { "application/x-cbr",
				       "application/x-rar",
				       "application/vnd.rar",
				       "application/x-stuffit",
				       NULL };


static const char **
fr_command_unarchiver_get_mime_types (FrArchive *archive)
{
	return unarchiver_mime_type;
}


static FrArchiveCaps
fr_command_unarchiver_get_capabilities (FrArchive  *archive,
					const char *mime_type,
					gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_DO_NOTHING;
	if (_g_program_is_available ("lsar", check_command) && _g_program_is_available ("unar", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_unarchiver_get_packages (FrArchive  *archive,
				    const char *mime_type)
{
	return FR_PACKAGES ("unarchiver");
}


static void
fr_command_unarchiver_finalize (GObject *object)
{
	FrCommandUnarchiver *self;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_UNARCHIVER (object));

	self = FR_COMMAND_UNARCHIVER (object);
	_g_object_unref (self->stream);

	if (G_OBJECT_CLASS (fr_command_unarchiver_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_unarchiver_parent_class)->finalize (object);
}


static void
fr_command_unarchiver_class_init (FrCommandUnarchiverClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_unarchiver_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_unarchiver_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_unarchiver_get_mime_types;
	archive_class->get_capabilities = fr_command_unarchiver_get_capabilities;
	archive_class->get_packages     = fr_command_unarchiver_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_unarchiver_list;
	command_class->extract          = fr_command_unarchiver_extract;
	command_class->handle_error     = fr_command_unarchiver_handle_error;
}


static void
fr_command_unarchiver_init (FrCommandUnarchiver *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = TRUE;
	base->propTest                     = FALSE;
	base->propListFromFile             = FALSE;

	self->stream = NULL;
}
