/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2006 The Free Software Foundation, Inc.
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
#include "fr-command-cpio.h"


struct _FrCommandCpio
{
	FrCommand  parent_instance;
	gboolean   is_empty;
};


G_DEFINE_TYPE (FrCommandCpio, fr_command_cpio, fr_command_get_type ())


/* -- list -- */

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
	if (strchr (year, ':') != NULL) {
		char **fields = g_strsplit (year, ":", 2);
        	if (g_strv_length (fields) == 2) {
	        	time_t      now;
        		struct tm  *now_tm;

	  		tm.tm_hour = atoi (fields[0]);
	  		tm.tm_min = atoi (fields[1]);

	  		now = time(NULL);
	  		now_tm = localtime (&now);
	  		tm.tm_year = now_tm->tm_year;
        	}
	} else
		tm.tm_year = atoi (year) - 1900;

	return mktime (&tm);
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrFileData *fdata;
	FrCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;
	char        *name;
	int          ofs = 0;

	g_return_if_fail (line != NULL);

	fdata = fr_file_data_new ();

#ifdef __sun
	fields = _g_str_split_line (line, 9);
	fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
	fdata->modified = mktime_from_string (fields[5], fields[6], fields[8]);
	g_strfreev (fields);

	name_field = _g_str_get_last_field (line, 10);
#else /* !__sun */
	/* Handle char and block device files */
	if ((line[0] == 'c') || (line[0] == 'b')) {
		fields = _g_str_split_line (line, 9);
		ofs = 1;
		fdata->size = 0;
		/* FIXME: We should also specify the content type */
	}
	else {
		fields = _g_str_split_line (line, 8);
		fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
	}
	fdata->modified = mktime_from_string (fields[5+ofs], fields[6+ofs], fields[7+ofs]);
	g_strfreev (fields);

	name_field = _g_str_get_last_field (line, 9+ofs);
#endif /* !__sun */

	fields = g_strsplit (name_field, " -> ", 2);

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (name_field, " link to ", 2);
	}

	fdata->dir = line[0] == 'd';

	name = g_strcompress (fields[0]);
	if (*(fields[0]) == '/') {
		fdata->full_path = g_strdup (name);
		fdata->original_path = fdata->full_path;
	}
	else {
		fdata->full_path = g_strconcat ("/", name, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	if (fdata->dir && (name[strlen (name) - 1] != '/')) {
		char *old_full_path = fdata->full_path;
		fdata->full_path = g_strconcat (old_full_path, "/", NULL);
		g_free (old_full_path);
		fdata->original_path = g_strdup (name);
		fdata->free_original_path = TRUE;
	}
	g_free (name);

	if (fields[1] != NULL)
		fdata->link = g_strcompress (fields[1]);
	g_strfreev (fields);

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
fr_command_cpio_list (FrCommand  *comm)
{
	fr_process_set_out_line_func (comm->process, list__process_line, comm);

	fr_process_begin_command (comm->process, "sh");
	fr_process_add_arg (comm->process, "-c");
	fr_process_add_arg_concat (comm->process, CPIO_PATH " -itv < ", comm->e_filename, NULL);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_cpio_extract (FrCommand *comm,
			const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList   *scan;
	GString *cmd;

	fr_process_begin_command (comm->process, "sh");
	if (dest_dir != NULL)
                fr_process_set_working_dir (comm->process, dest_dir);
	fr_process_add_arg (comm->process, "-c");

	cmd = g_string_new (CPIO_PATH " -idu --no-absolute-filenames ");
	for (scan = file_list; scan; scan = scan->next) {
		char *filepath = scan->data;
		char *filename;

		if (filepath[0] == '/')
			filename = g_shell_quote (filepath + 1);
		else
			filename = g_shell_quote (filepath);
		g_string_append (cmd, filename);
		g_string_append (cmd, " ");

		g_free (filename);
	}
        g_string_append (cmd, " < ");
	g_string_append (cmd, comm->e_filename);
	fr_process_add_arg (comm->process, cmd->str);
	g_string_free (cmd, TRUE);

	fr_process_end_command (comm->process);
}


const char *cpio_mime_type[] = { "application/x-cpio", NULL };


static const char **
fr_command_cpio_get_mime_types (FrArchive *archive)
{
	return cpio_mime_type;
}


static FrArchiveCaps
fr_command_cpio_get_capabilities (FrArchive  *archive,
			          const char *mime_type,
				  gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available (CPIO_PATH, check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	return capabilities;
}


static const char *
fr_command_cpio_get_packages (FrArchive  *archive,
			      const char *mime_type)
{
	return FR_PACKAGES ("cpio");
}


static void
fr_command_cpio_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_CPIO (object));

        if (G_OBJECT_CLASS (fr_command_cpio_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_cpio_parent_class)->finalize (object);
}


static void
fr_command_cpio_class_init (FrCommandCpioClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_cpio_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_cpio_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_cpio_get_mime_types;
	archive_class->get_capabilities = fr_command_cpio_get_capabilities;
	archive_class->get_packages     = fr_command_cpio_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_cpio_list;
	command_class->extract          = fr_command_cpio_extract;
}


static void
fr_command_cpio_init (FrCommandCpio *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = FALSE;
	base->propAddCanReplace            = FALSE;
	base->propAddCanStoreFolders       = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
}
