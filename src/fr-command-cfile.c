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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-cfile.h"


struct _FrCommandCFile
{
	FrCommand  parent_instance;

	FrError    error;
};


G_DEFINE_TYPE (FrCommandCFile, fr_command_cfile, fr_command_get_type ())


static char *
get_uncompressed_name_from_archive (FrCommand  *comm,
				    const char *archive)
{
	GFile        *file;
	GInputStream *stream;
	char         *filename = NULL;

	if (! _g_mime_type_matches (FR_ARCHIVE (comm)->mime_type, "application/x-gzip"))
		return NULL;

	file = g_file_new_for_path (archive);

	stream = (GInputStream *) g_file_read (file, NULL, NULL);
	if (stream != NULL) {
		gboolean filename_present = TRUE;
		char     buffer[10];

		if (g_input_stream_read (stream, buffer, 10, NULL, NULL) >= 0) {
			/* Check whether the FLG.FNAME is set */
			if (((unsigned char)(buffer[3]) & 0x08) != 0x08)
				filename_present = FALSE;

			/* Check whether the FLG.FEXTRA is set */
			if (((unsigned char)(buffer[3]) & 0x04) == 0x04)
				filename_present = FALSE;
		}

		if (filename_present) {
			GString *str = NULL;

			str = g_string_new ("");
			while (g_input_stream_read (stream, buffer, 1, NULL, NULL) > 0) {
				if (buffer[0] == '\0') {
					filename = g_strdup (_g_path_get_basename (str->str));
#ifdef DEBUG
					g_message ("filename is: %s", filename);
#endif
					break;
				}
				g_string_append_c (str, buffer[0]);
			}
			g_string_free (str, TRUE);
		}
		g_object_unref (stream);
	}
	g_object_unref (file);

	return filename;
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrCommand  *comm = FR_COMMAND (data);
	FrFileData *fdata;
	char      **fields;
	GFile      *file;
	char       *filename;

	fdata = fr_file_data_new ();

	fields = _g_str_split_line (line, 2);
	if (strcmp (fields[1], "-1") != 0)
		fdata->size = g_ascii_strtoull (fields[1], NULL, 10);
	g_strfreev (fields);

	file = g_file_new_for_path (comm->filename);

	if (fdata->size == 0)
		fdata->size = _g_file_get_file_size (file);

	filename = get_uncompressed_name_from_archive (comm, comm->filename);
	if (filename == NULL)
		filename = _g_path_remove_first_extension (comm->filename);

	fdata->full_path = g_strconcat ("/", _g_path_get_basename (filename), NULL);
	g_free (filename);

	fdata->original_path = fdata->full_path + 1;
	fdata->link = NULL;
	fdata->modified = _g_file_get_file_mtime (file);

	fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
	fdata->path = _g_path_remove_level (fdata->full_path);

	if (*fdata->name == 0)
		fr_file_data_free (fdata);
	else
		fr_archive_add_file (FR_ARCHIVE (comm), fdata);

	g_object_unref (file);
}


static gboolean
fr_command_cfile_list (FrCommand *comm)
{
	if (_g_mime_type_matches (FR_ARCHIVE (comm)->mime_type, "application/x-gzip")) {
		/* gzip let us known the uncompressed size */

		fr_process_set_out_line_func (comm->process,
					      list__process_line,
					      comm);

		fr_process_begin_command (comm->process, "gzip");
		fr_process_add_arg (comm->process, "-l");
		fr_process_add_arg (comm->process, "-q");
		fr_process_add_arg (comm->process, comm->filename);
		fr_process_end_command (comm->process);
	}
	else {
		/* ... other compressors do not support this feature so
		 * simply use the archive size, suboptimal but there is no
		 * alternative. */

		FrFileData *fdata;
		char     *filename;
		GFile    *file;

		fdata = fr_file_data_new ();

		filename = _g_path_remove_first_extension (comm->filename);
		fdata->full_path = g_strconcat ("/",
						_g_path_get_basename (filename),
						NULL);
		g_free (filename);

		file = g_file_new_for_path (comm->filename);

		fdata->original_path = fdata->full_path + 1;
		fdata->link = NULL;
		fdata->size = _g_file_get_file_size (file);
		fdata->modified = _g_file_get_file_mtime (file);
		fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
		fdata->path = _g_path_remove_level (fdata->full_path);

		if (*fdata->name == 0)
			fr_file_data_free (fdata);
		else
			fr_archive_add_file (FR_ARCHIVE (comm), fdata);

		g_object_unref (file);

		return FALSE;
	}

	return TRUE;
}


static void
fr_command_cfile_add (FrCommand  *comm,
		      const char *from_file,
		      GList      *file_list,
		      const char *base_dir,
		      gboolean    update,
		      gboolean    follow_links)
{
	FrArchive  *archive = FR_ARCHIVE (comm);
	const char *filename;
	char       *temp_dir;
	char       *temp_file;
	char       *compressed_filename;

	if ((file_list == NULL) || (file_list->data == NULL))
		return;

	/* copy file to the temp dir */

	temp_dir = _g_path_get_temp_work_dir (NULL);
	filename = file_list->data;
	temp_file = g_strconcat (temp_dir, "/", filename, NULL);

	fr_process_begin_command (comm->process, "cp");
	fr_process_set_working_dir (comm->process, base_dir);
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, filename);
	fr_process_add_arg (comm->process, temp_file);
	fr_process_end_command (comm->process);

	/**/

	if (_g_mime_type_matches (archive->mime_type, "application/x-gzip")) {
		fr_process_begin_command (comm->process, "gzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".gz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli")) {
		fr_process_begin_command (comm->process, "brotli");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".br", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip")) {
		fr_process_begin_command (comm->process, "bzip2");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".bz2", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3")) {
		fr_process_begin_command (comm->process, "bzip3");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".bz3", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-compress")) {
		fr_process_begin_command (comm->process, "compress");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".Z", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip")) {
		fr_process_begin_command (comm->process, "lzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".lz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma")) {
		fr_process_begin_command (comm->process, "lzma");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".lzma", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-xz")) {
		fr_process_begin_command (comm->process, "xz");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".xz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzop")) {
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-fU");
		fr_process_add_arg (comm->process, "--no-stdin");
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".lzo", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-rzip")) {
		fr_process_begin_command (comm->process, "rzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".rz", NULL);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4")) {
		compressed_filename = g_strconcat (filename, ".lz4", NULL);
		fr_process_begin_command (comm->process, "lz4");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-z");
		fr_process_add_arg (comm->process, filename);
		fr_process_add_arg (comm->process, compressed_filename);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/zstd")) {
		fr_process_begin_command (comm->process, "zstd");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "--");
		fr_process_add_arg (comm->process, filename);
		fr_process_end_command (comm->process);
		compressed_filename = g_strconcat (filename, ".zst", NULL);
	}
	else {
		g_warning ("Unhandled mime type: '%s'", archive->mime_type);
		g_warn_if_reached ();
		g_free (temp_file);
		g_free (temp_dir);
		return;
	}

      	/* copy compressed file to the dest dir */

	fr_process_begin_command (comm->process, "cp");
	fr_process_set_working_dir (comm->process, temp_dir);
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, compressed_filename);
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	/* remove the temp dir */

	fr_process_begin_command (comm->process, "rm");
	fr_process_set_sticky (comm->process, TRUE);
	fr_process_add_arg (comm->process, "-rf");
	fr_process_add_arg (comm->process, "--");
	fr_process_add_arg (comm->process, temp_dir);
	fr_process_end_command (comm->process);

	g_free (compressed_filename);
	g_free (temp_file);
	g_free (temp_dir);
}


static void
fr_command_cfile_delete (FrCommand  *comm,
			 const char *from_file,
			 GList      *file_list)
{
	/* never called */
}


static void
fr_command_cfile_extract (FrCommand  *comm,
			  const char *from_file,
			  GList      *file_list,
			  const char *dest_dir,
			  gboolean    overwrite,
			  gboolean    skip_older,
			  gboolean    junk_paths)
{
	FrArchive *archive = FR_ARCHIVE (comm);
	char      *temp_dir;
	char      *dest_file;
	char      *temp_file;
	char      *uncompr_file;
	char      *compr_file;

	/* copy file to the temp dir, remove the already existing file first */

	temp_dir = _g_path_get_temp_work_dir (NULL);
	temp_file = g_strconcat (temp_dir,
				 "/",
				 _g_path_get_basename (comm->filename),
				 NULL);

	fr_process_begin_command (comm->process, "cp");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, temp_file);
	fr_process_end_command (comm->process);

	/* uncompress the file */

	uncompr_file = _g_path_remove_first_extension (temp_file);

	if (_g_mime_type_matches (archive->mime_type, "application/x-gzip")) {
		fr_process_begin_command (comm->process, "gzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, "-n");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-brotli")) {
		fr_process_begin_command (comm->process, "brotli");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip")) {
		fr_process_begin_command (comm->process, "bzip2");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-bzip3")) {
		fr_process_begin_command (comm->process, "bzip3");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-compress")) {
		if (_g_program_is_in_path ("gzip")) {
			fr_process_begin_command (comm->process, "gzip");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, "-n");
		}
		else
			fr_process_begin_command (comm->process, "uncompress");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzip")) {
		fr_process_begin_command (comm->process, "lzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzma")) {
		fr_process_begin_command (comm->process, "lzma");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-xz")) {
		fr_process_begin_command (comm->process, "xz");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lzop")) {
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, "-fU");
		fr_process_add_arg (comm->process, "--no-stdin");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-rzip")) {
		fr_process_begin_command (comm->process, "rzip");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/x-lz4")) {
		fr_process_begin_command (comm->process, "lz4");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_add_arg (comm->process, uncompr_file);
		fr_process_end_command (comm->process);
	}
	else if (_g_mime_type_matches (archive->mime_type, "application/zstd")) {
		fr_process_begin_command (comm->process, "zstd");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
	}

	/* copy uncompress file to the dest dir */

	compr_file = get_uncompressed_name_from_archive (comm, comm->filename);
	if (compr_file == NULL)
		compr_file = _g_path_remove_first_extension (_g_path_get_basename (comm->filename));
	dest_file = g_strconcat (dest_dir,
				 "/",
				 compr_file,
				 NULL);

	fr_process_begin_command (comm->process, "cp");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, uncompr_file);
	fr_process_add_arg (comm->process, dest_file);
	fr_process_end_command (comm->process);

	/* remove the temp dir */

	fr_process_begin_command (comm->process, "rm");
	fr_process_set_sticky (comm->process, TRUE);
	fr_process_add_arg (comm->process, "-rf");
	fr_process_add_arg (comm->process, temp_dir);
	fr_process_end_command (comm->process);

	g_free (dest_file);
	g_free (compr_file);
	g_free (uncompr_file);
	g_free (temp_file);
	g_free (temp_dir);
}


const char *cfile_mime_type[] = { "application/x-gzip",
				  "application/x-brotli",
				  "application/x-bzip",
				  "application/x-bzip3",
				  "application/x-compress",
				  "application/x-lz4",
				  "application/x-lzip",
				  "application/x-lzma",
				  "application/x-lzop",
				  "application/x-rzip",
				  "application/x-xz",
				  "application/zstd",
				  NULL };


static const char **
fr_command_cfile_get_mime_types (FrArchive *archive)
{
	return cfile_mime_type;
}


static FrArchiveCaps
fr_command_cfile_get_capabilities (FrArchive  *archive,
			           const char *mime_type,
				   gboolean    check_command)
{
	FrArchiveCaps capabilities;

	capabilities = FR_ARCHIVE_CAN_DO_NOTHING;
	if (_g_mime_type_matches (mime_type, "application/x-gzip")) {
		if (_g_program_is_available ("gzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-brotli")) {
		if (_g_program_is_available ("brotli", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-bzip")) {
		if (_g_program_is_available ("bzip2", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-bzip3")) {
		if (_g_program_is_available ("bzip3", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-compress")) {
		if (_g_program_is_available ("compress", check_command))
			capabilities |= FR_ARCHIVE_CAN_WRITE;
		if (_g_program_is_available ("uncompress", check_command) || _g_program_is_available ("gzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lzip")) {
		if (_g_program_is_available ("lzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lzma")) {
		if (_g_program_is_available ("lzma", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-xz")) {
		if (_g_program_is_available ("xz", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lzop")) {
		if (_g_program_is_available ("lzop", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-rzip") ||
		 _g_mime_type_matches (mime_type, "application/x-rzip-compressed-tar")) {
		if (_g_program_is_available ("rzip", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/x-lz4")) {
		if (_g_program_is_available ("lz4", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	else if (_g_mime_type_matches (mime_type, "application/zstd")) {
		if (_g_program_is_available ("zstd", check_command))
			capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	}
	return capabilities;
}


static const char *
fr_command_cfile_get_packages (FrArchive  *archive,
			       const char *mime_type)
{
	if (_g_mime_type_matches (mime_type, "application/x-gzip"))
		return FR_PACKAGES ("gzip");
	else if (_g_mime_type_matches (mime_type, "application/x-brotli"))
		return FR_PACKAGES ("brotli");
	else if (_g_mime_type_matches (mime_type, "application/x-bzip"))
		return FR_PACKAGES ("bzip2");
	else if (_g_mime_type_matches (mime_type, "application/x-bzip3"))
		return FR_PACKAGES ("bzip3");
	else if (_g_mime_type_matches (mime_type, "application/x-compress"))
		return FR_PACKAGES ("ncompress");
	else if (_g_mime_type_matches (mime_type, "application/x-lzip"))
		return FR_PACKAGES ("lzip");
	else if (_g_mime_type_matches (mime_type, "application/x-lzma"))
		return FR_PACKAGES ("lzma");
	else if (_g_mime_type_matches (mime_type, "application/x-xz"))
		return FR_PACKAGES ("xz");
	else if (_g_mime_type_matches (mime_type, "application/x-lzop"))
		return FR_PACKAGES ("lzop");
	else if (_g_mime_type_matches (mime_type, "application/x-rzip"))
		return FR_PACKAGES ("rzip");
	else if (_g_mime_type_matches (mime_type, "application/x-lz4"))
		return FR_PACKAGES ("lz4");
	else if (_g_mime_type_matches (mime_type, "application/zstd"))
		return FR_PACKAGES ("zstd");

	return NULL;
}


static void
fr_command_cfile_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_CFILE (object));

	/* Chain up */
        if (G_OBJECT_CLASS (fr_command_cfile_parent_class)->finalize)
                G_OBJECT_CLASS (fr_command_cfile_parent_class)->finalize (object);
}


static void
fr_command_cfile_class_init (FrCommandCFileClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_cfile_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
        gobject_class->finalize = fr_command_cfile_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_cfile_get_mime_types;
	archive_class->get_capabilities = fr_command_cfile_get_capabilities;
	archive_class->get_packages     = fr_command_cfile_get_packages;

        command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_cfile_list;
	command_class->add              = fr_command_cfile_add;
	command_class->delete           = fr_command_cfile_delete;
	command_class->extract          = fr_command_cfile_extract;
}


static void
fr_command_cfile_init (FrCommandCFile *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = TRUE;
	base->propAddCanReplace            = TRUE;
	base->propAddCanStoreLinks         = FALSE;
	base->propExtractCanAvoidOverwrite = FALSE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
}
