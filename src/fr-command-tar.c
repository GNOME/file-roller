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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
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
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-tar.h"

#define ACTIVITY_DELAY 20

static void fr_command_tar_class_init  (FRCommandTarClass *class);
static void fr_command_tar_init        (FRCommand         *afile);
static void fr_command_tar_finalize    (GObject           *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


/* -- list -- */

static time_t
mktime_from_string (char *date_s, 
		    char *time_s)
{
	struct tm   tm = {0, };
	char      **fields;

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
	FileData    *fdata;
	FRCommand   *comm = FR_COMMAND (data);
	char       **fields;
	int          date_idx;
	char        *field_date, *field_time, *field_size, *field_name;
	char        *name;

	g_return_if_fail (line != NULL);

	fdata = file_data_new ();

	date_idx = file_list__get_index_from_pattern (line, "%n%n%n%n-%n%n-%n%n %n%n:%n%n");

	field_size = file_list__get_prev_field (line, date_idx, 1);
	fdata->size = g_ascii_strtoull (field_size, NULL, 10);
	g_free (field_size);

	field_date = file_list__get_next_field (line, date_idx, 1);
	field_time = file_list__get_next_field (line, date_idx, 2);
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

	name = unescape_str (fields[0]);
	if (*name == '/') {
		fdata->full_path = g_strdup (name);
		fdata->original_path = fdata->full_path;
	} else {
		fdata->full_path = g_strconcat ("/", name, NULL);
		fdata->original_path = fdata->full_path + 1;
	}
	g_free (name);

	if (fields[1] != NULL)
		fdata->link = g_strdup (fields[1]);
	g_strfreev (fields);
	g_free (field_name);

	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else 
		comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
add_compress_arg (FRCommand *comm)
{
	FRCommandTar *comm_tar = FR_COMMAND_TAR (comm);

	switch (comm_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		fr_process_add_arg (comm->process, "-z");
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		fr_process_add_arg (comm->process, 
				    "--use-compress-program=bzip");
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		fr_process_add_arg (comm->process, "--use-compress-program=bzip2");
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS:
		fr_process_add_arg (comm->process, "-Z");
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		fr_process_add_arg (comm->process, 
				    "--use-compress-program=lzop");
		break;
	}
}


static void
begin_tar_command (FRCommand *comm)
{
	char *command = NULL;

	/* In solaris gtar is present under /usr/sfw/bin */

	command = g_find_program_in_path ("gtar");
#if defined (__SVR4) && defined (__sun)
	if (g_file_test ("/usr/sfw/bin/gtar", G_FILE_TEST_IS_EXECUTABLE)) {
		command = g_strdup ("/usr/sfw/bin/gtar");
	}
#endif
	if (command != NULL)
		fr_process_begin_command (comm->process, command);
	else
		fr_process_begin_command (comm->process, "tar");
	g_free (command);
}


static void
fr_command_tar_list (FRCommand  *comm,
		     const char *password)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line,
				      comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "-tvf");
	fr_process_add_arg (comm->process, comm->e_filename);
	add_compress_arg (comm);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
process_line__generic (char     *line, 
		       gpointer  data,
		       char     *action_msg)
{	
	FRCommand *comm = FR_COMMAND (data);
	char      *msg;

	if (line == NULL)
		return;

	if (line[strlen (line) - 1] == '/') /* ignore directories */
		return;

	comm->n_file++;

	msg = g_strconcat (action_msg, file_name_from_path (line), NULL);
	fr_command_message (comm, msg);
	g_free (msg);

	if (comm->n_files != 0) {
		double fraction = (double) comm->n_file / (comm->n_files+1);
		fr_command_progress (comm, fraction);
	}
}


static void
process_line__add (char     *line, 
		   gpointer  data)
{
	process_line__generic (line, data, _("Adding file: "));
}


static void
fr_command_tar_add (FRCommand     *comm,
		    GList         *file_list,
		    const char    *base_dir,
		    gboolean       update,
		    const char    *password,
		    FRCompression  compression)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	GList        *scan;

	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line__add,
				      comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-p");

	if (base_dir != NULL) {
		char *e_base_dir = shell_escape (base_dir);
		fr_process_add_arg (comm->process, "-C");
		fr_process_add_arg (comm->process, e_base_dir);
		g_free (e_base_dir);
	}

	if (update) 
		fr_process_add_arg (comm->process, "-uf");
	else
		fr_process_add_arg (comm->process, "-rf");

	fr_process_add_arg (comm->process, c_tar->uncomp_filename);
	for (scan = file_list; scan; scan = scan->next) 
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
process_line__delete (char     *line, 
		      gpointer  data)
{
	process_line__generic (line, data, _("Removing file: "));
}


static void
begin_func__delete (gpointer data)
{
	FRCommand *comm = data;
	fr_command_progress (comm, -1.0);
	fr_command_message (comm, _("Deleting files from archive"));
}


static void
fr_command_tar_delete (FRCommand *comm,
		       GList     *file_list)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	GList        *scan;

	fr_process_set_out_line_func (comm->process, 
				      process_line__delete,
				      comm);

	begin_tar_command (comm);
	fr_process_set_begin_func (comm->process, begin_func__delete, comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "--delete");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, c_tar->uncomp_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
}


static void
process_line__extract (char     *line, 
		       gpointer  data)
{
	process_line__generic (line, data, _("Extracting file: "));
}


static void
fr_command_tar_extract (FRCommand  *comm,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths,
			const char *password)
{
	GList *scan;

	fr_process_set_out_line_func (comm->process, 
				      process_line__extract,
				      comm);

	begin_tar_command (comm);
	fr_process_add_arg (comm->process, "--force-local");
	fr_process_add_arg (comm->process, "-v");
	fr_process_add_arg (comm->process, "-p");
	fr_process_add_arg (comm->process, "-xf");
	fr_process_add_arg (comm->process, comm->e_filename);
	add_compress_arg (comm);

	if (dest_dir != NULL) {
		char *e_dest_dir = shell_escape (dest_dir);
		fr_process_add_arg (comm->process, "-C");
		fr_process_add_arg (comm->process, e_dest_dir);
		g_free (e_dest_dir);
	}

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
begin_func__recompress (gpointer data)
{
	FRCommand *comm = data;
	fr_command_progress (comm, -1.0);
	fr_command_message (comm, _("Recompressing archive"));
}


static void
fr_command_tar_recompress (FRCommand     *comm,
			   FRCompression  compression)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	char         *new_name = NULL;

	switch (c_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		fr_process_begin_command (comm->process, "gzip");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (compression) {
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
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		fr_process_begin_command (comm->process, "bzip");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (compression) {
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

		new_name = g_strconcat (c_tar->uncomp_filename, ".bz", NULL);
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		fr_process_begin_command (comm->process, "bzip2");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (compression) {
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
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS: 
		fr_process_begin_command (comm->process, "compress");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".Z", NULL);
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_set_begin_func (comm->process, begin_func__recompress, comm);
		switch (compression) {
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
		break;
	}

	if (c_tar->name_modified) {
		/* Restore original name. */
		
		fr_process_begin_command (comm->process, "mv");
		fr_process_set_sticky (comm->process, TRUE);
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, new_name);
		fr_process_add_arg (comm->process, comm->e_filename);
		fr_process_end_command (comm->process);
	}

	g_free (new_name);
	g_free (c_tar->uncomp_filename);
	c_tar->uncomp_filename = NULL;
}


static void
begin_func__uncompress (gpointer data)
{
	FRCommand *comm = data;
	fr_command_progress (comm, -1.0);
	fr_command_message (comm, _("Decompressing archive"));
}


static char *
get_uncompressed_name (FRCommandTar *c_tar,
		       const char   *e_filename) 
{
	char *new_name = g_strdup (e_filename);
	int   l = strlen (new_name);
 
	switch (c_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		/* X.tgz     -->  X.tar 
		 * X.tar.gz  -->  X.tar */
		if (file_extension_is (e_filename, ".tgz")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (e_filename, ".tar.gz")) 
			new_name[l - 3] = 0;
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		/* X.tbz     -->  X.tar 
		 * X.tar.bz  -->  X.tar */
		if (file_extension_is (e_filename, ".tbz")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (e_filename, ".tar.bz")) 
			new_name[l - 3] = 0;
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		/* X.tbz2    -->  X.tar 
		 * X.tar.bz2 -->  X.tar */
		if (file_extension_is (e_filename, ".tbz2")) {
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		} else if (file_extension_is (e_filename, ".tar.bz2")) 
			new_name[l - 4] = 0;
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS: 
		/* X.taz   -->  X.tar 
		 * X.tar.Z -->  X.tar */
		if (file_extension_is (e_filename, ".taz")) 
			new_name[l - 1] = 'r';
		else if (file_extension_is (e_filename, ".tar.Z")) 
			new_name[l - 2] = 0;
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		/* X.tzo     -->  X.tar 
		 * X.tar.lzo -->  X.tar */
		if (file_extension_is (e_filename, ".tzo")) {
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (e_filename, ".tar.lzo")) 
			new_name[l - 4] = 0;
		break;
	}
	
	return new_name;
}


#define MAX_TRIES 50


static char *
get_temp_name (FRCommandTar *c_tar,
	       const char   *filepath) 
{
	char       *dirname = remove_level_from_path (filepath);
	const char *filename = file_name_from_path (filepath);
	char       *temp_name = NULL;
	char       *e_temp_name = NULL;
	char       *uncomp_temp_name = NULL;
	static int  count = 0;
	int         try = 0;

	do {
		char *tmp_file_name;
		g_free (temp_name);
		g_free (uncomp_temp_name);
		tmp_file_name = g_strdup_printf ("fr.%d.%d.%s", 
						 getpid (), 
						 count++, 
						 filename);
		temp_name = g_build_filename (dirname, tmp_file_name, NULL);
		g_free (tmp_file_name);
		uncomp_temp_name = get_uncompressed_name (c_tar, temp_name);
	} while ((path_is_file (temp_name)
		  || path_is_file (uncomp_temp_name))
		 && (try++ < MAX_TRIES));

	g_free (uncomp_temp_name);
	g_free (dirname);

	e_temp_name = shell_escape (temp_name);
	g_free (temp_name);

	return e_temp_name;
}


static void
fr_command_tar_uncompress (FRCommand *comm)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	char         *tmp_name;

	if (c_tar->uncomp_filename != NULL) {
		g_free (c_tar->uncomp_filename);
		c_tar->uncomp_filename = NULL;
	}

	c_tar->name_modified = c_tar->compress_prog != FR_COMPRESS_PROGRAM_NONE;
	if (c_tar->name_modified) {
		tmp_name = get_temp_name (c_tar, comm->filename);
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "mv");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
	} else
		tmp_name = g_strdup (comm->e_filename);

	switch (c_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "gzip");
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "bzip");
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "bzip2");
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS: 
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "uncompress");
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "lzop");
			fr_process_set_begin_func (comm->process, begin_func__uncompress, comm);
			fr_process_add_arg (comm->process, "-dfU");
			fr_process_add_arg (comm->process, "--no-stdin");
			fr_process_add_arg (comm->process, tmp_name);
			fr_process_end_command (comm->process);
		}
		break;
	}

	c_tar->uncomp_filename = get_uncompressed_name (c_tar, tmp_name);
	g_free (tmp_name);
}


static char *
fr_command_tar_escape (FRCommand     *comm,
		       const char    *str)
{
	char *estr;
	char *estr2;
	char *estr3;

        estr = escape_str (str, "\\");
        estr2 = escape_str (estr, "\\$?*'& !|()@#:;<>");
        estr3 = escape_str_common (estr2, "[]", '[', ']');

	g_free (estr);
	g_free (estr2);

	return estr3;
}


static void 
fr_command_tar_class_init (FRCommandTarClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

	gobject_class->finalize = fr_command_tar_finalize;

        afc->list         = fr_command_tar_list;
	afc->add          = fr_command_tar_add;
	afc->delete       = fr_command_tar_delete;
	afc->extract      = fr_command_tar_extract;

	afc->recompress   = fr_command_tar_recompress;
	afc->uncompress   = fr_command_tar_uncompress;

	afc->escape       = fr_command_tar_escape;
}

 
static void 
fr_command_tar_init (FRCommand *comm)
{
	FRCommandTar *comm_tar = (FRCommandTar*) comm;

	comm->file_type = FR_FILE_TYPE_TAR;

	comm->propCanModify                = TRUE;
	comm->propAddCanUpdate             = TRUE;
	comm->propAddCanReplace            = FALSE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;

	comm_tar->msg = NULL;
}


static void 
fr_command_tar_finalize (GObject *object)
{
	FRCommandTar *comm_tar;

        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_TAR (object));

	comm_tar = FR_COMMAND_TAR (object);

	if (comm_tar->uncomp_filename != NULL) {
		g_free (comm_tar->uncomp_filename);
		comm_tar->uncomp_filename = NULL;
	}

	if (comm_tar->msg != NULL) {
		g_free (comm_tar->msg);
		comm_tar->msg = NULL;
	}

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_tar_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandTarClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_tar_class_init,
			NULL,
			NULL,
			sizeof (FRCommandTar),
			0,
			(GInstanceInitFunc) fr_command_tar_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandTar",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_tar_new (FRProcess         *process,
		    const char        *filename,
		    FRCompressProgram  prog)
{
	FRCommand *comm;

	/* In solaris gtar is present under /usr/sfw/bin */

	if ((!is_program_in_path("/usr/sfw/bin/gtar")) &&
	    (!is_program_in_path("tar"))) {
		return NULL;
	}

	if ((prog == FR_COMPRESS_PROGRAM_GZIP) &&
            (!is_program_in_path("gzip"))) {
                return NULL;
        }

        if ((prog == FR_COMPRESS_PROGRAM_BZIP) &&
            (!is_program_in_path("bzip"))) {
                return NULL;
        }

        if ((prog == FR_COMPRESS_PROGRAM_BZIP2) &&
            (!is_program_in_path("bzip2"))) {
                return NULL;
        }

        if ((prog == FR_COMPRESS_PROGRAM_COMPRESS) &&
            ((!is_program_in_path("compress")) ||
             (!is_program_in_path("uncompress")))) {
                return NULL;
        }

        if ((prog == FR_COMPRESS_PROGRAM_LZOP) &&
            (!is_program_in_path("lzop"))) {
                return NULL;
        }

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_TAR, NULL));
	fr_command_construct (comm, process, filename);
	FR_COMMAND_TAR (comm)->compress_prog = prog;
	FR_COMMAND_TAR (comm)->uncomp_filename = NULL;

	return comm;
}
