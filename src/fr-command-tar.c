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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-tar.h"

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
		tm.tm_hour = atoi (fields[0]) - 1;
		if (fields[1] != NULL) {
			tm.tm_min  = atoi (fields[1]);
			if (fields[2] != NULL)
				tm.tm_sec  = atoi (fields[2]);
		}
	}
	g_strfreev (fields);

	return mktime (&tm);
}


static char *
eat_spaces (char *line)
{
	while ((*line == ' ') && (*line != 0))
		line++;
	return line;
}


static char **
split_line (char *line, 
	    int   n_fields)
{
	char **fields;
	char  *scan, *field_end;
	int    i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields + 1] = NULL;

	scan = eat_spaces (line);
	for (i = 0; i < n_fields; i++) {
		field_end = strchr (scan, ' ');
		fields[i] = g_strndup (scan, field_end - scan);
		scan = eat_spaces (field_end);
	}

	return fields;
}


static char *
get_last_field (char *line)
{
	int   i;
	char *field;
	int   n = 6;

	n--;
	field = eat_spaces (line);
	for (i = 0; i < n; i++) {
		field = strchr (field, ' ');
		field = eat_spaces (field);
	}

	return field;
}


static void
process_line (char     *line, 
	      gpointer  data)
{
	FileData   *fdata;
	FRCommand  *comm = FR_COMMAND (data);
	char      **fields;
	char       *name_field;

	g_return_if_fail (line != NULL);

	fdata = file_data_new ();

	fields = split_line (line, 5);
	fdata->size = atol (fields[2]);
	fdata->modified = mktime_from_string (fields[3], fields[4]);
	g_strfreev (fields);

	/* Full path */

	name_field = get_last_field (line);
	fields = g_strsplit (name_field, " -> ", 2);

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (name_field, " link to ", 2);
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
fr_command_tar_list (FRCommand *comm)
{
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
				      process_line,
				      comm);

	fr_process_clear (comm->process);
	fr_process_begin_command (comm->process, "tar");
	fr_process_add_arg (comm->process, "-tvf");
	fr_process_add_arg (comm->process, comm->e_filename);
	add_compress_arg (comm);
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
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

	/* Add files. */

	fr_process_begin_command (comm->process, "tar");

	if (base_dir != NULL) {
		gchar *e_base_dir = shell_escape (base_dir);
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
fr_command_tar_delete (FRCommand *comm,
		       GList     *file_list)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	GList        *scan;

	/* Delete files. */

	fr_process_begin_command (comm->process, "tar");
	fr_process_add_arg (comm->process, "--delete");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, c_tar->uncomp_filename);

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);
	fr_process_end_command (comm->process);
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

	fr_process_begin_command (comm->process, "tar");
	
	fr_process_add_arg (comm->process, "-xf");
	fr_process_add_arg (comm->process, comm->e_filename);
	add_compress_arg (comm);

	if (dest_dir != NULL) {
		gchar *e_dest_dir = shell_escape (dest_dir);
		fr_process_add_arg (comm->process, "-C");
		fr_process_add_arg (comm->process, e_dest_dir);
		g_free (e_dest_dir);
	}

	for (scan = file_list; scan; scan = scan->next)
		fr_process_add_arg (comm->process, scan->data);

	fr_process_end_command (comm->process);
}


static void
fr_command_tar_recompress (FRCommand     *comm,
			   FRCompression  compression)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	gchar        *new_name = NULL;

	switch (c_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		fr_process_begin_command (comm->process, "gzip");
		fr_process_set_sticky (comm->process, TRUE);
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
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, c_tar->uncomp_filename);
		fr_process_end_command (comm->process);

		new_name = g_strconcat (c_tar->uncomp_filename, ".Z", NULL);
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_sticky (comm->process, TRUE);
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

	/* Restore original name. */

	if (c_tar->name_modified) {
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
fr_command_tar_uncompress (FRCommand *comm)
{
	FRCommandTar *c_tar = FR_COMMAND_TAR (comm);
	gchar        *new_name;
	gint          l;

	if (c_tar->uncomp_filename != NULL) {
		g_free (c_tar->uncomp_filename);
		c_tar->uncomp_filename = NULL;
	}
	c_tar->name_modified = FALSE;

	new_name = g_strdup (comm->e_filename);
	l = strlen (new_name);

	switch (c_tar->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;

	case FR_COMPRESS_PROGRAM_GZIP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "gzip");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_end_command (comm->process);
		}

		/* X.tgz     -->  X.tar 
		 * X.tar.gz  -->  X.tar */
		if (file_extension_is (comm->e_filename, ".tgz")) {
			c_tar->name_modified = TRUE;
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (comm->e_filename, ".tar.gz")) 
			new_name[l - 3] = 0;
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "bzip");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_end_command (comm->process);
		}

		/* X.tbz     -->  X.tar 
		 * X.tar.bz  -->  X.tar */
		if (file_extension_is (comm->e_filename, ".tbz")) {
			c_tar->name_modified = TRUE;
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (comm->e_filename, ".tar.bz")) 
			new_name[l - 3] = 0;
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "bzip2");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, "-d");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_end_command (comm->process);
		}

		/* X.tbz2    -->  X.tar 
		 * X.tar.bz2 -->  X.tar */
		if (file_extension_is (comm->e_filename, ".tbz2")) {
			c_tar->name_modified = TRUE;
			new_name[l - 3] = 'a';
			new_name[l - 2] = 'r';
			new_name[l - 1] = 0;
		} else if (file_extension_is (comm->e_filename, ".tar.bz2")) 
			new_name[l - 4] = 0;
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS: 
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "uncompress");
			fr_process_add_arg (comm->process, "-f");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_end_command (comm->process);
		}

		/* X.taz   -->  X.tar 
		 * X.tar.Z -->  X.tar */
		if (file_extension_is (comm->e_filename, ".taz")) {
			c_tar->name_modified = TRUE;
			new_name[l - 1] = 'r';
		} else if (file_extension_is (comm->e_filename, ".tar.Z")) 
			new_name[l - 2] = 0;
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		if (path_is_file (comm->filename)) {
			fr_process_begin_command (comm->process, "lzop");
			fr_process_add_arg (comm->process, "-dfU");
			fr_process_add_arg (comm->process, "--no-stdin");
			fr_process_add_arg (comm->process, comm->e_filename);
			fr_process_end_command (comm->process);
		}

		/* X.tzo     -->  X.tar 
		 * X.tar.lzo -->  X.tar */
		if (file_extension_is (comm->e_filename, ".tzo")) {
			c_tar->name_modified = TRUE;
			new_name[l - 2] = 'a';
			new_name[l - 1] = 'r';
		} else if (file_extension_is (comm->e_filename, ".tar.lzo")) 
			new_name[l - 4] = 0;
		break;
	}

	c_tar->uncomp_filename = new_name;
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
}

 
static void 
fr_command_tar_init (FRCommand *comm)
{
	comm->propAddCanUpdate             = TRUE;
	comm->propAddCanReplace            = FALSE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;
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

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_tar_get_type ()
{
        static guint type = 0;

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

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_TAR, NULL));
	fr_command_construct (comm, process, filename);
	FR_COMMAND_TAR (comm)->compress_prog = prog;
	FR_COMMAND_TAR (comm)->uncomp_filename = NULL;

	return comm;
}
