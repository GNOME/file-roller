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
#include <unistd.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-alz.h"
#include "glib-utils.h"


struct _FrCommandAlz
{
	FrCommand  parent_instance;

	gboolean   extract_none;
	gboolean   invalid_password;
	gboolean   list_started;
};


G_DEFINE_TYPE (FrCommandAlz, fr_command_alz, fr_command_get_type ())


/* -- list -- */


static time_t
mktime_from_string (char *date_s,
		    char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

	/* date */

	fields = g_strsplit (date_s, "/", 3);
	if (fields[0] != NULL) {
		tm.tm_mon = atoi (fields[0]) - 1;
		if (fields[1] != NULL) {
			tm.tm_mday = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_year = 100 + atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 3);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
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
	FrCommand     *comm = FR_COMMAND (data);
	FrCommandAlz  *alz_comm = FR_COMMAND_ALZ (comm);
	FrFileData *fdata;
	char         **fields;
	char          *name_field;
	char           name_last;
	gsize	       name_len;

	g_return_if_fail (line != NULL);


	if (! alz_comm->list_started) {
		if (strncmp (line, "-----", 5 ) == 0 )
			alz_comm->list_started = TRUE;
		return;
	}

	if (strncmp (line, "-----", 5 ) == 0) {
		alz_comm->list_started = FALSE;
		return;

	}

	if (! alz_comm->list_started)
		return;

	fdata = fr_file_data_new ();
	fields = _g_str_split_line (line, 5);
	fdata->modified = mktime_from_string (fields[0], fields[1]);
	fdata->size = g_ascii_strtoull (fields[3], NULL, 10);

	name_field = g_strdup (_g_str_get_last_field (line, 6));
	name_len = strlen (name_field);

	name_last = name_field[name_len - 1];
	fdata->dir = name_last == '\\';
	fdata->encrypted = name_last == '*';

	if (fdata->dir || fdata->encrypted)
		name_field[--name_len] = '\0';

	if (*name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	}
	else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	if (fdata->dir) {
		char *s;
		for (s = fdata->full_path; *s != '\0'; ++s)
			if (*s == '\\') *s = '/';
		for (s = fdata->original_path; *s != '\0'; ++s)
			if (*s == '\\') *s = '/';
		fdata->name = _g_path_get_dir_name (fdata->full_path);
	}
	else {
		fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
	}

	fdata->path = _g_path_remove_level (fdata->full_path);

	if (*fdata->name == 0)
		fr_file_data_free (fdata);
	else
		fr_archive_add_file (FR_ARCHIVE (comm), fdata);

	g_free (name_field);
	g_strfreev (fields);
}


static void
add_codepage_arg (FrCommand *comm)
{
	const char  *env_list[] = { "LC_CTYPE", "LC_ALL", "LANG", NULL };
	const char **scan;
	const char  *arg = "-cp949";

	for (scan = env_list; *scan != NULL; ++scan) {
		char *env = getenv (*scan);

		if (! env)
			continue;

		if (strstr (env, "UTF-8") ||  strstr (env, "utf-8"))
			arg = "-utf8";
		else if (strstr (env, "euc") || strstr (env, "EUC"))
			arg = "-euc-kr";
		else
			continue;
		break;
	}

	fr_process_add_arg (comm->process, arg);
}


static void
add_password_arg (FrCommand  *comm,
		  const char *password,
		  gboolean    disable_query)
{
	if (password != NULL) {
		fr_process_add_arg (comm->process, "-pwd");
		fr_process_add_arg (comm->process, password);
	}
	else if (disable_query) {
		fr_process_add_arg (comm->process, "-pwd");
		fr_process_add_arg (comm->process, "");
	}
}


static void
list__begin (gpointer data)
{
	FrCommandAlz *comm = data;

	comm->list_started = FALSE;
	comm->invalid_password = FALSE;
}


static gboolean
fr_command_alz_list (FrCommand  *comm)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, process_line, comm);

	fr_process_begin_command (comm->process, "unalz");
	fr_process_set_begin_func (comm->process, list__begin, comm);
	fr_process_add_arg (comm->process, "-l");
	add_codepage_arg(comm);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
	fr_process_use_standard_locale (comm->process, TRUE);

	return TRUE;
}


/* -- extract -- */

static void
process_extract_line (char     *line,
		      gpointer  data)
{
	FrCommand    *comm = FR_COMMAND (data);
	FrCommandAlz *alz_comm = FR_COMMAND_ALZ (comm);

	g_return_if_fail (line != NULL);

	/* - error check - */

	if (strncmp (line, "err code(28) (invalid password)", 31) == 0) {
		alz_comm->invalid_password = TRUE;
		fr_process_cancel (comm->process);
		return;
	}

	if (alz_comm->extract_none && (strncmp (line, "unalziiiing :", 13) == 0)) {
		alz_comm->extract_none = FALSE;
	}
	else if ((strncmp (line, "done..", 6) == 0) && alz_comm->extract_none) {
		fr_process_cancel (comm->process);
		return;
	}
}


static void
fr_command_alz_extract (FrCommand  *comm,
		        const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;

	FR_COMMAND_ALZ (comm)->extract_none = TRUE;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_extract_line,
				      comm);

	fr_process_begin_command (comm->process, "unalz");
	if (dest_dir != NULL) {
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, dest_dir);
	}
	add_codepage_arg (comm);
	add_password_arg (comm, FR_ARCHIVE (comm)->password, TRUE);
	fr_process_add_arg (comm->process, comm->filename);
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_alz_handle_error (FrCommand *comm,
			     FrError   *error)
{
	if ((error->type == FR_ERROR_STOPPED)) {
		if  (FR_COMMAND_ALZ (comm)->extract_none
		     || FR_COMMAND_ALZ (comm)->invalid_password)
		{
			fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
		}
	}
}


const char *alz_mime_type[] = { "application/x-alz", NULL };


static const char **
fr_command_alz_get_mime_types (FrArchive *archive)
{
	return alz_mime_type;
}


static FrArchiveCaps
fr_command_alz_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("unalz", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_alz_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return FR_PACKAGES ("unalz");
}


static void
fr_command_alz_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_ALZ (object));

        if (G_OBJECT_CLASS (fr_command_alz_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_alz_parent_class)->finalize (object);
}


static void
fr_command_alz_class_init (FrCommandAlzClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_alz_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_alz_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_alz_get_mime_types;
	archive_class->get_capabilities = fr_command_alz_get_capabilities;
	archive_class->get_packages     = fr_command_alz_get_packages;

	command_class = (FrCommandClass*) klass;
        command_class->list             = fr_command_alz_list;
	command_class->add              = NULL;
	command_class->delete           = NULL;
	command_class->extract          = fr_command_alz_extract;
	command_class->handle_error     = fr_command_alz_handle_error;
}


static void
fr_command_alz_init (FrCommandAlz *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = TRUE;
	base->propTest                     = FALSE;
}
