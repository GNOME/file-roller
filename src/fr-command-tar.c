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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-tar.h"

#define ACTIVITY_DELAY 20


struct _FrCommandTar
{
	FrCommand  parent_instance;

	char      *uncomp_filename;
	gboolean   name_modified;
	char      *compress_command;

	char      *msg;
};


G_DEFINE_TYPE (FrCommandTar, fr_command_tar, fr_command_get_type ())


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
		if (fields[1] != NULL) {
			tm.tm_mon = atoi (fields[1]) - 1;
			if (fields[2] != NULL)
				tm.tm_mday = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

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
tar_get_last_field (const char *line,
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
	char        *field_date, *field_time, *field_size, *field_name;
	char        *name;

	g_return_if_fail (line != NULL);

	date_idx = _g_line_get_index_from_pattern (line, "%n%n%n%n-%n%n-%n%n %n%n:%n%n");
	if (date_idx < 0)
		return;

	fdata = fr_file_data_new ();

	field_size = _g_line_get_prev_field (line, date_idx, 1);
	fdata->size = g_ascii_strtoull (field_size, NULL, 10);
	g_free (field_size);

	field_date = _g_line_get_next_field (line, date_idx, 1);
	field_time = _g_line_get_next_field (line, date_idx, 2);
	fdata->modified = mktime_from_string (field_date, field_time);
	g_free (field_date);
	g_free (field_time);

	/* Full path */

	field_name = tar_get_last_field (line, date_idx, 3);
	fields = g_strsplit (field_name, " -> ", 2);

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (field_name, " link to ", 2);
	}

	name = g_strcompress (fields[0]);
	if (*name == '/') {
		fdata->full_path = g_strdup (name);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name, NULL);
		fdata->original_path = fdata->full_path + 1;
	}
	g_free (name);
	name = g_filename_from_utf8 (fdata->original_path, -1, NULL, NULL, NULL);
	if (name)
		fdata->original_path = name;

	if (fields[1] != NULL)
		fdata->link = g_strdup (fields[1]);
	g_strfreev (fields);
	g_free (field_name);

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
add_compress_arg (FrCommand *comm)
{
	FrArchive *archive = FR_ARCHIVE (comm);

	if (_g_mime_type_matches (archive->mime_type, "application/x-compressed-tar"))
		fr_process_add_arg (comm->process, "-z");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=brotli");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=bzip2");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=bzip3");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-tarz")) {
		if (_g_program_is_in_path ("gzip"))
			fr_process_add_arg (comm->process, "-z");
		else
			fr_process_add_arg (comm->process, "-Z");
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lrzip-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=lrzip");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=lz4");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=lzip");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=lzma");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-xz-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=xz");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-tzo"))
		fr_process_add_arg (comm->process, "--use-compress-program=lzop");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-zstd-compressed-tar"))
		fr_process_add_arg (comm->process, "--use-compress-program=zstd");

	else if (_g_mime_type_matches (archive->mime_type, "application/x-7z-compressed-tar")) {
		FrCommandTar *comm_tar = (FrCommandTar*) comm;
		char         *option;

		option = g_strdup_printf ("--use-compress-program=%s", comm_tar->compress_command);
		fr_process_add_arg (comm->process, option);
		g_free (option);
	}
}


static void
begin_tar_command (FrCommand *comm)
{
	char *command = NULL;

	/* In solaris gtar is present under /usr/sfw/bin */

	command = g_find_program_in_path ("gtar");
#if defined (__SVR4) && defined (__sun)
	if (g_file_test ("/usr/sfw/bin/gtar", G_FILE_TEST_IS_EXECUTABLE))
		command = g_strdup ("/usr/sfw/bin/gtar");
#endif
	if (command != NULL)
		fr_process_begin_command (comm->process, command);
	else
		fr_process_begin_command (comm->process, "tar");
	g_free (command);
}


static gboolean
fr_command_tar_list (FrCommand *comm)
{
	fr_process_set_out_line_func (comm->process, process_line, comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "--no-wildcards");
	fr_process_add_arg (comm->process, "-tvf");
	fr_process_add_arg (comm->process, comm->filename);
	add_compress_arg (comm);
	fr_process_end_command (comm->process);

	return TRUE;
}


static gboolean
can_create_a_compressed_archive (FrCommand *comm)
{
	return comm->creating_archive &&
		! _g_mime_type_matches (FR_ARCHIVE (comm)->mime_type, "application/x-7z-compressed-tar") &&
		! _g_mime_type_matches (FR_ARCHIVE (comm)->mime_type, "application/x-rzip-compressed-tar");
}


static void
process_line__generic (char     *line,
		       gpointer  data,
		       char     *message_format)
{
	FrCommand *comm = FR_COMMAND (data);
	FrArchive *archive = FR_ARCHIVE (comm);

	if (line == NULL)
		return;

	if (line[strlen (line) - 1] == '/') /* ignore directories */
		return;

	if (fr_archive_progress_get_total_files (archive) > 1) {
		fr_archive_progress (archive, fr_archive_progress_inc_completed_files (archive, 1));
	}
	else {
		char *msg = g_strdup_printf (message_format, _g_path_get_basename (line), NULL);
		fr_archive_message (archive, msg);
		g_free (msg);
	}
}


static void
process_line__add (char     *line,
		   gpointer  data)
{
	/* Translators: %s is a filename. */
	process_line__generic (line, data, _("Adding “%s”"));
}


static void
fr_command_tar_add (FrCommand  *comm,
		    const char *from_file,
		    GList      *file_list,
		    const char *base_dir,
		    gboolean    update,
		    gboolean    follow_links)
{
	FrCommandTar *c_tar = FR_COMMAND_TAR (comm);
	GList        *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process,
				      process_line__add,
				      comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "--no-recursion");
	fr_process_add_arg (comm->process, "--no-wildcards");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-p");
	if (follow_links)
		fr_process_add_arg (comm->process, "-h");

	if (base_dir != NULL) {
		fr_process_add_arg (comm->process, "-C");
		fr_process_add_arg (comm->process, base_dir);
	}

	if (can_create_a_compressed_archive (comm)) {
		fr_process_add_arg (comm->process, "-cf");
		fr_process_add_arg (comm->process, comm->filename);
		add_compress_arg (comm);
	}
	else {
		if (comm->creating_archive)
			fr_process_add_arg (comm->process, "-cf");
		else
			fr_process_add_arg (comm->process, "-rf");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
	}

	if (from_file != NULL) {
		fr_process_add_arg (comm->process, "-T");
		fr_process_add_arg (comm->process, from_file);
	}

	fr_process_add_arg (comm->process, "--");

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
process_line__delete (char     *line,
		      gpointer  data)
{
	/* Translators: %s is a filename. */
	process_line__generic (line, data, _("Removing “%s”"));
}


static void
begin_func__delete (gpointer data)
{
	FrArchive *archive = data;

	fr_archive_progress (archive, -1.0);
	fr_archive_message (archive, _("Deleting files from archive"));
}


static void
fr_command_tar_delete (FrCommand  *comm,
		       const char *from_file,
		       GList      *file_list)
{
	FrCommandTar *c_tar = FR_COMMAND_TAR (comm);
	GList        *scan;

	fr_process_set_out_line_func (comm->process,
				      process_line__delete,
				      comm);

	begin_tar_command (comm);
	fr_process_set_begin_func (comm->process, begin_func__delete, comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "--no-wildcards");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "--delete");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, c_tar->uncomp_filename);

	if (from_file != NULL) {
		fr_process_add_arg (comm->process, "-T");
		fr_process_add_arg (comm->process, from_file);
	}

	fr_process_add_arg (comm->process, "--");

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
process_line__extract (char     *line,
		       gpointer  data)
{
	/* Translators: %s is a filename. */
	process_line__generic (line, data, _("Extracting “%s”"));
}


static void
fr_command_tar_extract (FrCommand  *comm,
		        const char *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList *scan;

	fr_process_set_out_line_func (comm->process,
				      process_line__extract,
				      comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "--no-wildcards");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-p");

	if (! overwrite)
		fr_process_add_arg (comm->process, "-k");
	if (skip_older)
		fr_process_add_arg (comm->process, "--keep-newer-files");

	fr_process_add_arg (comm->process, "-xf");
	fr_process_add_arg (comm->process, comm->filename);
	add_compress_arg (comm);

	if (dest_dir != NULL) {
		fr_process_add_arg (comm->process, "-C");
		fr_process_add_arg (comm->process, dest_dir);
	}

	if (from_file != NULL) {
		fr_process_add_arg (comm->process, "-T");
		fr_process_add_arg (comm->process, from_file);
	}

	fr_process_add_arg (comm->process, "--");

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
begin_func__recompress (gpointer data)
{
	FrArchive *archive = data;

	fr_archive_progress (archive, -1.0);
	fr_archive_message (archive, _("Recompressing archive"));
}


static gboolean
gzip_continue_func (FrError  **error,
		    gpointer   user_data)
{
	/* ignore gzip warnings */

	if ((*error != NULL) && ((*error)->status == 2))
		fr_clear_error (error);

	return *error == NULL;
}


static void
fr_command_tar_recompress (FrCommand *comm)
{
	FrCommandTar *c_tar = FR_COMMAND_TAR (comm);
	FrArchive    *archive = FR_ARCHIVE (comm);
	char         *new_name = NULL;

	if (can_create_a_compressed_archive (comm))
		return;

	if (_g_mime_type_matches (archive->mime_type, "application/x-compressed-tar")) {
		fr_process_begin_command (comm->process, "gzip");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		fr_process_set_continue_func (comm->process, gzip_continue_func, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".gz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli-compressed-tar")) {
		fr_process_begin_command (comm->process, "brotli");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
			case FR_COMPRESSION_VERY_FAST:
				fr_process_add_arg (comm->process, "-1"); break;
			case FR_COMPRESSION_FAST:
				fr_process_add_arg (comm->process, "-3"); break;
			case FR_COMPRESSION_NORMAL:
				fr_process_add_arg (comm->process, "-6"); break;
			case FR_COMPRESSION_MAXIMUM:
				fr_process_add_arg (comm->process, "--best"); break; // i.e. -q 11
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".br", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip-compressed-tar")) {
		fr_process_begin_command (comm->process, "bzip2");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".bz2", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3-compressed-tar")) {
		fr_process_begin_command (comm->process, "bzip3");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		//switch (archive->compression) {
		//case FR_COMPRESSION_VERY_FAST:
		//	fr_process_add_arg (comm->process, "-1"); break;
		//case FR_COMPRESSION_FAST:
		//	fr_process_add_arg (comm->process, "-3"); break;
		//case FR_COMPRESSION_NORMAL:
		//	fr_process_add_arg (comm->process, "-6"); break;
		//case FR_COMPRESSION_MAXIMUM:
		//	fr_process_add_arg (comm->process, "-9"); break;
		//}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-e");
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".bz3", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-tarz")) {
		fr_process_begin_command (comm->process, "compress");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".Z", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lrzip-compressed-tar")) {
		fr_process_begin_command (comm->process, "lrzip");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-l"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-g"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-b"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-z"); break;
		}
		fr_process_add_arg (comm->process, "-o");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".lrz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4-compressed-tar")) {
		fr_process_begin_command (comm->process, "lz4");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-z");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		new_name = g_strconcat (c_tar->uncomp_filename, ".lz4", NULL);
		fr_process_add_arg (comm->process, new_name);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip-compressed-tar")) {
		fr_process_begin_command (comm->process, "lzip");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".lz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma-compressed-tar")) {
		fr_process_begin_command (comm->process, "lzma");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".lzma", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-xz-compressed-tar")) {
		fr_process_begin_command (comm->process, "xz");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".xz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-tzo")) {
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-9"); break;
		}
		fr_process_add_arg (comm->process, "-fU");
		fr_process_add_arg (comm->process, "--no-stdin");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".lzo", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-7z-compressed-tar")) {
		FrCommandTar *comm_tar = (FrCommandTar*) comm;

		fr_process_begin_command (comm->process, comm_tar->compress_command);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-mx=1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-mx=5"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-mx=5"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-mx=7"); break;
		}
		fr_process_add_arg (comm->process, "a");
		fr_process_add_arg (comm->process, "-bd");
		fr_process_add_arg (comm->process, "-y");
		fr_process_add_arg (comm->process, "-l");

		new_name = g_strconcat (c_tar->uncomp_filename, ".7z", NULL);
		fr_process_add_arg_concat (comm->process, new_name, NULL);

		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		/* remove the uncompressed tar */

		fr_process_begin_command (comm->process, "rm");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-rzip-compressed-tar")) {
		fr_process_begin_command (comm->process, "rzip");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-L1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-L3"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-L6"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "-L9"); break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".rz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-zstd-compressed-tar")) {
		fr_process_begin_command (comm->process, "zstd");
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (archive->compression) {
		case FR_COMPRESSION_VERY_FAST:
			fr_process_add_arg (comm->process, "-1"); break;
		case FR_COMPRESSION_FAST:
			fr_process_add_arg (comm->process, "-2"); break;
		case FR_COMPRESSION_NORMAL:
			fr_process_add_arg (comm->process, "-3"); break;
		case FR_COMPRESSION_MAXIMUM:
			fr_process_add_arg (comm->process, "--ultra");
			fr_process_add_arg (comm->process, "-22");
			break;
		}
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".zst", NULL);
	}

	if (c_tar->name_modified) {
		char *tmp_dir;

		/* Restore original name. */

		fr_process_begin_command (comm->process, "mv");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, new_name);
		fr_process_add_arg (comm->process, comm->filename);
		fr_process_end_command (comm->process);

		tmp_dir = _g_path_remove_level (new_name);

		fr_process_begin_command (comm->process, "rm");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_add_arg (comm->process, "-fr");
		fr_process_add_arg (comm->process, tmp_dir);
		fr_process_end_command (comm->process);

		g_free (tmp_dir);
	}

	g_free (new_name);
	g_free (c_tar->uncomp_filename);
	c_tar->uncomp_filename = NULL;
}


static void
begin_func__uncompress (gpointer data)
{
	FrArchive *archive = data;

	fr_archive_progress (archive, -1.0);
	fr_archive_message (archive, _("Decompressing archive"));
}


static char *
get_uncompressed_name (FrCommandTar *c_tar,
		       const char   *e_filename)
{
	FrCommand *comm = FR_COMMAND (c_tar);
	FrArchive *archive = FR_ARCHIVE (comm);
	char      *new_name = g_strdup (e_filename);
	int        l = strlen (new_name);

	if (_g_mime_type_matches (archive->mime_type, "application/x-compressed-tar")) {
		/* X.tgz     -->  X.tar
		 * X.tar.gz  -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tgz")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		}
		else if (_g_filename_has_extension (e_filename, ".tar.gz"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli-compressed-tar")) {
		/* X.tar.br --> X.tar  */
		if (_g_filename_has_extension (e_filename, ".tar.br"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip-compressed-tar")) {
		/* X.tbz2    -->  X.tar
		 * X.tar.bz2 -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tbz2")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		}
		else if (_g_filename_has_extension (e_filename, ".tar.bz2"))
			new_name[l - 4] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3-compressed-tar")) {
		/* X.tbz3    -->  X.tar
		 * X.tar.bz3 -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tbz3")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		}
		else if (_g_filename_has_extension (e_filename, ".tar.bz3"))
			new_name[l - 4] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-tarz")) {
		/* X.taz   -->  X.tar
		 * X.tar.Z -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".taz"))
			new_name[l - 1] = 'r';
		else if (_g_filename_has_extension (e_filename, ".tar.Z"))
			new_name[l - 2] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lrzip-compressed-tar")) {
		/* X.tlrz     -->  X.tar
		 * X.tar.lrz  -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tlrz")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		}
		else if (_g_filename_has_extension (e_filename, ".tar.lrz"))
			new_name[l - 4] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4-compressed-tar")) {
		/* X.tlz4     -->  X.tar
		 * X.tar.lz4  -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tlz4")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = '0';
		}
		else if (_g_filename_has_extension (e_filename, ".tar.lz4"))
			new_name[l - 4] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip-compressed-tar")) {
		/* X.tlz     -->  X.tar
		 * X.tar.lz  -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tlz")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		}
		else if (_g_filename_has_extension (e_filename, ".tar.lz"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma-compressed-tar")) {
		/* X.tar.lzma --> X.tar
		 * (There doesn't seem to be a shorthand suffix) */
		if (_g_filename_has_extension (e_filename, ".tar.lzma"))
			new_name[l - 5] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-xz-compressed-tar")) {
		/* X.tar.xz --> X.tar
		 * (There doesn't seem to be a shorthand suffix) */
		if (_g_filename_has_extension (e_filename, ".tar.xz"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-tzo")) {
		/* X.tzo     -->  X.tar
		 * X.tar.lzo -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tzo")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		}
		else if (_g_filename_has_extension (e_filename, ".tar.lzo"))
			new_name[l - 4] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-7z-compressed-tar")) {
		/* X.tar.7z -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tar.7z"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-rzip-compressed-tar")) {
		/* X.tar.rz -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tar.rz"))
			new_name[l - 3] = 0;
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-zstd-compressed-tar")) {
		/* X.tzst    -->  X.tar
		 * X.tar.zst -->  X.tar */
		if (_g_filename_has_extension (e_filename, ".tzst")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		}
		else if (_g_filename_has_extension (e_filename, ".tar.zst"))
			new_name[l - 4] = 0;
	}

	return new_name;
}


#define MAX_TRIES 50


static char *
get_temp_name (FrCommandTar *c_tar,
	       const char   *filepath)
{
	char *dirname = _g_path_remove_level (filepath);
	char *template;
	char *result = NULL;
	char *temp_name = NULL;

	template = g_strconcat (dirname, "/.fr-XXXXXX", NULL);
	result = g_mkdtemp (template);
	temp_name = g_build_filename (result, _g_path_get_basename (filepath), NULL);
	g_free (template);

	return temp_name;
}


static void
fr_command_tar_uncompress (FrCommand *comm)
{
	FrCommandTar *c_tar = FR_COMMAND_TAR (comm);
	FrArchive    *archive = FR_ARCHIVE (comm);
	char         *tmp_name;
	char         *tmp_dir;
	gboolean      archive_exists;

	if (can_create_a_compressed_archive (comm))
		return;

	if (c_tar->uncomp_filename != NULL) {
		g_free (c_tar->uncomp_filename);
		c_tar->uncomp_filename = NULL;
	}

	archive_exists = ! comm->creating_archive;

	c_tar->name_modified = ! _g_mime_type_matches (archive->mime_type, "application/x-tar");
	if (c_tar->name_modified) {
		tmp_name = get_temp_name (c_tar, comm->filename);
		if (archive_exists) {
			fr_process_begin_command (comm->process, "mv");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "--");
			fr_process_add_arg (comm->process, comm->filename);
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
	}
	else
		tmp_name = g_strdup (comm->filename);
	tmp_dir = _g_path_remove_level (tmp_name);

	c_tar->uncomp_filename = get_uncompressed_name (c_tar, tmp_name);

	if (archive_exists) {
		if (_g_mime_type_matches (archive->mime_type, "application/x-compressed-tar")) {
			fr_process_begin_command (comm->process, "gzip");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_set_continue_func (comm->process, gzip_continue_func, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli-compressed-tar")) {
			fr_process_begin_command (comm->process, "brotli");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip-compressed-tar")) {
			fr_process_begin_command (comm->process, "bzip2");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3-compressed-tar")) {
			fr_process_begin_command (comm->process, "bzip3");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, "--");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-tarz")) {
			if (_g_program_is_in_path ("gzip")) {
				fr_process_begin_command (comm->process, "gzip");
				fr_process_set_continue_func (comm->process, gzip_continue_func, comm);
			}
			else
				fr_process_begin_command (comm->process, "uncompress");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-lrzip-compressed-tar")) {
			fr_process_begin_command (comm->process, "lrzip");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4-compressed-tar")) {
			fr_process_begin_command (comm->process, "lz4");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_add_arg (comm->process, c_tar->uncomp_filename);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip-compressed-tar")) {
			fr_process_begin_command (comm->process, "lzip");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma-compressed-tar")) {
			fr_process_begin_command (comm->process, "lzma");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-xz-compressed-tar")) {
			fr_process_begin_command (comm->process, "xz");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-tzo")) {
			fr_process_begin_command (comm->process, "lzop");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-dfU");
			fr_process_add_arg (comm->process, "--no-stdin");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-7z-compressed-tar")) {
			FrCommandTar *comm_tar = (FrCommandTar*) comm;

			fr_process_begin_command (comm->process, comm_tar->compress_command);
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "e");
			fr_process_add_arg (comm->process, "-bd");
			fr_process_add_arg (comm->process, "-y");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);

			/* remove the compressed tar */

			fr_process_begin_command (comm->process, "rm");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-rzip-compressed-tar")) {
			fr_process_begin_command (comm->process, "rzip");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-df");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		else if (_g_mime_type_matches (archive->mime_type, "application/x-zstd-compressed-tar")) {
			fr_process_begin_command (comm->process, "zstd");
			fr_process_set_working_dir (comm->process, tmp_dir);
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
	}

	g_free (tmp_dir);
	g_free (tmp_name);
}


static void
fr_command_tar_handle_error (FrCommand   *comm,
			     FrError *error)
{
	if (error->type != FR_ERROR_NONE) {
		if (error->status <= 1)
			fr_error_clear_gerror (error);
	}
}


const char *tar_mime_types[] = { "application/x-compressed-tar",
				 "application/x-brotli-compressed-tar",
				 "application/x-bzip-compressed-tar",
				 "application/x-bzip3-compressed-tar",
				 "application/x-tar",
				 "application/x-7z-compressed-tar",
				 "application/x-lrzip-compressed-tar",
				 "application/x-lz4-compressed-tar",
				 "application/x-lzip-compressed-tar",
			         "application/x-lzma-compressed-tar",
			         "application/x-rzip-compressed-tar",
			         "application/x-tarz",
				 "application/x-tzo",
				 "application/x-xz-compressed-tar",
				 "application/x-zstd-compressed-tar",
			         NULL };


static const char **
fr_command_tar_get_mime_types (FrArchive *archive)
{
	return tar_mime_types;
}


static FrArchiveCaps
fr_command_tar_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;

	/* In solaris gtar is present under /usr/sfw/bin */
	if (! _g_program_is_available ("tar", check_command) && ! _g_program_is_available ("/usr/sfw/bin/gtar", check_command))
		return capabilities;

	if (_g_mime_type_matches (mime_type, "application/x-tar")) {
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-compressed-tar")) {
		if (_g_program_is_available ("gzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-brotli-compressed-tar")) {
		if (_g_program_is_available ("brotli", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-bzip-compressed-tar")) {
		if (_g_program_is_available ("bzip2", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-bzip3-compressed-tar")) {
		if (_g_program_is_available ("bzip3", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-tarz")) {
		if (_g_program_is_available ("compress", check_command) && _g_program_is_available ("uncompress", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
		else if (_g_program_is_available ("gzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lrzip-compressed-tar")) {
		if (_g_program_is_available ("lrzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lz4-compressed-tar")) {
		if (_g_program_is_available ("lz4", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lzip-compressed-tar")) {
		if (_g_program_is_available ("lzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lzma-compressed-tar")) {
		if (_g_program_is_available ("lzma", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-xz-compressed-tar")) {
		if (_g_program_is_available ("xz", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-tzo")) {
		if (_g_program_is_available ("lzop", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-7z-compressed-tar")) {
		char *try_command[5] = { "7zzs", "7zz", "7za", "7zr", "7z" };

		for (size_t i = 0; i < G_N_ELEMENTS (try_command); i++) {
			if (_g_program_is_available (try_command[i], check_command)) {
				capabilities |= FR_ARCHIVE_CAN_WRITE;
				break;
			}
		}
	}
	else if(_g_mime_type_matches (mime_type, "application/x-rzip-compressed-tar")) {
		if(_g_program_is_available ("rzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-zstd-compressed-tar")) {
		if (_g_program_is_available ("zstd", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}

	return capabilities;
}


static void
fr_command_tar_set_mime_type (FrArchive  *archive,
		 	      const char *mime_type)
{
	FrCommandTar *comm_tar = FR_COMMAND_TAR (archive);

	FR_ARCHIVE_CLASS (fr_command_tar_parent_class)->set_mime_type (archive, mime_type);

	if (_g_mime_type_matches (mime_type, "application/x-7z-compressed-tar")) {
		char *try_command[5] = { "7zzs", "7zz", "7za", "7zr", "7z" };

		for (size_t i = 0; i < G_N_ELEMENTS (try_command); i++) {
			if (_g_program_is_in_path (try_command[i])) {
				comm_tar->compress_command = g_strdup (try_command[i]);
				break;
			}
		}
	}
}


static const char *
fr_command_tar_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	if (_g_mime_type_matches (mime_type, "application/x-tar"))
		return FR_PACKAGES ("tar");
	else if (_g_mime_type_matches (mime_type, "application/x-compressed-tar"))
		return FR_PACKAGES ("tar,gzip");
	else if (_g_mime_type_matches (mime_type, "application/x-brotli-compressed-tar"))
		return FR_PACKAGES ("tar,brotli");
	else if (_g_mime_type_matches (mime_type, "application/x-bzip-compressed-tar"))
		return FR_PACKAGES ("tar,bzip2");
	else if (_g_mime_type_matches (mime_type, "application/x-bzip3-compressed-tar"))
		return FR_PACKAGES ("tar,bzip3");
	else if (_g_mime_type_matches (mime_type, "application/x-tarz"))
		return FR_PACKAGES ("tar,gzip,ncompress");
	else if (_g_mime_type_matches (mime_type, "application/x-lrzip-compressed-tar"))
		return FR_PACKAGES ("tar,lrzip");
	else if (_g_mime_type_matches (mime_type, "application/x-lz4-compressed-tar"))
		return FR_PACKAGES ("tar,lz4");
	else if (_g_mime_type_matches (mime_type, "application/x-lzip-compressed-tar"))
		return FR_PACKAGES ("tar,lzip");
	else if (_g_mime_type_matches (mime_type, "application/x-lzma-compressed-tar"))
		return FR_PACKAGES ("tar,lzma");
	else if (_g_mime_type_matches (mime_type, "application/x-xz-compressed-tar"))
		return FR_PACKAGES ("tar,xz");
	else if (_g_mime_type_matches (mime_type, "application/x-tzo"))
		return FR_PACKAGES ("tar,lzop");
	else if (_g_mime_type_matches (mime_type, "application/x-7z-compressed-tar"))
		return FR_PACKAGES ("tar,7zip");
	else if (_g_mime_type_matches (mime_type, "application/x-rzip-compressed-tar"))
		return FR_PACKAGES ("tar,rzip");
	else if (_g_mime_type_matches (mime_type, "application/x-zstd-compressed-tar"))
		return FR_PACKAGES ("tar,zstd");

	return NULL;
}


static void
fr_command_tar_finalize (GObject *object)
{
	FrCommandTar *self;

        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_TAR (object));

	self = FR_COMMAND_TAR (object);

	if (self->uncomp_filename != NULL) {
		g_free (self->uncomp_filename);
		self->uncomp_filename = NULL;
	}

	if (self->msg != NULL) {
		g_free (self->msg);
		self->msg = NULL;
	}

	if (self->compress_command != NULL) {
		g_free (self->compress_command);
		self->compress_command = NULL;
	}

	/* Chain up */
        if (G_OBJECT_CLASS (fr_command_tar_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_tar_parent_class)->finalize (object);
}


static void
fr_command_tar_class_init (FrCommandTarClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_tar_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_tar_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_tar_get_mime_types;
	archive_class->get_capabilities = fr_command_tar_get_capabilities;
	archive_class->set_mime_type    = fr_command_tar_set_mime_type;
	archive_class->get_packages     = fr_command_tar_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_tar_list;
	command_class->add              = fr_command_tar_add;
	command_class->delete           = fr_command_tar_delete;
	command_class->extract          = fr_command_tar_extract;
	command_class->handle_error     = fr_command_tar_handle_error;
	command_class->recompress       = fr_command_tar_recompress;
	command_class->uncompress       = fr_command_tar_uncompress;
}


static void
fr_command_tar_init (FrCommandTar *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate              = FALSE;
	base->propAddCanReplace             = FALSE;
	base->propAddCanStoreFolders        = TRUE;
	base->propAddCanStoreLinks          = TRUE;
	base->propExtractCanAvoidOverwrite  = FALSE;
	base->propExtractCanSkipOlder       = TRUE;
	base->propExtractCanJunkPaths       = FALSE;
	base->propPassword                  = FALSE;
	base->propTest                      = FALSE;
	base->propCanDeleteNonEmptyFolders  = FALSE;
	base->propCanExtractNonEmptyFolders = FALSE;
	base->propListFromFile              = TRUE;

	self->msg = NULL;
	self->uncomp_filename = NULL;
}
