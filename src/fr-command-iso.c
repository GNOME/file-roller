/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 The Free Software Foundation, Inc.
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
#include "fr-command-iso.h"


struct _FrCommandIso
{
	FrCommand  parent_instance;
	char      *cur_path;
	gboolean   joliet;
};


G_DEFINE_TYPE (FrCommandIso, fr_command_iso, fr_command_get_type ())


static time_t
mktime_from_string (char *month,
		    char *mday,
		    char *year)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm     tm = {0, };

	tm.tm_isdst = -1;

	if (month != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month) == 0) {
				tm.tm_mon = i;
				break;
			}
	}
	tm.tm_mday = atoi (mday);
	tm.tm_year = atoi (year) - 1900;

	return mktime (&tm);
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrFileData *fdata;
	FrCommand     *comm = FR_COMMAND (data);
	FrCommandIso  *comm_iso = FR_COMMAND_ISO (comm);
	char         **fields;
	const char    *name_field;

	g_return_if_fail (line != NULL);

	if (line[0] == 'd') /* Ignore directories. */
		return;

	if (line[0] == 'D') {
		g_free (comm_iso->cur_path);
		comm_iso->cur_path = g_strdup (_g_str_get_last_field (line, 4));

	} else if (line[0] == '-') { /* Is file */
		const char *last_field, *first_bracket;

		fdata = fr_file_data_new ();

		fields = _g_str_split_line (line, 8);
		fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
		fdata->modified = mktime_from_string (fields[5], fields[6], fields[7]);
		g_strfreev (fields);

		/* Full path */

		last_field = _g_str_get_last_field (line, 9);
		first_bracket = strchr (last_field, ']');
		if (first_bracket == NULL) {
			fr_file_data_free (fdata);
			return;
		}

		name_field = _g_str_eat_spaces (first_bracket + 1);
		if ((name_field == NULL)
		    || (strcmp (name_field, ".") == 0)
		    || (strcmp (name_field, "..") == 0)) {
			fr_file_data_free (fdata);
			return;
		}

		if (comm_iso->cur_path[0] != '/')
			fdata->full_path = g_strstrip (g_strconcat ("/", comm_iso->cur_path, name_field, NULL));
		else
			fdata->full_path = g_strstrip (g_strconcat (comm_iso->cur_path, name_field, NULL));
		fdata->original_path = fdata->full_path;
		fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
		fdata->path = _g_path_remove_level (fdata->full_path);

		fr_archive_add_file (FR_ARCHIVE (comm), fdata);
	}
}


static void
list__begin (gpointer data)
{
	FrCommandIso *comm = data;

	g_free (comm->cur_path);
	comm->cur_path = NULL;
}


static gboolean
fr_command_iso_list (FrCommand *comm)
{
	fr_process_set_out_line_func (comm->process, list__process_line, comm);

	fr_process_begin_command (comm->process, "sh");
	fr_process_set_begin_func (comm->process, list__begin, comm);
	fr_process_add_arg (comm->process, SHDIR "isoinfo.sh");
	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, "-l");
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_iso_extract (FrCommand  *comm,
			const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		char       *path = scan->data;
		const char *filename;
		char       *file_dir;
		char       *temp_dest_dir = NULL;
		GFile      *directory;

		filename = _g_path_get_basename (path);
		file_dir = _g_path_remove_level (path);
		if ((file_dir != NULL) && (strcmp (file_dir, "/") != 0))
			temp_dest_dir = g_build_filename (dest_dir, file_dir, NULL);
		 else
			temp_dest_dir = g_strdup (dest_dir);
		g_free (file_dir);

		if (temp_dest_dir == NULL)
			continue;

		directory = g_file_new_for_path (temp_dest_dir);
		_g_file_make_directory_tree (directory, 0700, NULL);

		fr_process_begin_command (comm->process, "sh");
		fr_process_set_working_dir (comm->process, temp_dest_dir);
		fr_process_add_arg (comm->process, SHDIR "isoinfo.sh");
		fr_process_add_arg (comm->process, "-i");
		fr_process_add_arg (comm->process, comm->filename);
		fr_process_add_arg (comm->process, "-x");
		fr_process_add_arg (comm->process, path);
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);

		g_object_unref (directory);
		g_free (temp_dest_dir);
	}
}


const char *iso_mime_type[] = { "application/x-cd-image", NULL };


static const char **
fr_command_iso_get_mime_types (FrArchive *archive)
{
	return iso_mime_type;
}


static FrArchiveCaps
fr_command_iso_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("isoinfo", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_iso_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return FR_PACKAGES ("genisoimage");
}


static void
fr_command_iso_finalize (GObject *object)
{
	FrCommandIso *self;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_ISO (object));

	self = FR_COMMAND_ISO (object);
	g_free (self->cur_path);

	if (G_OBJECT_CLASS (fr_command_iso_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_iso_parent_class)->finalize (object);
}


static void
fr_command_iso_class_init (FrCommandIsoClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_iso_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_iso_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_iso_get_mime_types;
	archive_class->get_capabilities = fr_command_iso_get_capabilities;
	archive_class->get_packages     = fr_command_iso_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_iso_list;
	command_class->extract          = fr_command_iso_extract;
}


static void
fr_command_iso_init (FrCommandIso *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = FALSE;
	base->propAddCanReplace            = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
	base->propCanExtractAll            = FALSE;

	self->cur_path = NULL;
	self->joliet = TRUE;
}
