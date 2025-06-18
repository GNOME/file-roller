/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004, 2008 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-7z.h"
#include "rar-utils.h"


struct _FrCommand7z
{
	FrCommand  parent_instance;
	gboolean   list_started;
	FrFileData *fdata;
};


G_DEFINE_TYPE (FrCommand7z, fr_command_7z, fr_command_get_type ())


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
		tm.tm_mon = atoi (fields[1]) - 1;
		tm.tm_mday = atoi (fields[2]);
	}
	g_strfreev (fields);

	/* time */

	fields = g_strsplit (time_s, ":", 3);
	if (fields[0] != NULL) {
		tm.tm_hour = atoi (fields[0]);
		if (fields[1] != NULL) {
			tm.tm_min = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrCommand7z  *self = FR_COMMAND_7Z (data);
	FrArchive    *archive = FR_ARCHIVE (data);
	char        **fields;
	FrFileData *fdata;

	g_return_if_fail (line != NULL);

	if (! self->list_started) {
		if (strcmp (line, "----------") == 0)
			self->list_started = TRUE;
		else if (strncmp (line, "Multivolume = ", 14) == 0) {
			fields = g_strsplit (line, " = ", 2);
			archive->multi_volume = (strcmp (fields[1], "+") == 0);
			g_strfreev (fields);
		}
		return;
	}

	if (strcmp (line, "") == 0) {
		if (self->fdata != NULL) {
			if (self->fdata->original_path == NULL) {
				fr_file_data_free (self->fdata);
				self->fdata = NULL;
			}
			else {
				fdata = self->fdata;
				if (fdata->dir)
					fdata->name = _g_path_get_dir_name (fdata->full_path);
				else
					fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
				fdata->path = _g_path_remove_level (fdata->full_path);
				fr_archive_add_file (archive, fdata);
				self->fdata = NULL;
			}
		}
		return;
	}

	if (self->fdata == NULL)
		self->fdata = fr_file_data_new ();

	fields = g_strsplit (line, " = ", 2);

	if (g_strv_length (fields) < 2) {
		g_strfreev (fields);
		return;
	}

	fdata = self->fdata;

	if (strcmp (fields[0], "Path") == 0) {
		fdata->free_original_path = TRUE;
		fdata->original_path = g_strdup (fields[1]);
		fdata->full_path = g_strconcat ((fdata->original_path[0] != '/') ? "/" : "",
						fdata->original_path,
						(fdata->dir && (fdata->original_path[strlen (fdata->original_path) - 1] != '/')) ? "/" : "",
						NULL);
	}
	else if (strcmp (fields[0], "Folder") == 0) {
		fdata->dir = (strcmp (fields[1], "+") == 0);
	}
	else if (strcmp (fields[0], "Size") == 0) {
		fdata->size = g_ascii_strtoull (fields[1], NULL, 10);
	}
	else if (strcmp (fields[0], "Modified") == 0) {
		char **modified_fields;

		modified_fields = g_strsplit (fields[1], " ", 2);
		if (modified_fields[0] != NULL)
			fdata->modified = mktime_from_string (modified_fields[0], modified_fields[1]);
		g_strfreev (modified_fields);
	}
	else if (strcmp (fields[0], "Encrypted") == 0) {
		if (strcmp (fields[1], "+") == 0)
			fdata->encrypted = TRUE;
	}
	else if (strcmp (fields[0], "Method") == 0) {
		if (strstr (fields[1], "AES") != NULL)
			fdata->encrypted = TRUE;
	}
	else if (strcmp (fields[0], "Attributes") == 0) {
		if (fields[1][0] == 'D')
			fdata->dir = TRUE;
	}
	g_strfreev (fields);
}


static void
fr_command_7z_begin_command (FrCommand *comm)
{
	// Modern 7-Zip by the original author.
	// Statically linked from a binary distribution, almost guaranteed to work.
	if (_g_program_is_in_path ("7zzs"))
		fr_process_begin_command (comm->process, "7zzs");
	// Dynamically linked from either binary or source distribution.
	else if (_g_program_is_in_path ("7zz"))
		fr_process_begin_command (comm->process, "7zz");
	// Legacy p7zip project.
	else if (_g_program_is_in_path ("7z"))
		fr_process_begin_command (comm->process, "7z");
	else if (_g_program_is_in_path ("7za"))
		fr_process_begin_command (comm->process, "7za");
	else if (_g_program_is_in_path ("7zr"))
		fr_process_begin_command (comm->process, "7zr");
}


static void
add_password_arg (FrCommand  *command,
		  const char *password,
		  gboolean    always_specify)
{
	if (always_specify || ((password != NULL) && (*password != 0))) {
		char *arg;

		arg = g_strconcat ("-p", password, NULL);
		fr_process_add_arg (command->process, arg);
		g_free (arg);
	}
}


static void
list__begin (gpointer data)
{
	FrCommand7z *p7z_comm = data;

	if (p7z_comm->fdata != NULL) {
		fr_file_data_free (p7z_comm->fdata);
		p7z_comm->fdata = NULL;
	}
	p7z_comm->list_started = FALSE;
}


static gboolean
fr_command_7z_list (FrCommand *command)
{
	rar_check_multi_volume (command);

	fr_process_set_out_line_func (command->process, list__process_line, command);

	fr_command_7z_begin_command (command);
	fr_process_set_begin_func (command->process, list__begin, command);
	fr_process_add_arg (command->process, "l");
	fr_process_add_arg (command->process, "-slt");
	fr_process_add_arg (command->process, "-bd");
	fr_process_add_arg (command->process, "-y");
	add_password_arg (command, FR_ARCHIVE (command)->password, FALSE);
	fr_process_add_arg (command->process, "--");
	fr_process_add_arg (command->process, command->filename);
	fr_process_end_command (command->process);

	return TRUE;
}


static void
parse_progress_line (FrArchive  *archive,
		     const char *prefix,
		     const char *message_format,
		     const char *line)
{
	int prefix_len;

	prefix_len = strlen (prefix);
	if (strncmp (line, prefix, prefix_len) == 0) {
		if (fr_archive_progress_get_total_files (archive) > 1) {
			fr_archive_progress (archive, fr_archive_progress_inc_completed_files (archive, 1));
		}
		else {
			char  filename[4196];
			char *msg;

			strcpy (filename, line + prefix_len);
			msg = g_strdup_printf (message_format, filename, NULL);
			fr_archive_message (archive, msg);

			g_free (msg);
		}
	}
}


static void
process_line__add (char     *line,
		   gpointer  data)
{
	FrCommand *command = FR_COMMAND (data);
	FrArchive *archive = FR_ARCHIVE (data);

	if ((archive->volume_size > 0) && (strncmp (line, "Creating archive", 16) == 0)) {
		char  *volume_filename;
		GFile *volume_file;

		volume_filename = g_strconcat (command->filename, ".001", NULL);
		volume_file = g_file_new_for_path (volume_filename);
		fr_archive_set_multi_volume (archive, volume_file);

		g_object_unref (volume_file);
		g_free (volume_filename);
	}

	if (fr_archive_progress_get_total_files (archive) > 0)
		parse_progress_line (archive, "+ ", _("Adding “%s”"), line);
}


static void
fr_command_7z_add (FrCommand  *command,
		   const char *from_file,
		   GList      *file_list,
		   const char *base_dir,
		   gboolean    update,
		   gboolean    follow_links)
{
	FrArchive *archive = FR_ARCHIVE (command);
	GList     *scan;

	fr_process_use_standard_locale (command->process, TRUE);
	fr_process_set_out_line_func (command->process,
				      process_line__add,
				      command);

	fr_command_7z_begin_command (command);

	if (update)
		fr_process_add_arg (command->process, "u");
	else
		fr_process_add_arg (command->process, "a");

	if (base_dir != NULL)
		fr_process_set_working_dir (command->process, base_dir);

	if (_g_mime_type_matches (archive->mime_type, "application/zip")
	    || _g_mime_type_matches (archive->mime_type, "application/x-cbz"))
	{
		fr_process_add_arg (command->process, "-tzip");
		fr_process_add_arg (command->process, "-mem=AES128");
	}

	fr_process_add_arg (command->process, "-bd");
	fr_process_add_arg (command->process, "-bb1");
	fr_process_add_arg (command->process, "-y");
	if (!follow_links) {
		fr_process_add_arg (command->process, "-snl");
	}
	add_password_arg (command, archive->password, FALSE);
	if ((archive->password != NULL)
	    && (*archive->password != 0)
	    && archive->encrypt_header
	    && fr_archive_is_capable_of (archive, FR_ARCHIVE_CAN_ENCRYPT_HEADER))
	{
		fr_process_add_arg (command->process, "-mhe=on");
	}

	/* fr_process_add_arg (command->process, "-ms=off"); FIXME: solid mode off? */

	switch (archive->compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (command->process, "-mx=1");
		break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (command->process, "-mx=5");
		break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (command->process, "-mx=7");
		break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (command->process, "-mx=9");
		if (! _g_mime_type_matches (archive->mime_type, "application/zip")
		    && ! _g_mime_type_matches (archive->mime_type, "application/x-cbz"))
		{
			fr_process_add_arg (command->process, "-m0=lzma2");;
		}
		break;
	}

	if (_g_mime_type_matches (archive->mime_type, "application/x-ms-dos-executable"))
		fr_process_add_arg (command->process, "-sfx");

	if (archive->volume_size > 0)
		fr_process_add_arg_printf (command->process, "-v%ub", archive->volume_size);

	if (from_file != NULL)
		fr_process_add_arg_concat (command->process, "-i@", from_file, NULL);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Files prefixed with '@' need to be handled specially */
			if (g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg_concat (command->process, "-i!", scan->data, NULL);

	fr_process_add_arg (command->process, "--");
	fr_process_add_arg (command->process, command->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Skip files prefixed with '@', already added */
			if (!g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg (command->process, scan->data);

	fr_process_end_command (command->process);
}


static void
fr_command_7z_delete (FrCommand  *command,
		      const char *from_file,
		      GList      *file_list)
{
	FrArchive *archive = FR_ARCHIVE (command);
	GList     *scan;

	fr_command_7z_begin_command (command);
	fr_process_add_arg (command->process, "d");
	fr_process_add_arg (command->process, "-bd");
	fr_process_add_arg (command->process, "-y");
	if (_g_mime_type_matches (FR_ARCHIVE (command)->mime_type, "application/x-ms-dos-executable"))
		fr_process_add_arg (command->process, "-sfx");

	if (_g_mime_type_matches (archive->mime_type, "application/zip")
	    || _g_mime_type_matches (archive->mime_type, "application/x-cbz"))
	{
		fr_process_add_arg (command->process, "-tzip");
	}

	if (from_file != NULL)
		fr_process_add_arg_concat (command->process, "-i@", from_file, NULL);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Files prefixed with '@' need to be handled specially */
			if (g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg_concat (command->process, "-i!", scan->data, NULL);

	add_password_arg (command, FR_ARCHIVE (command)->password, FALSE);

	fr_process_add_arg (command->process, "--");
	fr_process_add_arg (command->process, command->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Skip files prefixed with '@', already added */
			if (!g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg (command->process, scan->data);

	fr_process_end_command (command->process);
}


static void
process_line__extract (char     *line,
		       gpointer  data)
{
	FrArchive *archive = FR_ARCHIVE (data);

	if (fr_archive_progress_get_total_files (archive) > 0)
		parse_progress_line (archive, "- ", _("Extracting “%s”"), line);
}


static void
fr_command_7z_extract (FrCommand  *command,
		       const char *from_file,
		       GList      *file_list,
		       const char *dest_dir,
		       gboolean    overwrite,
		       gboolean    skip_older,
		       gboolean    junk_paths)
{
	FrArchive *archive = FR_ARCHIVE (command);
	GList     *scan;

	fr_process_use_standard_locale (command->process, TRUE);
	fr_process_set_out_line_func (command->process,
				      process_line__extract,
				      command);
	fr_command_7z_begin_command (command);

	if (junk_paths)
		fr_process_add_arg (command->process, "e");
	else
		fr_process_add_arg (command->process, "x");

	fr_process_add_arg (command->process, "-bd");
	fr_process_add_arg (command->process, "-bb1");
	fr_process_add_arg (command->process, "-y");
	add_password_arg (command, archive->password, FALSE);

	if (dest_dir != NULL)
		fr_process_add_arg_concat (command->process, "-o", dest_dir, NULL);

	if (from_file != NULL)
		fr_process_add_arg_concat (command->process, "-i@", from_file, NULL);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Files prefixed with '@' need to be handled specially */
			if (g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg_concat (command->process, "-i!", scan->data, NULL);


	fr_process_add_arg (command->process, "--");
	fr_process_add_arg (command->process, command->filename);

	if (from_file == NULL)
		for (scan = file_list; scan; scan = scan->next)
			/* Skip files prefixed with '@', already added */
			if (!g_str_has_prefix (scan->data, "@"))
				fr_process_add_arg (command->process, scan->data);

	fr_process_end_command (command->process);
}


static void
fr_command_7z_test (FrCommand *command)
{
	FrArchive *archive = FR_ARCHIVE (command);

	fr_command_7z_begin_command (command);
	fr_process_add_arg (command->process, "t");
	fr_process_add_arg (command->process, "-bd");
	fr_process_add_arg (command->process, "-y");
	add_password_arg (command, archive->password, FALSE);
	fr_process_add_arg (command->process, "--");
	fr_process_add_arg (command->process, command->filename);
	fr_process_end_command (command->process);
}


static void
fr_command_7z_handle_error (FrCommand *command,
			    FrError   *error)
{
	FrArchive *archive = FR_ARCHIVE (command);

	if (error->type == FR_ERROR_NONE) {
		FrFileData *first;
		char     *basename;
		char     *testname;

		/* This is a way to fix bug #582712. */

		if (archive->files->len != 1)
			return;

		if (! g_str_has_suffix (command->filename, ".001"))
			return;

		first = g_ptr_array_index (archive->files, 0);
		basename = g_path_get_basename (command->filename);
		testname = g_strconcat (first->original_path, ".001", NULL);

		if (strcmp (basename, testname) == 0)
			fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));

		g_free (testname);
		g_free (basename);

		return;
	}

	if (error->status <= 1) {
		/* ignore warnings */
		fr_error_clear_gerror (error);
	}
	if (error->status == 255) {
		fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
	}
	else {
		GList *scan;

		for (scan = g_list_last (command->process->out.raw); scan; scan = scan->prev) {
			char *line = scan->data;

			if ((strstr (line, "Wrong password?") != NULL)
			    || (strstr (line, "Enter password") != NULL))
			{
				fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
				return;
			}
		}

		for (scan = g_list_last (command->process->err.raw); scan; scan = scan->prev) {
			char *line = scan->data;

			if ((strstr (line, "Wrong password?") != NULL)
			    || (strstr (line, "Enter password") != NULL))
			{
				fr_error_take_gerror (error, g_error_new_literal (FR_ERROR, FR_ERROR_ASK_PASSWORD, ""));
				return;
			}
		}
	}
}


const char *sevenz_mime_types[] = {
	"application/epub+zip",
	"application/x-7z-compressed",
	"application/x-apple-diskimage",
	"application/x-arj",
	"application/vnd.ms-cab-compressed",
	"application/vnd.rar",
	"application/x-cd-image",
	"application/x-chrome-extension",
	/*"application/x-cbr",*/
	"application/x-cbz",
	"application/x-ms-dos-executable",
	"application/x-ms-wim",
	"application/x-rar",
	"application/zip",
	NULL
};


static const char **
fr_command_7z_get_mime_types (FrArchive *archive)
{
	return sevenz_mime_types;
}


static gboolean
check_info_subcommand_for_codec_support (char *program_name, char *codec_name) {
	if (! _g_program_is_in_path (program_name)) {
		return FALSE;
	}

	g_autofree gchar *standard_output = NULL;

	gchar* argv[] = {
		program_name,
		"i",
		NULL
	};
	if (!g_spawn_sync (
		/* working_directory = */ NULL,
		argv,
		/* envp = */ NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
		/* child_setup = */ NULL,
		/* user_data = */ NULL,
		&standard_output,
		/* standard_error = */ NULL,
		/* wait_status = */ NULL,
		/* error = */ NULL
	)) {
		return FALSE;
	}

	gchar *codecs = g_strrstr (standard_output, "Codecs:");

	if (codecs == NULL) {
		return FALSE;
	}

	gchar *codec_found = g_strrstr (codecs, codec_name);

	return codec_found != NULL;
}


static gboolean
has_rar_support (gboolean check_command)
{
	/*
	 * Some 7-Zip distributions store RAR codec as a separate shared library and we can detect that.
	 * Most commonly, however, the programs link the codec statically so the only way to find out is to query them.
	 */
	return !check_command
	       || g_file_test ("/usr/lib/p7zip/Codecs/Rar.so", G_FILE_TEST_EXISTS)
	       || check_info_subcommand_for_codec_support ("7zzs", "Rar3")
	       || check_info_subcommand_for_codec_support ("7zz", "Rar3");
}


static FrArchiveCaps
fr_command_7z_get_capabilities (FrArchive  *archive,
				const char *mime_type,
				gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	/*
	 * We support two sets of program names:
	 * - 7z/7za/7zr from the no longer maintained p7zip project
	 * - 7zz/7zzs from the 7-Zip project by the original author
	 * Their CLI is mostly compatible.
	 */

	// Support full range of formats (except possibly rar).
	gboolean available_7zip = _g_program_is_available ("7zzs", check_command) || _g_program_is_available ("7zz", check_command);
	gboolean available_p7zip_full = _g_program_is_available ("7z", check_command);
	gboolean available_formats_full = available_7zip || available_p7zip_full;
	// Supports 7z, xz, lzma, zip, bzip2, gzip, tar, cab, ppmd and split.
	gboolean available_p7zip_partial = _g_program_is_available ("7za", check_command);
	// Supports 7z, xz, lzma and split.
	gboolean available_p7zip_reduced = _g_program_is_available ("7zr", check_command);

	if (! available_7zip
	    && ! available_p7zip_full
	    && ! available_p7zip_partial
	    && ! available_p7zip_reduced)
		return capabilities;

	if (_g_mime_type_matches (mime_type, "application/x-7z-compressed")) {
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE | FR_ARCHIVE_CAN_CREATE_VOLUMES;
		if (available_formats_full)
			capabilities |= FR_ARCHIVE_CAN_ENCRYPT | FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-7z-compressed-tar")) {
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
		if (available_formats_full)
			capabilities |= FR_ARCHIVE_CAN_ENCRYPT | FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	}
	else if (available_formats_full) {
		if (_g_mime_type_matches (mime_type, "application/x-rar")
		    || _g_mime_type_matches (mime_type, "application/vnd.rar")
		    || _g_mime_type_matches (mime_type, "application/x-cbr"))
		{
			/* give priority to rar and unrar that supports RAR files better. */
			if (!_g_program_is_available ("rar", TRUE)
			    && !_g_program_is_available ("unrar", TRUE)
			    && has_rar_support (check_command))
				capabilities |= FR_ARCHIVE_CAN_READ;
		}
		else
			capabilities |= FR_ARCHIVE_CAN_READ;

		if (_g_mime_type_matches (mime_type, "application/epub+zip")
		    || _g_mime_type_matches (mime_type, "application/x-cbz")
		    || _g_mime_type_matches (mime_type, "application/x-ms-dos-executable")
		    || _g_mime_type_matches (mime_type, "application/zip"))
		{
			capabilities |= FR_ARCHIVE_CAN_WRITE | FR_ARCHIVE_CAN_ENCRYPT;
		}
	}
	else if (available_p7zip_partial) {
		if (_g_mime_type_matches (mime_type, "application/vnd.ms-cab-compressed")
		    || _g_mime_type_matches (mime_type, "application/zip"))
		{
			capabilities |= FR_ARCHIVE_CAN_READ;
		}

		if (_g_mime_type_matches (mime_type, "application/zip"))
			capabilities |= FR_ARCHIVE_CAN_WRITE;
	}

	/* multi-volumes are read-only */
	if ((archive->files->len > 0) && archive->multi_volume && (capabilities & FR_ARCHIVE_CAN_WRITE))
		capabilities ^= FR_ARCHIVE_CAN_WRITE;

	return capabilities;
}


static const char *
fr_command_7z_get_packages (FrArchive  *archive,
			    const char *mime_type)
{
	if (_g_mime_type_matches (mime_type, "application/vnd.rar") || _g_mime_type_matches (mime_type, "application/x-rar"))
		return FR_PACKAGES ("7zip,7zip-rar");
	else if (_g_mime_type_matches (mime_type, "application/zip") || _g_mime_type_matches (mime_type, "application/vnd.ms-cab-compressed"))
		return FR_PACKAGES ("7zip,7zip-full");
	else
		return FR_PACKAGES ("7zip");
}


static void
fr_command_7z_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_7Z (object));

	/* Chain up */
	if (G_OBJECT_CLASS (fr_command_7z_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_7z_parent_class)->finalize (object);
}


static void
fr_command_7z_class_init (FrCommand7zClass *class)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_7z_parent_class = g_type_class_peek_parent (class);

	gobject_class = G_OBJECT_CLASS (class);
	gobject_class->finalize = fr_command_7z_finalize;

	archive_class = FR_ARCHIVE_CLASS (class);
	archive_class->get_mime_types   = fr_command_7z_get_mime_types;
	archive_class->get_capabilities = fr_command_7z_get_capabilities;
	archive_class->get_packages     = fr_command_7z_get_packages;

	command_class = FR_COMMAND_CLASS (class);
	command_class->list             = fr_command_7z_list;
	command_class->add              = fr_command_7z_add;
	command_class->delete           = fr_command_7z_delete;
	command_class->extract          = fr_command_7z_extract;
	command_class->test             = fr_command_7z_test;
	command_class->handle_error     = fr_command_7z_handle_error;
}


static void
fr_command_7z_init (FrCommand7z *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreFolders       = TRUE;
	base->propAddCanStoreLinks         = TRUE;
	base->propAddCanFollowDirectoryLinksWithoutDereferencing = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = TRUE;
	base->propPassword                 = TRUE;
	base->propTest                     = TRUE;
	base->propListFromFile             = TRUE;
}
