/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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
#include "fr-command.h"
#include "fr-command-ar.h"
#include "glib-utils.h"


struct _FrCommandAr
{
	FrCommand  parent_instance;
};


G_DEFINE_TYPE (FrCommandAr, fr_command_ar, fr_command_get_type ())


/* -- list -- */

static time_t
mktime_from_string (char *time_s,
		    char *day_s,
		    char *month_s,
		    char *year_s)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm    tm = {0, };
	char       **fields;

	tm.tm_isdst = -1;

	/* date */

	if (month_s != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month_s) == 0) {
				tm.tm_mon = i;
				break;
			}
	}
	tm.tm_mday = atoi (day_s);
	tm.tm_year = atoi (year_s) - 1900;

	/* time */

	fields = g_strsplit (time_s, ":", 3);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_min  = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec  = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static char*
ar_get_last_field (const char *line,
		   int         start_from,
		   int         field_n)
{
	const char *f_start, *f_end;

	line = line + start_from;

	f_start = line;
	while ((*f_start == ' ') && (*f_start != *line))
		f_start++;
	f_end = f_start;

	while ((field_n > 0) && (*f_end != 0)) {
		if (*f_end == ' ') {
			field_n--;
			if (field_n != 0) {
				while ((*f_end == ' ') && (*f_end != *line))
					f_end++;
				f_start = f_end;
			}
		} else
			f_end++;
	}

	return g_strdup (f_start);
}


static void
process_line (char     *line,
	      gpointer  data)
{
	FrFileData *fdata;
	FrCommand   *comm = FR_COMMAND (data);
	char       **fields;
	int          date_idx;
	char        *field_month, *field_day, *field_time, *field_year;
	char        *field_size, *field_name;

	g_return_if_fail (line != NULL);

	fdata = fr_file_data_new ();

	date_idx = _g_line_get_index_from_pattern (line, "%c%c%c %a%n %n%n:%n%n %n%n%n%n");

	field_size = _g_line_get_prev_field (line, date_idx, 1);
	fdata->size = g_ascii_strtoull (field_size, NULL, 10);
	g_free (field_size);

	field_month = _g_line_get_next_field (line, date_idx, 1);
	field_day = _g_line_get_next_field (line, date_idx, 2);
	field_time = _g_line_get_next_field (line, date_idx, 3);
	field_year = _g_line_get_next_field (line, date_idx, 4);
	fdata->modified = mktime_from_string (field_time, field_day, field_month, field_year);
	g_free (field_day);
	g_free (field_month);
	g_free (field_year);
	g_free (field_time);

	/* Full path */

	field_name = ar_get_last_field (line, date_idx, 5);

	fields = g_strsplit (field_name, " -> ", 2);

	if (fields[0] == NULL) {
		g_strfreev (fields);
		g_free (field_name);
		fr_file_data_free (fdata);
		return;
	}

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (field_name, " link to ", 2);
	}

	if (*(fields[0]) == '/') {
		fdata->full_path = g_strdup (fields[0]);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", fields[0], NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	if (fields[1] != NULL)
		fdata->link = g_strdup (fields[1]);
	g_strfreev (fields);
	g_free (field_name);

	fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
	fdata->path = _g_path_remove_level (fdata->full_path);

	if (*fdata->name == 0)
		fr_file_data_free (fdata);
	else
		fr_archive_add_file (FR_ARCHIVE (comm), fdata);
}


static gboolean
fr_command_ar_list (FrCommand *comm)
{
	fr_process_set_out_line_func (comm->process, process_line, comm);

	fr_process_begin_command (comm->process, "ar");
	fr_process_add_arg (comm->process, "tv");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_ar_add (FrCommand  *comm,
		   const char *from_file,
		   GList      *file_list,
		   const char *base_dir,
		   gboolean    update,
		   gboolean    follow_links)
{
	GList *scan;

	fr_process_begin_command (comm->process, "ar");

	if (update)
		fr_process_add_arg (comm->process, "ru");
	else
		fr_process_add_arg (comm->process, "r");

	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);

	fr_process_add_arg (comm->process, comm->filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_ar_delete (FrCommand  *comm,
		      const char *from_file,
		      GList      *file_list)
{
	GList *scan;

	fr_process_begin_command (comm->process, "ar");
	fr_process_add_arg (comm->process, "d");
	fr_process_add_arg (comm->process, comm->filename);
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_ar_extract (FrCommand  *comm,
		       const char *from_file,
		       GList      *file_list,
		       const char *dest_dir,
		       gboolean    overwrite,
		       gboolean    skip_older,
		       gboolean    junk_paths)
{
	GList *scan;

	fr_process_begin_command (comm->process, "ar");

	if (dest_dir != NULL)
		fr_process_set_working_dir (comm->process, dest_dir);

	fr_process_add_arg (comm->process, "x");
	fr_process_add_arg (comm->process, comm->filename);
	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
fr_command_ar_handle_error (FrCommand *comm,
			    FrError   *error)
{
	/* FIXME */
}


const char *ar_mime_type[] = { "application/vnd.debian.binary-package",
			       "application/x-ar",
			       "application/x-deb",
			       NULL };


static const char **
fr_command_ar_get_mime_types (FrArchive *archive)
{
	return ar_mime_type;
}


static FrArchiveCaps
fr_command_ar_get_capabilities (FrArchive  *archive,
			        const char *mime_type,
				gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("ar", check_command)) {
		if (_g_mime_type_matches (mime_type, "application/x-deb")
		    || _g_mime_type_matches (mime_type, "application/vnd.debian.binary-package"))
		{
			capabilities |= FR_ARCHIVE_CAN_READ;
		}
		else if (_g_mime_type_matches (mime_type, "application/x-ar"))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}

	return capabilities;
}


static const char *
fr_command_ar_get_packages (FrArchive  *archive,
			    const char *mime_type)
{
	return FR_PACKAGES ("binutils");
}


static void
fr_command_ar_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_AR (object));

        if (G_OBJECT_CLASS (fr_command_ar_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_ar_parent_class)->finalize (object);
}


static void
fr_command_ar_class_init (FrCommandArClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_ar_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_ar_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_ar_get_mime_types;
	archive_class->get_capabilities = fr_command_ar_get_capabilities;
	archive_class->get_packages     = fr_command_ar_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_ar_list;
	command_class->add              = fr_command_ar_add;
	command_class->delete           = fr_command_ar_delete;
	command_class->extract          = fr_command_ar_extract;
	command_class->handle_error     = fr_command_ar_handle_error;
}


static void
fr_command_ar_init (FrCommandAr *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreFolders       = FALSE;
	base->propAddCanStoreLinks         = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
}
