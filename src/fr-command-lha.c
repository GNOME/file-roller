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
#include "fr-command-lha.h"


struct _FrCommandLha
{
	FrCommand  parent_instance;
};


G_DEFINE_TYPE (FrCommandLha, fr_command_lha, fr_command_get_type ())


/* -- list -- */

static time_t
mktime_from_string (char *month,
		    char *mday,
		    char *time_or_year)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm     tm = {0, };
	char        **fields;

	tm.tm_isdst = -1;

	/* date */

	if (month != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month) == 0) {
				tm.tm_mon = i;
				break;
			}
	}
	tm.tm_mday = atoi (mday);
	if (strchr (time_or_year, ':') == NULL)
		tm.tm_year = atoi (time_or_year) - 1900;
	else {
		time_t     now;
		struct tm *tm_now;

		now = time (NULL);
		tm_now = localtime (&now);
		if (tm_now != NULL)
			tm.tm_year = tm_now->tm_year;

		/* time */

		fields = g_strsplit (time_or_year, ":", 2);
		if (fields[0] != NULL) {
			tm.tm_hour = atoi (fields[0]);
			if (fields[1] != NULL)
				tm.tm_min = atoi (fields[1]);
		}
		g_strfreev (fields);
	}

	return mktime (&tm);
}


static char **
split_line_lha (char *line)
{
	char       **fields;
	int          n_fields = 8;
	const char  *scan, *field_end;
	int          i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields] = NULL;

	i = 0;

	/* First column either contains Unix permissions or OS type, depending
	 * on what generated the archive. The OS type is enclosed in [brackets]
	 * and may include a space. */
	scan = _g_str_eat_spaces (line);
	if (scan[0] == '[') {
		field_end = strchr (scan, ']');
		if (field_end != NULL) {
			++field_end;
		}
	} else {
		field_end = NULL;
	}

	if (field_end == NULL) {
		field_end = strchr (scan, ' ');
		if (field_end == NULL) {
			field_end = scan + strlen(scan);
		}
	}

	fields[i++] = g_strndup (scan, field_end - scan);
	scan = field_end;

	/* Second column contains Unix UID/GID, but if the archive was not
	 * made on a Unix system it will be empty. Insert a dummy value so
	 * we get a consistent result. */
	if (g_str_has_prefix (scan, "           ")) {
		fields[i++] = g_strdup("");
	}

	scan = _g_str_eat_spaces (scan);
	for (; i < n_fields; i++) {
		field_end = strchr (scan, ' ');
		if (field_end == NULL) {
			field_end = scan + strlen(scan);
		}

		fields[i] = g_strndup (scan, field_end - scan);
		scan = _g_str_eat_spaces (field_end);
	}

	return fields;
}


static void
process_line (char     *line,
	      gpointer  data)
{
	FrFileData *fdata;
	FrCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;

	g_return_if_fail (line != NULL);

	fdata = fr_file_data_new ();

	fields = split_line_lha (line);
	fdata->size = g_ascii_strtoull (fields[2], NULL, 10);
	fdata->modified = mktime_from_string (fields[4],
					      fields[5],
					      fields[6]);

	/* Full path */

	name_field = fields[7];

	if (name_field && *name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	g_strfreev (fields);

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


static gboolean
fr_command_lha_list (FrCommand  *comm)
{
	fr_process_set_out_line_func (comm->process, process_line, comm);

	fr_process_begin_command (comm->process, "lha");
	fr_process_add_arg (comm->process, "lq");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_lha_add (FrCommand  *comm,
		    const char *from_file,
		    GList      *file_list,
		    const char *base_dir,
		    gboolean    update,
		    gboolean    follow_links)
{
	GList *scan;

	fr_process_begin_command (comm->process, "lha");
	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);
	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");
	fr_process_add_arg (comm->process, comm->filename);
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_lha_delete (FrCommand  *comm,
		       const char *from_file,
		       GList      *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "lha");
	fr_process_add_arg (comm->process, "d");
	fr_process_add_arg (comm->process, comm->filename);
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_lha_extract (FrCommand  *comm,
			const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;
	char   options[5];
	int    i = 0;

	fr_process_begin_command (comm->process, "lha");

	if (dest_dir != NULL)
		fr_process_set_working_dir (comm->process, dest_dir);

	options[i++] = 'x';
	options[i++] = 'f'; /* Always overwrite.
			     * The overwrite option is handled in
			     * fr_archive_extract,
			     * this is because lha asks the user whether he
			     * wants to overwrite a file. */

	if (junk_paths)
		options[i++] = 'i';

	options[i++] = 0;
	fr_process_add_arg (comm->process, options);
	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


const char *lha_mime_type[] = { "application/x-lha", NULL };


static const char **
fr_command_lha_get_mime_types (FrArchive *archive)
{
	return lha_mime_type;
}


static FrArchiveCaps
fr_command_lha_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("lha", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE;

	return capabilities;
}


static const char *
fr_command_lha_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return FR_PACKAGES ("lha");
}


static void
fr_command_lha_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_LHA (object));

        if (G_OBJECT_CLASS (fr_command_lha_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_lha_parent_class)->finalize (object);
}


static void
fr_command_lha_class_init (FrCommandLhaClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_lha_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_lha_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_lha_get_mime_types;
	archive_class->get_capabilities = fr_command_lha_get_capabilities;
	archive_class->get_packages     = fr_command_lha_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_lha_list;
	command_class->add              = fr_command_lha_add;
	command_class->delete           = fr_command_lha_delete;
	command_class->extract          = fr_command_lha_extract;
}


static void
fr_command_lha_init (FrCommandLha *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreFolders       = TRUE;
	base->propAddCanStoreLinks         = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = TRUE;
	base->propCanDeleteAllFiles        = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
}
