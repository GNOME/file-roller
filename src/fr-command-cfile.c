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

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include <glib.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "file-data.h"
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-cfile.h"

static void fr_command_cfile_class_init  (FRCommandCFileClass *class);
static void fr_command_cfile_init        (FRCommand           *afile);
static void fr_command_cfile_finalize    (GObject             *object);

/* Parent Class */

static FRCommandClass *parent_class = NULL;


static char *
get_uncompressed_name_from_archive (FRCommand  *comm, 
				    const char *e_filename)
{
	FRCommandCFile *comm_cfile = FR_COMMAND_CFILE (comm);
	int             fd;
	char            buffer[11];
	char           *filename = NULL;
	GString        *str = NULL;
	
	if (comm_cfile->compress_prog != FR_COMPRESS_PROGRAM_GZIP)
		return NULL;
	
	fd = open (e_filename, O_RDONLY);
	if (fd < 0) 
		return NULL;
	
	if (read (fd, &buffer, 11) < 11) {
		close (fd);
		return NULL;
	}
	
	/* Do we have the extra field in the header ?*/
	if ((buffer[10] & 0x04) != 0) {
		close (fd);
		return NULL;
	}
	
	str = g_string_new ("");
	/* Don't lose the first character */
	g_string_append_c (str, buffer[10]);
	while (read (fd, &buffer, 1) > 0) {
		if (buffer[0] == '\0') {
			close (fd);
			/* FIXME verify if there's a '/' in the name,
			 * and discard it if so */
			filename = g_strdup (str->str);
			g_string_free (str, FALSE);
			g_message ("filename is: %s", filename);
			return filename;
		}
		
		g_string_append_c (str, buffer[0]);
	}
	
	close (fd);
	g_string_free (str, FALSE);
	
	return NULL;
}


static void
list__process_line (char     *line, 
		    gpointer  data)
{
	FRCommand  *comm = FR_COMMAND (data);
	FileData   *fdata;
	char      **fields;
	char       *filename;

	fdata = file_data_new ();

	fields = split_line (line, 2);
	fdata->size = atol (fields[1]);
	g_strfreev (fields);

	if (fdata->size == -1)
		fdata->size = get_file_size (comm->filename);

	filename = get_uncompressed_name_from_archive (comm, comm->filename);
	if (filename == NULL) 
		filename = remove_extension_from_path (comm->filename);

	fdata->full_path = g_strconcat ("/", 
					file_name_from_path (filename),
					NULL);
	g_free (filename);

	fdata->original_path = fdata->full_path + 1;
	fdata->link = NULL;
	fdata->modified = get_file_mtime (comm->filename);
	
	fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);
	fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);
	
	if (*fdata->name == 0)
		file_data_free (fdata);
	else 
		comm->file_list = g_list_prepend (comm->file_list, fdata);
}


static void
fr_command_cfile_list (FRCommand *comm)
{
	FRCommandCFile *comm_cfile = FR_COMMAND_CFILE (comm);

	if (comm_cfile->compress_prog == FR_COMPRESS_PROGRAM_GZIP) {
		/* gzip let us known the uncompressed size */

		fr_process_set_out_line_func (FR_COMMAND (comm)->process, 
					      list__process_line,
					      comm);

		fr_process_begin_command (comm->process, "gzip");
		fr_process_add_arg (comm->process, "-l");
		fr_process_add_arg (comm->process, "-q");
		fr_process_add_arg (comm->process, comm->e_filename);
		fr_process_end_command (comm->process);
		fr_process_start (comm->process);

	} else {
		/* ... other compressors do not support this feature so 
		 * simply use the archive size, suboptimal but there is no 
		 * alternative. */

		FileData *fdata;
		char     *filename;

		fdata = file_data_new ();

		filename = remove_extension_from_path (comm->filename);
		fdata->full_path = g_strconcat ("/", 
						file_name_from_path (filename),
						NULL);
		g_free (filename);

		fdata->original_path = fdata->full_path + 1;
		fdata->link = NULL;
		fdata->size = get_file_size (comm->filename);
		fdata->modified = get_file_mtime (comm->filename);
		
		fdata->name = g_strdup (file_name_from_path (fdata->full_path));
		fdata->path = remove_level_from_path (fdata->full_path);
		fdata->type = gnome_vfs_mime_type_from_name_or_default (fdata->name, GNOME_VFS_MIME_TYPE_UNKNOWN);
		
		if (*fdata->name == 0)
			file_data_free (fdata);
		else 
			comm->file_list = g_list_prepend (comm->file_list, fdata);
		
		comm_cfile->error.type = FR_PROC_ERROR_NONE;
		comm_cfile->error.status = 0;
		g_signal_emit_by_name (G_OBJECT (comm), 
				       "done",
				       comm->action, 
				       &comm_cfile->error);
	}
}


static void
fr_command_cfile_add (FRCommand     *comm,
		      GList         *file_list,
		      const char    *base_dir,
		      gboolean       update,
		      const char    *password,
		      FRCompression  compression)
{
	/* never called */
}


static void
fr_command_cfile_delete (FRCommand *comm,
			 GList     *file_list)
{
	/* never called */
}


static void
fr_command_cfile_extract (FRCommand  *comm,
			  GList      *file_list,
			  const char *dest_dir,
			  gboolean    overwrite,
			  gboolean    skip_older,
			  gboolean    junk_paths,
			  const char *password)
{
	FRCommandCFile *comm_cfile = FR_COMMAND_CFILE (comm);
	char           *temp_dir;
	char           *e_temp_dir;
	char           *e_dest_dir;
	char           *temp_file;
	char           *uncompr_file;
	char           *compr_file;
	char           *e_compr_file;

	temp_dir = get_temp_work_dir_name ();
	e_temp_dir = shell_escape (temp_dir);

	/* create a temp dir. */

	fr_process_begin_command (comm->process, "mkdir");
	fr_process_add_arg (comm->process, e_temp_dir);
	fr_process_end_command (comm->process);


	/* copy file to the temp dir, remove the already existing file first */

	compr_file = get_uncompressed_name_from_archive (comm, comm->filename);
	if (compr_file != NULL) 
		e_compr_file = shell_escape (compr_file);
	else 
		e_compr_file = NULL;
	
	temp_file = g_strconcat (e_temp_dir,
				 "/",
				 (e_compr_file != NULL) ? e_compr_file : file_name_from_path (comm->e_filename),
				 e_compr_file ? ".gz" : NULL,
				 NULL);
	g_free (e_compr_file);
	g_free (compr_file);

	fr_process_begin_command (comm->process, "cp");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, comm->e_filename);
	fr_process_add_arg (comm->process, temp_file);
	fr_process_end_command (comm->process);

	/* uncompress the file */

	switch (comm_cfile->compress_prog) {
	case FR_COMPRESS_PROGRAM_NONE:
		break;
		
	case FR_COMPRESS_PROGRAM_GZIP:
		fr_process_begin_command (comm->process, "gzip");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, "-N");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
		break;

	case FR_COMPRESS_PROGRAM_BZIP:
		fr_process_begin_command (comm->process, "bzip");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
		break;

	case FR_COMPRESS_PROGRAM_BZIP2:
		fr_process_begin_command (comm->process, "bzip2");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
		break;

	case FR_COMPRESS_PROGRAM_COMPRESS: 
		fr_process_begin_command (comm->process, "uncompress");
		fr_process_add_arg (comm->process, "-f");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
		break;

	case FR_COMPRESS_PROGRAM_LZOP:
		fr_process_begin_command (comm->process, "lzop");
		fr_process_set_working_dir (comm->process, temp_dir);
		fr_process_add_arg (comm->process, "-d");
		fr_process_add_arg (comm->process, "-fU");
		fr_process_add_arg (comm->process, "--no-stdin");
		fr_process_add_arg (comm->process, temp_file);
		fr_process_end_command (comm->process);
		break;
	}

	/* copy uncompress file to the dest dir */

	uncompr_file = remove_extension_from_path (temp_file);
	e_dest_dir = shell_escape (dest_dir);

	fr_process_begin_command (comm->process, "cp");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, uncompr_file);
	fr_process_add_arg (comm->process, e_dest_dir);
	fr_process_end_command (comm->process);

	/* remove the temp dir */

	fr_process_begin_command (comm->process, "rm");
	fr_process_set_sticky (comm->process, TRUE);
	fr_process_add_arg (comm->process, "-rf");
	fr_process_add_arg (comm->process, e_temp_dir);
	fr_process_end_command (comm->process);

	g_free (e_dest_dir);
	g_free (uncompr_file);
	g_free (temp_file);
	g_free (e_temp_dir);
	g_free (temp_dir);
}


static void 
fr_command_cfile_class_init (FRCommandCFileClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FRCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FRCommandClass*) class;

        gobject_class->finalize = fr_command_cfile_finalize;

        afc->list         = fr_command_cfile_list;
	afc->add          = fr_command_cfile_add;
	afc->delete       = fr_command_cfile_delete;
	afc->extract      = fr_command_cfile_extract;
}

 
static void 
fr_command_cfile_init (FRCommand *comm)
{
	comm->propAddCanUpdate             = FALSE;
	comm->propAddCanReplace            = FALSE; 
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;
}


static void 
fr_command_cfile_finalize (GObject *object)
{
	FRCommandCFile *comm_tar;

        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_CFILE (object));

	comm_tar = FR_COMMAND_CFILE (object);

	 /* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
                G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_cfile_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandCFileClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_cfile_class_init,
			NULL,
			NULL,
			sizeof (FRCommandCFile),
			0,
			(GInstanceInitFunc) fr_command_cfile_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandCFile",
					       &type_info,
					       0);
        }

        return type;
}


FRCommand *
fr_command_cfile_new (FRProcess         *process,
		      const char        *filename,
		      FRCompressProgram  prog)
{
	FRCommand *comm;

	comm = FR_COMMAND (g_object_new (FR_TYPE_COMMAND_CFILE, NULL));
	fr_command_construct (comm, process, filename);
	FR_COMMAND_CFILE (comm)->compress_prog = prog;

	return comm;
}
