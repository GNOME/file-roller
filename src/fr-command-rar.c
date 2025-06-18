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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-rar.h"
#include "fr-error.h"
#include "rar-utils.h"


struct _FrCommandRar
{
	FrCommand  parent_instance;

	gboolean   list_started;
	gboolean   rar4_odd_line;
	gboolean   rar5;
	gboolean   rar5_30;
	int        name_column_start;
	FrFileData *fdata;
};


G_DEFINE_TYPE (FrCommandRar, fr_command_rar, fr_command_get_type ())


static gboolean
have_rar (void)
{
	return _g_program_is_in_path ("rar");
}


/* -- list -- */


static time_t
mktime_from_string (const char *date_s,
		    const char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

	tm.tm_isdst = -1;

	/* date */

	fields = g_strsplit (date_s, "-", 3);
	if (fields[0] != NULL) {
		tm.tm_mday = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_mon = atoi (fields[1]) - 1;
			if (fields[2] != NULL)
				tm.tm_year = 100 + atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 2);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL)
			tm.tm_min = atoi (fields[1]);
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static time_t
mktime_from_string_rar_5_30 (const char *date_s,
			     const char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

	tm.tm_isdst = -1;

	/* date */

	fields = g_strsplit (date_s, "-", 3);
	if (fields[0] != NULL) {
		tm.tm_year = atoi (fields[0]) - 1900;
		if (fields[1] != NULL) {
			tm.tm_mon = atoi (fields[1]) - 1;
			if (fields[2] != NULL)
				tm.tm_mday = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 2);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL)
			tm.tm_min = atoi (fields[1]);
	}
	g_strfreev (fields);

	return mktime (&tm);
}


/*
 * Sample rar 5.30 or higher output:
 *

RAR 5.30   Copyright (c) 1993-2017 Alexander Roshal   11 Aug 2017
Trial version             Type 'rar -?' for help

Archive: test.rar
Details: RAR 5

 Attributes      Size    Packed Ratio    Date    Time   Checksum  Name
----------- ---------  -------- ----- ---------- -----  --------  ----
 -rw-r--r--        51        47  92%  2017-11-19 16:20  80179DAB  loremipsum.txt
----------- ---------  -------- ----- ---------- -----  --------  ----
                   51        47  92%                              1

 */

/* Sample rar-5 listing output:

RAR 5.00 beta 8   Copyright (c) 1993-2013 Alexander Roshal   22 Aug 2013
Trial version             Type RAR -? for help

Archive: test.rar
Details: RAR 4

 Attributes      Size    Packed Ratio   Date   Time   Checksum  Name
----------- ---------  -------- ----- -------- -----  --------  ----
 -rw-r--r--       453       304  67%  05-09-13 09:55  56DA5EF3  loremipsum.txt
----------- ---------  -------- ----- -------- -----  --------  ----
                  453       304  67%                            1

 *
 * Sample rar-4 listing output:
 *

RAR 4.20   Copyright (c) 1993-2012 Alexander Roshal   9 Jun 2012
Trial version             Type RAR -? for help

Archive test.rar

Pathname/Comment
                  Size   Packed Ratio  Date   Time     Attr      CRC   Meth Ver
-------------------------------------------------------------------------------
 loremipsum.txt
                   453      304  67% 05-09-13 09:55 -rw-r--r-- 56DA5EF3 m3b 2.9
-------------------------------------------------------------------------------
    1              453      304  67%

 */

static gboolean
attribute_field_with_space (char *line)
{
	/* sometimes when the archive is encrypted the attributes field is
	 * like this: "*   ..A...."
	 * */
	return ((line[0] != ' ') && (line[1] == ' '));
}


static gboolean
parse_name_field (char         *line,
		  int           line_len,
		  FrCommandRar *rar_comm)
{
	FrFileData *fdata;
	char       *name_field;

	fr_file_data_free (rar_comm->fdata);
	rar_comm->fdata = fdata = fr_file_data_new ();

	/* read file name. */

	fdata->encrypted = (line[0] == '*') ? TRUE : FALSE;

	name_field = NULL;
	if (rar_comm->rar5) {
		const char *field = NULL;
		if ((rar_comm->name_column_start > 0) && (rar_comm->name_column_start < line_len)) {
			field = line + rar_comm->name_column_start;
			// If the file size has 10 digits or more the name is
			// shifted to the right, in this case the checksum could
			// end after name_column_start.
			if ((*field != ' ') && (*(field - 1) != ' ')) {
				// Skip the checksum.
				field = strchr (field, ' ');
			}
		}
		if (field == NULL) {
			int field_idx = attribute_field_with_space (line) ? 9 : 8;
			const char *field = _g_str_get_last_field (line, field_idx);
			if (field == NULL) {
				// Sometimes the checksum column is empty (seen for directories).
				field_idx--;
				field = _g_str_get_last_field (line, field_idx);
			}
		}
		if (field != NULL) {
			// rar-5 output adds trailing spaces to short file names :(
			name_field = g_strstrip (g_strdup (field));
		}
	}
	else {
		name_field = g_strdup (line + 1);
	}

	if (name_field == NULL)
		return FALSE;

	if (*name_field == '/') {
		fdata->full_path = g_strdup (name_field);
		fdata->original_path = fdata->full_path;
	}
	else {
		fdata->full_path = g_strconcat ("/", name_field, NULL);
		fdata->original_path = fdata->full_path + 1;
	}

	fdata->link = NULL;
	fdata->path = _g_path_remove_level (fdata->full_path);

	g_free (name_field);

	return TRUE;
}

static gboolean
attr_field_is_dir (const char   *attr_field,
                   FrCommandRar *rar_comm)
{
        if ((attr_field[0] == 'd') ||
            (rar_comm->rar5 && attr_field[3] == 'D') ||
            (!rar_comm->rar5 && attr_field[1] == 'D'))
                return TRUE;

        return FALSE;
}

static void
process_line (char     *line,
	      gpointer  data)
{
	FrCommand     *comm = FR_COMMAND (data);
	FrCommandRar  *rar_comm = FR_COMMAND_RAR (comm);
	char         **fields;
	int            line_len;

	g_return_if_fail (line != NULL);
	line_len = strlen (line);

	if (! rar_comm->list_started) {
		if ((strncmp (line, "RAR ", 4) == 0) || (strncmp (line, "UNRAR ", 6) == 0)) {
			int major_version;
			int minor_version;

			if (strncmp (line, "RAR ", 4) == 0)
				sscanf (line, "RAR %d.%d", &major_version, &minor_version);
			else
				sscanf (line, "UNRAR %d.%d", &major_version, &minor_version);

			rar_comm->rar5 = (major_version >= 5);
			rar_comm->rar5_30 = ((major_version == 5) && (minor_version >= 30)) || (major_version >= 6);
		}
		else if (strncmp (line, "--------", 8) == 0) {
			rar_comm->list_started = TRUE;
			if (! rar_comm->rar5)
			    rar_comm->rar4_odd_line = TRUE;
		}
		else if (strncmp (line, "Volume ", 7) == 0) {
			FR_ARCHIVE (comm)->multi_volume = TRUE;
		}
		else if (rar_comm->rar5 && (strncmp (line, " Attributes", 11) == 0)) {
			// This is the header line, find where the name column starts.
			const char *name_field = strstr (line, "Name");
			if (name_field != NULL) {
				rar_comm->name_column_start = name_field - line;
			}
		}
		return;
	}

	if (strncmp (line, "--------", 8) == 0) {
		rar_comm->list_started = FALSE;
		return;
	}

	if (rar_comm->rar4_odd_line || rar_comm->rar5) {
		if (!parse_name_field (line, line_len, rar_comm))
			return;
	}

	if (! rar_comm->rar4_odd_line) {
		FrFileData *fdata;
		const char *size_field, *ratio_field, *date_field, *time_field, *attr_field;

		fdata = rar_comm->fdata;

		/* read file info. */

		fields = _g_str_split_line (line, attribute_field_with_space (line) ? 7 : 6);
		if (rar_comm->rar5) {
			int offset = attribute_field_with_space (line) ? 1 : 0;

			size_field = fields[1+offset];
			ratio_field = fields[3+offset];
			date_field = fields[4+offset];
			time_field = fields[5+offset];
			attr_field = fields[0+offset];
		}
		else {
			size_field = fields[0];
			ratio_field = fields[2];
			date_field = fields[3];
			time_field = fields[4];
			attr_field = fields[5];
		}
		if (g_strv_length (fields) < 6) {
			/* wrong line format, treat this line as a filename line */
			g_strfreev (fields);
			fr_file_data_free (rar_comm->fdata);
			rar_comm->fdata = NULL;
			rar_comm->rar4_odd_line = TRUE;
			if (!parse_name_field (line, line_len, rar_comm))
				return;
		}
		else {
			if ((strcmp (ratio_field, "<->") == 0)
			    || (strcmp (ratio_field, "<--") == 0))
			{
				/* ignore files that span more volumes */

				fr_file_data_free (rar_comm->fdata);
				rar_comm->fdata = NULL;
			}
			else {
				fdata->size = g_ascii_strtoull (size_field, NULL, 10);

				fdata->modified = rar_comm->rar5_30 ? mktime_from_string_rar_5_30 (date_field, time_field) : mktime_from_string (date_field, time_field);

				if (attr_field_is_dir (attr_field, rar_comm)) {
					char *tmp;

					tmp = fdata->full_path;
					fdata->full_path = g_strconcat (fdata->full_path, "/", NULL);

					fdata->original_path = g_strdup (fdata->original_path);
					fdata->free_original_path = TRUE;

					g_free (tmp);

					fdata->name = _g_path_get_dir_name (fdata->full_path);
					fdata->dir = TRUE;
				}
				else {
					fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
					if (attr_field[0] == 'l')
						fdata->link = g_strdup (_g_path_get_basename (fdata->full_path));
				}

				fr_archive_add_file (FR_ARCHIVE (comm), fdata);
				rar_comm->fdata = NULL;
			}

			g_strfreev (fields);
		}
	}

	if (! rar_comm->rar5)
		rar_comm->rar4_odd_line = ! rar_comm->rar4_odd_line;
}


static void
add_password_arg (FrCommand  *comm,
		  const char *password,
		  gboolean    disable_query)
{
	if ((password != NULL) && (password[0] != '\0')) {
		if (FR_ARCHIVE (comm)->encrypt_header)
			fr_process_add_arg_concat (comm->process, "-hp", password, NULL);
		else
			fr_process_add_arg_concat (comm->process, "-p", password, NULL);
	}
	else if (disable_query)
		fr_process_add_arg (comm->process, "-p-");
}


static void
list__begin (gpointer data)
{
	FrCommandRar *comm = data;

	comm->list_started = FALSE;
	comm->name_column_start = -1;
}


static gboolean
fr_command_rar_list (FrCommand  *comm)
{
	rar_check_multi_volume (comm);

	fr_process_set_out_line_func (comm->process, process_line, comm);

	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");
	fr_process_set_begin_func (comm->process, list__begin, comm);
	fr_process_add_arg (comm->process, "v");
	fr_process_add_arg (comm->process, "-c-");
	fr_process_add_arg (comm->process, "-v");

	add_password_arg (comm, FR_ARCHIVE (comm)->password, TRUE);

	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--");

	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
parse_progress_line (FrCommand  *comm,
		     const char *prefix,
		     const char *message_format,
		     const char *line)
{
	FrArchive *archive = FR_ARCHIVE (comm);
	int        prefix_len;

	prefix_len = strlen (prefix);
	if (strncmp (line, prefix, prefix_len) == 0) {
		if (fr_archive_progress_get_total_files (archive) > 0) {
			fr_archive_progress (archive, fr_archive_progress_inc_completed_files (archive, 1));
		}
		else {
			char  filename[4096];
			char *b_idx;
			int   len;
			char *msg;

			strcpy (filename, line + prefix_len);

			/* when a new volume is created a sequence of backspaces is
			 * issued, remove the backspaces from the filename */
			b_idx = strchr (filename, '\x08');
			if (b_idx != NULL)
				*b_idx = 0;

			/* remove the OK at the end of the filename */
			len = strlen (filename);
			if ((len > 5) && (strncmp (filename + len - 5, "  OK ", 5) == 0))
				filename[len - 5] = 0;

			msg = g_strdup_printf (message_format, _g_path_get_basename (filename), NULL);
			fr_archive_message (archive, msg);

			g_free (msg);
		}
	}
}


static void
process_line__add (char     *line,
		   gpointer  data)
{
	FrCommand *comm = FR_COMMAND (data);
	FrArchive *archive = FR_ARCHIVE (comm);

	if (strncmp (line, "Creating archive ", 17) == 0) {
		const char *archive_filename = line + 17;
		char *uri;

		uri = g_filename_to_uri (archive_filename, NULL, NULL);
		if ((archive->volume_size > 0)
		    && g_regex_match_simple ("^.*\\.part(0)*2\\.rar$", uri, G_REGEX_CASELESS, 0))
		{
			char  *volume_filename;
			GFile *volume_file;

			volume_filename = g_strdup (archive_filename);
			volume_filename[strlen (volume_filename) - 5] = '1';
			volume_file = g_file_new_for_path (volume_filename);
			fr_archive_set_multi_volume (archive, volume_file);

			g_object_unref (volume_file);
			g_free (volume_filename);
		}
		fr_archive_working_archive (archive, uri);

		g_free (uri);
		return;
	}

	if (fr_archive_progress_get_total_files (archive) > 0)
		parse_progress_line (comm, "Adding    ", _("Adding “%s”"), line);
}


static void
fr_command_rar_add (FrCommand  *comm,
		    const char *from_file,
		    GList      *file_list,
		    const char *base_dir,
		    gboolean    update,
		    gboolean    follow_links)
{
	GList *scan;

	fr_process_use_standard_locale (comm->process, TRUE);
	fr_process_set_out_line_func (comm->process,
				      process_line__add,
				      comm);

	fr_process_begin_command (comm->process, "rar");

	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);

	if (update)
		fr_process_add_arg (comm->process, "u");
	else
		fr_process_add_arg (comm->process, "a");

	if (! follow_links)
		fr_process_add_arg (comm->process, "-ol");

	switch (FR_ARCHIVE (comm)->compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-m1"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-m2"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-m3"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-m5"); break;
	}

	add_password_arg (comm, FR_ARCHIVE (comm)->password, FALSE);

	if (FR_ARCHIVE (comm)->volume_size > 0)
		fr_process_add_arg_printf (comm->process, "-v%ub", FR_ARCHIVE (comm)->volume_size);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-Idp");

	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);
	else
		fr_process_add_arg_concat (comm->process, "@", from_file, NULL);

	fr_process_end_command (comm->process);
}


static void
process_line__delete (char     *line,
		      gpointer  data)
{
	FrCommand *comm = FR_COMMAND (data);

	if (strncmp (line, "Deleting from ", 14) == 0) {
		char *uri;

		uri = g_filename_to_uri (line + 14, NULL, NULL);
		fr_archive_working_archive (FR_ARCHIVE (comm), uri);
		g_free (uri);

		return;
	}

	if (fr_archive_progress_get_total_files (FR_ARCHIVE (comm)) > 0)
		parse_progress_line (comm, "Deleting ", _("Removing “%s”"), line);
}


static void
fr_command_rar_delete (FrCommand  *comm,
		       const char *from_file,
		       GList      *file_list)
{
	GList *scan;

	fr_process_use_standard_locale (comm->process, TRUE);
	fr_process_set_out_line_func (comm->process,
				      process_line__delete,
				      comm);

	fr_process_begin_command (comm->process, "rar");
	fr_process_add_arg (comm->process, "d");

	add_password_arg (comm, FR_ARCHIVE (comm)->password, FALSE);

	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);
	else
		fr_process_add_arg_concat (comm->process, "@", from_file, NULL);

	fr_process_end_command (comm->process);
}


static void
process_line__extract (char     *line,
		       gpointer  data)
{
	FrCommand *comm = FR_COMMAND (data);

	if (strncmp (line, "Extracting from ", 16) == 0) {
		char *uri;

		uri = g_filename_to_uri (line + 16, NULL, NULL);
		fr_archive_working_archive (FR_ARCHIVE (comm), uri);
		g_free (uri);

		return;
	}

	if (fr_archive_progress_get_total_files (FR_ARCHIVE (comm)) > 0)
		parse_progress_line (comm, "Extracting  ", _("Extracting “%s”"), line);
}


static void
fr_command_rar_extract (FrCommand  *comm,
			const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;

	fr_process_use_standard_locale (comm->process, TRUE);
	fr_process_set_out_line_func (comm->process,
				      process_line__extract,
				      comm);

	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");

	fr_process_add_arg (comm->process, "x");

	/* keep broken extracted files */
	fr_process_add_arg (comm->process, "-kb");

	if (overwrite)
		fr_process_add_arg (comm->process, "-o+");
	else
		fr_process_add_arg (comm->process, "-o-");

	if (skip_older)
		fr_process_add_arg (comm->process, "-u");

	if (junk_paths)
		fr_process_add_arg (comm->process, "-ep");

	add_password_arg (comm, FR_ARCHIVE (comm)->password, TRUE);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-Idp");

	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, comm->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);
	else
		fr_process_add_arg_concat (comm->process, "@", from_file, NULL);

	if (dest_dir != NULL)
		fr_process_add_arg (comm->process, dest_dir);

	fr_process_end_command (comm->process);
}


static void
fr_command_rar_test (FrCommand   *comm)
{
	if (have_rar ())
		fr_process_begin_command (comm->process, "rar");
	else
		fr_process_begin_command (comm->process, "unrar");

	fr_process_add_arg (comm->process, "t");

	add_password_arg (comm, FR_ARCHIVE (comm)->password, TRUE);

	/* disable percentage indicator */
	fr_process_add_arg (comm->process, "-Idp");

	/* stop switches scanning */
	fr_process_add_arg (comm->process, "--");

	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
}


static void
fr_command_rar_handle_error (FrCommand *comm,
			     FrError   *error)
{
	GList *scan;

#if 0
	{
		GList *scan;

		for (scan = g_list_last (comm->process->err.raw); scan; scan = scan->prev)
			g_print ("%s\n", (char*)scan->data);
	}
#endif

	if (error->type == FR_ERROR_NONE)
		return;

	if (error->status <= 1) {
		/* ignore warnings */
		fr_error_clear_gerror (error);
	}
	else if (error->status == 11) {
		/* handle wrong password */
		fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
		return;
	}

	gboolean checksum_error = FALSE;
	gboolean cannot_find_volume = FALSE;
	char *volume_filename = NULL;

	for (scan = g_list_last (comm->process->err.raw); scan; scan = scan->prev) {
		const char *line = scan->data;

		if (strncmp (line, "Unexpected end of archive", 25) == 0) {
			/* FIXME: handle this type of errors at a higher level when the freeze is over. */
		}

		if (strncmp (line, "Checksum error in the encrypted file", 36) == 0) {
			checksum_error = TRUE;
			break;
		}

		if (strncmp (line, "Cannot find volume", 18) == 0) {
			if (!cannot_find_volume) {
				volume_filename = g_path_get_basename (line + strlen ("Cannot find volume "));
				cannot_find_volume = TRUE;
			}
		}
	}

	// Sometimes rar writes both errors, give priority to the checksum error.
	if (checksum_error) {
		fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
	}
	else if (cannot_find_volume) {
		fr_error_take_gerror (error, g_error_new (FR_ERROR, FR_ERROR_MISSING_VOLUME, _("Could not find the volume: %s"), volume_filename));
		g_free (volume_filename);
	}
}


const char *rar_mime_type[] = { "application/x-cbr",
				"application/x-rar",
				"application/vnd.rar",
				NULL };


static const char **
fr_command_rar_get_mime_types (FrArchive *archive)
{
	return rar_mime_type;
}


static FrArchiveCaps
fr_command_rar_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES | FR_ARCHIVE_CAN_ENCRYPT | FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	if (_g_program_is_available ("rar", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE | FR_ARCHIVE_CAN_CREATE_VOLUMES;
	else if (_g_program_is_available ("unrar", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ;

	/* multi-volumes are read-only */
	if ((archive->files->len > 0) && archive->multi_volume && (capabilities & FR_ARCHIVE_CAN_WRITE))
		capabilities ^= FR_ARCHIVE_CAN_WRITE;

	return capabilities;
}


static const char *
fr_command_rar_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return FR_PACKAGES ("rar,unrar");
}


static void
fr_command_rar_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_RAR (object));

	FrCommandRar *rar_comm = FR_COMMAND_RAR (object);
	fr_file_data_free (rar_comm->fdata);

	if (G_OBJECT_CLASS (fr_command_rar_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_rar_parent_class)->finalize (object);
}


static void
fr_command_rar_class_init (FrCommandRarClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_rar_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_rar_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_rar_get_mime_types;
	archive_class->get_capabilities = fr_command_rar_get_capabilities;
	archive_class->get_packages     = fr_command_rar_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_rar_list;
	command_class->add              = fr_command_rar_add;
	command_class->delete           = fr_command_rar_delete;
	command_class->extract          = fr_command_rar_extract;
	command_class->test             = fr_command_rar_test;
	command_class->handle_error     = fr_command_rar_handle_error;
}


static void
fr_command_rar_init (FrCommandRar *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreFolders       = TRUE;
	base->propAddCanStoreLinks         = TRUE;
	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder      = TRUE;
	base->propExtractCanJunkPaths      = TRUE;
	base->propCanDeleteAllFiles        = FALSE;
	base->propPassword                 = TRUE;
	base->propTest                     = TRUE;
	base->propListFromFile             = TRUE;

	self->fdata = NULL;
}
