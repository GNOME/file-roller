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

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include "file-data.h"
#include "file-list.h"
#include "file-utils.h"
#include "fr-archive.h"
#include "fr-command.h"
#include "fr-command-cfile.h"
#include "fr-command-lha.h"
#include "fr-command-rar.h"
#include "fr-command-tar.h"
#include "fr-command-zip.h"
#include "fr-marshal.h"
#include "fr-process.h"

#define MAX_CHUNK_LEN 16000 /* FIXME : what is the max length of a command 
			     * line ? */
#define UNKNOWN_TYPE "application/octet-stream"

enum {
	START,
	DONE,
	LAST_SIGNAL
};

static GObjectClass *parent_class;
static guint fr_archive_signals[LAST_SIGNAL] = { 0 };

static void fr_archive_class_init (FRArchiveClass *class);
static void fr_archive_init       (FRArchive *archive);
static void fr_archive_finalize   (GObject *object);


GType
fr_archive_get_type (void)
{
        static GType type = 0;

        if (! type) {
                static const GTypeInfo type_info = {
			sizeof (FRArchiveClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_archive_class_init,
			NULL,
			NULL,
			sizeof (FRArchive),
			0,
			(GInstanceInitFunc) fr_archive_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "FRArchive",
					       &type_info,
					       0);
        }

        return type;
}


static void
fr_archive_class_init (FRArchiveClass *class)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (class);

        parent_class = g_type_class_peek_parent (class);

	fr_archive_signals[START] =
                g_signal_new ("start",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRArchiveClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE, 
			      1, G_TYPE_INT);
	fr_archive_signals[DONE] =
                g_signal_new ("done",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRArchiveClass, done),
			      NULL, NULL,
			      fr_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT,
			      G_TYPE_POINTER);
	
	gobject_class->finalize = fr_archive_finalize;
	class->start = NULL;
	class->done = NULL;
}


static void
fr_archive_init (FRArchive *archive)
{
	archive->filename = NULL;
	archive->command = NULL;
	archive->process = fr_process_new ();
	archive->is_compressed_file = FALSE;
	archive->fake_load = FALSE;
}


FRArchive *
fr_archive_new ()
{
	return FR_ARCHIVE (g_object_new (FR_TYPE_ARCHIVE, NULL));
}


static void
fr_archive_finalize (GObject *object)
{
	FRArchive *archive;

	g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_ARCHIVE (object));
  
	archive = FR_ARCHIVE (object);

	if (archive->filename != NULL)
		g_free (archive->filename);

	if (archive->command != NULL) 
		g_object_unref (archive->command);

	g_object_unref (archive->process);

	/* Chain up */

        if (G_OBJECT_CLASS (parent_class)->finalize)
                G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
create_command_from_mime_type (FRArchive  *archive, 
			       const char *filename,
			       const char *mime_type)
{
	archive->is_compressed_file = FALSE;

	if (is_mime_type (mime_type, "application/x-tar")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_NONE);
	} else if (is_mime_type (mime_type, "application/x-compressed-tar")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_GZIP);
	} else if (is_mime_type (mime_type, "application/x-bzip-compressed-tar")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_BZIP2);
	} else if (is_mime_type (mime_type, "application/zip")) {
		archive->command = fr_command_zip_new (archive->process, 
						       filename);
	} else if (is_mime_type (mime_type, "application/x-rar")) {
		archive->command = fr_command_rar_new (archive->process, 
						       filename);
	} else 
		return FALSE;

	return TRUE;
}


/* filename must not be escaped. */
static const char *
get_mime_type_from_content (const char *filename) 
{
	const char *mime_type;

	mime_type = gnome_vfs_get_file_mime_type (filename, NULL, FALSE);

	if (strcmp (mime_type, UNKNOWN_TYPE) == 0)
		return NULL;

	return mime_type;
}
	

static gboolean
hexcmp (const guchar *first_bytes,
	const guchar *buffer,
	int           len)
{
	int i;

	for (i = 0; i < len; i++)
		if (first_bytes[i] != buffer[i])
			return FALSE;

	return TRUE;
}


/* filename must not be escaped. */
static const char *
get_mime_type_from_sniffer (const char *filename) 
{
	static struct {
		const char *mime_type;
		const char *first_bytes;
		int         len;
	}            sniffer_data [] = { 
		{ "application/zip",                   "\x50\x4B\x03\x04", 4 },
		/* FIXME
		{ "application/x-compressed-tar",      "\x1F\x8B\x08\x08", 4 },
		{ "application/x-bzip-compressed-tar", "\x42\x5A\x68\x39", 4 },
		*/
		{ NULL, NULL, 0 } 
	};
	FILE        *file;
	char         buffer[80];
	int          n, i;

	file = fopen (filename, "r");

	if (file == NULL) 
                return NULL;

	n = fread (buffer, sizeof (char), sizeof (buffer), file);
	buffer[n] = 0;

	fclose (file);

	for (i = 0; sniffer_data[i].mime_type != NULL; i++) {
		const char *first_bytes = sniffer_data[i].first_bytes;
		int          len        = sniffer_data[i].len;

		if (hexcmp (first_bytes, buffer, len)) 
			return sniffer_data[i].mime_type;
	}

	return NULL;
}


/* filename must not be escaped. */
static gboolean
create_command_from_filename (FRArchive  *archive, 
			      const char *filename,
			      gboolean    loading)
{
	if (file_extension_is (filename, ".tar.gz")
	    || file_extension_is (filename, ".tgz")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_GZIP);
	} else if (file_extension_is (filename, ".tar.bz2")
		   || file_extension_is (filename, ".tbz2")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_BZIP2);
	} else if (file_extension_is (filename, ".tar.bz")
		   || file_extension_is (filename, ".tbz")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_BZIP);
	} else if (file_extension_is (filename, ".tar.Z")
		   || file_extension_is (filename, ".taz")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_COMPRESS);
	} else if (file_extension_is (filename, ".tar.lzo")
		   || file_extension_is (filename, ".tzo")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_LZOP);
	} else if (file_extension_is (filename, ".tar")) {
		archive->command = fr_command_tar_new (archive->process, 
						       filename, 
						       FR_COMPRESS_PROGRAM_NONE);
	} else if (file_extension_is (filename, ".zip")
		   || file_extension_is (filename, ".ear")
		   || file_extension_is (filename, ".jar")
		   || file_extension_is (filename, ".war")) {
		archive->command = fr_command_zip_new (archive->process, 
						       filename);
	} else if (file_extension_is (filename, ".lzh")) {
		archive->command = fr_command_lha_new (archive->process, 
						       filename);
	} else if (file_extension_is (filename, ".rar")) {
		archive->command = fr_command_rar_new (archive->process, 
						       filename);
	} else if (loading) {
		if (file_extension_is (filename, ".gz")
		    || file_extension_is (filename, ".z")
		    || file_extension_is (filename, ".Z")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_GZIP);
			archive->is_compressed_file = TRUE;
		} else if (file_extension_is (filename, ".bz")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_BZIP);
			archive->is_compressed_file = TRUE;
		} else if (file_extension_is (filename, ".bz2")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_BZIP2);
			archive->is_compressed_file = TRUE;
		} else if (file_extension_is (filename, ".lzo")) {
			archive->command = fr_command_cfile_new (archive->process, filename, FR_COMPRESS_PROGRAM_LZOP);
			archive->is_compressed_file = TRUE;
		} else
			return FALSE;
	} else
		return FALSE;
	
	return TRUE;
}


static void
action_started (FRCommand *command, 
		FRAction   action,
		FRArchive *archive)
{
	g_signal_emit (G_OBJECT (archive), 
		       fr_archive_signals[START],
		       0,
		       action);
}


static void
action_performed (FRCommand   *command, 
		  FRAction     action,
		  FRProcError *error,
		  FRArchive   *archive)
{
	char *s_action = NULL;

#ifdef DEBUG
	switch (action) {
	case FR_ACTION_LIST:
		s_action = "List";
		break;
	case FR_ACTION_ADD:
		s_action = "Add";
		break;
	case FR_ACTION_DELETE:
		s_action = "Delete";
		break;
	case FR_ACTION_EXTRACT:
		s_action = "Extract";
		break;
	case FR_ACTION_TEST:
		s_action = "Test";
		break;
	}
	g_print ("%s [DONE]\n", s_action);
#endif

	g_signal_emit (G_OBJECT (archive), 
		       fr_archive_signals[DONE],
		       0,
		       action,
		       error);
}


/* filename must not be escaped. */
gboolean
fr_archive_new_file (FRArchive  *archive, 
		     const char *filename)
{
	FRCommand *tmp_command;

	if (filename == NULL)
		return FALSE;

	tmp_command = archive->command;
	if (! create_command_from_filename (archive, filename, FALSE)) {
		archive->command = tmp_command;
		return FALSE;
	}

	if (tmp_command != NULL) 
		g_object_unref (G_OBJECT (tmp_command));

	archive->read_only = FALSE;

	if (archive->filename != NULL)
		g_free (archive->filename);	
	archive->filename = g_strdup (filename);

	g_signal_connect (G_OBJECT (archive->command), 
			  "start",
			  G_CALLBACK (action_started),
			  archive);
	g_signal_connect (G_OBJECT (archive->command), 
			  "done",
			  G_CALLBACK (action_performed),
			  archive);

	return TRUE;
}


/* filename must not be escaped. */
gboolean
fr_archive_load (FRArchive  *archive, 
		 const char *filename)
{
	FRCommand  *tmp_command;
	const char *mime_type;

	g_return_val_if_fail (archive != NULL, FALSE);

	if (access (filename, F_OK) != 0) /* file must exists. */
		return FALSE;

	archive->read_only = access (filename, W_OK) != 0;

	if (archive->filename != NULL)
		g_free (archive->filename);
	archive->filename = g_strdup (filename);

	tmp_command = archive->command;

	/* prefer mime-magic */

	mime_type = get_mime_type_from_sniffer (filename);
	if (mime_type == NULL)
		mime_type = get_mime_type_from_content (filename);

	if ((mime_type == NULL)
	    || ! create_command_from_mime_type (archive, filename, mime_type))
		if (! create_command_from_filename (archive, filename, TRUE)) {
			archive->command = tmp_command;
			return FALSE;
		}

	if (tmp_command != NULL) 
		g_object_unref (G_OBJECT (tmp_command));

	g_signal_connect (G_OBJECT (archive->command), 
			  "start",
			  G_CALLBACK (action_started),
			  archive);

	g_signal_connect (G_OBJECT (archive->command), 
			  "done",
			  G_CALLBACK (action_performed),
			  archive);

	archive->command->fake_load = archive->fake_load;
	fr_command_list (archive->command);

	return TRUE;
}


void
fr_archive_reload (FRArchive *archive)
{
	g_return_if_fail (archive != NULL);
	g_return_if_fail (archive->filename != NULL);

	archive->command->fake_load = archive->fake_load;
	fr_command_list (archive->command);
}


/* filename must not be escaped. */
void
fr_archive_rename (FRArchive  *archive,
		   const char *filename)
{
	g_return_if_fail (archive != NULL);

	if (archive->is_compressed_file) 
		/* If the archive is a compressed file we have to reload it,
		 * because in this case the 'content' of the archive changes 
		 * too. */
		fr_archive_load (archive, filename);
	else {
		if (archive->filename != NULL)
			g_free (archive->filename);
		archive->filename = g_strdup (filename);
		
		fr_command_set_filename (archive->command, filename);
	}
}


/* -- add -- */


/* Note: all paths unescaped. */
static FileData *
find_file_in_archive (FRArchive *archive, 
		      char      *path)
{
	GList *scan;

	for (scan = archive->command->file_list; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		
		if (strcmp (path, fdata->original_path) == 0)
			return fdata;
	}

	return NULL;
}


static void _archive_remove (FRArchive *archive, GList *file_list);


static GList *
escape_file_list (GList *file_list) {
	GList *e_file_list = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		gchar *filename = scan->data;

		e_file_list = g_list_prepend (e_file_list, 
					      shell_escape (filename));
	}

	return e_file_list;
}


/* Note: all paths unescaped. */
static GList *
newer_files_only (FRArchive  *archive,
		  GList      *file_list,
		  const char *base_dir)
{
	GList *newer_files = NULL;
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		gchar *filename = scan->data;
		gchar *fullpath;
		FileData *fdata;

		fdata = find_file_in_archive (archive, filename);

		if (fdata == NULL) {
			newer_files = g_list_prepend (newer_files, scan->data);
			continue;
		}

		fullpath = g_strconcat (base_dir, "/", filename, NULL);

		if (path_is_file (fullpath)
		    && (fdata->modified >= get_file_mtime (fullpath))) {
			g_free (fullpath);
			continue;
		}

		newer_files = g_list_prepend (newer_files, scan->data);
		g_free (fullpath);
	}
	
	return newer_files;
}


/* Note: all paths unescaped. */
void
fr_archive_add (FRArchive   *archive, 
		GList       *file_list, 
		const char  *base_dir,
		gboolean     update,
		const char  *password,
		FRCompression  compression)
{
	GList    *new_file_list;
	gboolean  free_new_file_list;
	GList    *e_file_list;
	GList    *scan;

	if (file_list == NULL)
		return;

	if (archive->read_only)
		return;

	/* if the command cannot update,  get the list of files that are 
	 * newer than the ones in the archive. */

	free_new_file_list = FALSE;
	if (update && ! archive->command->propAddCanUpdate) {
		free_new_file_list = TRUE;
		new_file_list = newer_files_only (archive, file_list, base_dir);
	} else
		new_file_list = file_list;

	if (new_file_list == NULL) {
#ifdef DEBUG
		g_print ("nothing to update.\n");
#endif
		return;
	}

	fr_process_clear (archive->process);

	fr_command_uncompress (archive->command);

	/* when files are already present in a tar archive and are added
	 * again, they are not replaced, so we have to delete them first. */

	/* if we are adding (== ! update) and 'add' cannot replace or
	 * if we are updating and 'add' cannot update, 
	 * delete the files first. */

	if ((! update && ! archive->command->propAddCanReplace)
	    || (update && ! archive->command->propAddCanUpdate)) {
		GList *del_list = NULL;

		for (scan = new_file_list; scan != NULL; scan = scan->next) {
			gchar *filename = scan->data;
			if (find_file_in_archive (archive, filename)) 
				del_list = g_list_prepend (del_list, filename);
		}

		/* delete */

		if (del_list != NULL) {
			_archive_remove (archive, del_list);
			g_list_free (del_list);
		}
	}

	/* add now. */

	e_file_list = escape_file_list (new_file_list);

	for (scan = e_file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;
		
		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += strlen (scan->data);
		}
		
		prev->next = NULL;
		fr_command_add (archive->command, 
				chunk_list, 
				base_dir, 
				update,
				password,
				compression);
		prev->next = scan;
	}

	path_list_free (e_file_list);
	if (free_new_file_list)
		g_list_free (new_file_list);

	fr_command_recompress (archive->command, compression);

	fr_process_start (archive->process);
}


static void
file_list_remove_from_pattern (GList      **list, 
			       const char  *pattern)
{
	gchar **patterns;
	GList  *scan;
	
	if (pattern == NULL)
		return;
	
	patterns = search_util_get_patterns (pattern);
	
	for (scan = *list; scan;) {
		char *path = scan->data;

		if (match_patterns (patterns, file_name_from_path (path))) {
			*list = g_list_remove_link (*list, scan);
			g_free (scan->data);
			g_list_free (scan);
			scan = *list;
		} else
			scan = scan->next;
	}
	
	g_strfreev (patterns);
}


/* Note: all paths unescaped. */
void
fr_archive_add_with_wildcard (FRArchive  *archive, 
			      const char *include_files,
			      const char *exclude_files,
			      const char *base_dir,
			      gboolean    update,
			      gboolean    recursive,
			      gboolean    follow_links,
			      gboolean    same_fs,
			      gboolean    no_backup_files,
			      gboolean    no_dot_files,
			      gboolean    ignore_case,
			      const char *password,
			      FRCompression compression)
{
	GList *file_list;

	if (archive->read_only)
		return;

	file_list = get_wildcard_file_list (base_dir, 
					    include_files, 
					    recursive, 
					    follow_links, 
					    same_fs,
					    no_backup_files, 
					    no_dot_files, 
					    ignore_case);

	file_list_remove_from_pattern (&file_list, exclude_files);

	fr_archive_add (archive,
			file_list,
			base_dir,
			update,
			password,
			compression);
	path_list_free (file_list);
}


/* Note: all paths unescaped. */
void
fr_archive_add_directory (FRArchive  *archive, 
			  const char *directory,
			  const char *base_dir,
			  gboolean    update,
			  const char *password,
			  FRCompression compression)

{
	GList *file_list;

	if (archive->read_only)
		return;

	file_list = get_directory_file_list (directory, base_dir);
	fr_archive_add (archive,
			file_list,
			base_dir,
			update,
			password,
			compression);
	path_list_free (file_list);
}


/* -- remove -- */


/* Note: all paths unescaped. */
static void
_archive_remove (FRArchive *archive,
		 GList     *file_list)
{
	GList *e_file_list;
	GList *scan;

	/* file_list == NULL means delete all files in archive. */

	if (file_list == NULL) 
		file_list = archive->command->file_list;

	e_file_list = escape_file_list (file_list);

	for (scan = e_file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;
		
		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += strlen (scan->data);
		}
		
		prev->next = NULL;
		fr_command_delete (archive->command, chunk_list);
		prev->next = scan;
	}

	path_list_free (e_file_list);
}


/* Note: all paths unescaped. */
void
fr_archive_remove (FRArchive     *archive,
		   GList         *file_list,
		   FRCompression  compression)
{
	g_return_if_fail (archive != NULL);

	if (archive->read_only)
		return;

	fr_process_clear (archive->process);
	fr_command_uncompress (archive->command);
	_archive_remove (archive, file_list);
	fr_command_recompress (archive->command, compression);
	fr_process_start (archive->process);
}


/* -- extract -- */


/* Note: all paths escaped, source_dir and dest_dir escaped. */
static void
move_files_to_dir (FRArchive  *archive,
		   GList      *file_list,
		   const char *source_dir,
		   const char *dest_dir)
{
	GList *scan;

	fr_process_begin_command (archive->process, "mv");
	fr_process_add_arg (archive->process, "-f");
	for (scan = file_list; scan; scan = scan->next) {
		char  path[4096]; /* FIXME : 4096 ? */
		char *filename = scan->data;
		
		if (filename[0] == '/')
			sprintf (path, "%s%s", source_dir, filename);
		else
			sprintf (path, "%s/%s", source_dir, filename);
		
		fr_process_add_arg (archive->process, path);
	}
	fr_process_add_arg (archive->process, dest_dir);
	fr_process_end_command (archive->process);
}


/* Note: all paths escaped, temp_dir and dest_dir unescaped. */
static void
move_files_in_chunks (FRArchive  *archive,
		      GList      *file_list,
		      const char *temp_dir,
		      const char *dest_dir)
{
	GList *scan;
	int    e_temp_dir_l;
	char  *e_temp_dir;
	char  *e_dest_dir;

	e_temp_dir = shell_escape (temp_dir);
	e_dest_dir = shell_escape (dest_dir);
	e_temp_dir_l = strlen (e_temp_dir);

	for (scan = file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;

		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = e_temp_dir_l + 1 + strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += e_temp_dir_l + 1 + strlen (scan->data);
		}

		prev->next = NULL;
		move_files_to_dir (archive,
				   chunk_list,
				   e_temp_dir, 
				   e_dest_dir);
		prev->next = scan;
	}	
	
	g_free (e_temp_dir);
	g_free (e_dest_dir);
}


/* Note: all paths escaped, dest_dir unescaped. */
static void
extract_in_chunks (FRCommand  *command,
		   GList      *file_list,
		   const char *dest_dir,
		   gboolean    overwrite,
		   gboolean    skip_older,
		   gboolean    junk_paths,
		   const char *password)
{
	GList *scan;

	if (file_list == NULL) {
		fr_command_extract (command,
				    file_list,
				    dest_dir,
				    overwrite,
				    skip_older,
				    junk_paths,
				    password);
		return;
	}

	for (scan = file_list; scan != NULL; ) {
		GList *prev = scan->prev;
		GList *chunk_list;
		int    l;

		chunk_list = scan;
		l = 0;
		while ((scan != NULL) && (l < MAX_CHUNK_LEN)) {
			if (l == 0)
				l = strlen (scan->data);
			prev = scan;
			scan = scan->next;
			if (scan != NULL)
				l += strlen (scan->data);
		}

		prev->next = NULL;
		fr_command_extract (command,
				    chunk_list,
				    dest_dir,
				    overwrite,
				    skip_older,
				    junk_paths,
				    password);
		prev->next = scan;
	}
}


/* Note : All paths unescaped.  
 * Note2: Do not escape dest_dir it will escaped in fr_command_extract if 
 *        needed. */
void
fr_archive_extract (FRArchive  *archive,
		    GList      *file_list,
		    const char *dest_dir,
		    gboolean    skip_older,
		    gboolean    overwrite,
		    gboolean    junk_paths,
		    const char *password)
{
	GList    *filtered, *e_filtered;
	GList    *scan;
	gboolean  extract_all;
	gboolean  move_to_dest_dir;

	g_return_if_fail (archive != NULL);

	/* if a command supports all the requested options use 
	 * fr_command_extract directly. */

	if (! (! overwrite && ! archive->command->propExtractCanAvoidOverwrite)
	    && ! (skip_older && ! archive->command->propExtractCanSkipOlder)
	    && ! (junk_paths && ! archive->command->propExtractCanJunkPaths)) {
		GList *e_file_list;

		e_file_list = escape_file_list (file_list);

		fr_process_clear (archive->process);
		extract_in_chunks (archive->command,
				   e_file_list,
				   dest_dir,
				   overwrite,
				   skip_older,
				   junk_paths,
				   password);

		path_list_free (e_file_list);

		fr_process_start (archive->process);
		return;
	}

	/* .. else we have to implement the unsupported options. */

	fr_process_clear (archive->process);

	move_to_dest_dir = (junk_paths 
			    && ! archive->command->propExtractCanJunkPaths);

	extract_all = (file_list == NULL);
	if (extract_all) {
		GList *scan;

		scan = archive->command->file_list;
		for (; scan; scan = scan->next) {
			FileData *fdata = scan->data;
			file_list = g_list_prepend (file_list, g_strdup (fdata->original_path));
		}
	}

	filtered = NULL;
	for (scan = file_list; scan; scan = scan->next) {
		FileData   *fdata;
		char       *arch_filename = scan->data;
		char        dest_filename[4096];
		const char *filename;

		fdata = find_file_in_archive (archive, arch_filename);

		if (fdata == NULL)
			continue;

		/* get the destination file path. */

		if (! junk_paths)
			filename = arch_filename;
		else
			filename = file_name_from_path (arch_filename);

		if ((dest_dir[strlen (dest_dir) - 1] == '/')
		    || (filename[0] == '/'))
			sprintf (dest_filename, "%s%s", dest_dir, filename);
		else
			sprintf (dest_filename, "%s/%s", dest_dir, filename);
		

#ifdef DEBUG		
		g_print ("-> %s\n", dest_filename);
#endif

		/**/
		
		if (! archive->command->propExtractCanSkipOlder
		    && skip_older 
		    && path_is_file (dest_filename)
		    && (fdata->modified < get_file_mtime (dest_filename)))
			continue;

		if (! archive->command->propExtractCanAvoidOverwrite
		    && ! overwrite 
		    && path_is_file (dest_filename))
			continue;

		filtered = g_list_prepend (filtered, fdata->original_path);
	}

	if (filtered == NULL) {
		/* all files got filtered, do nothing. */

#ifdef DEBUG
		g_print ("All files got filtered, do nothing.\n");
#endif

		if (extract_all) 
			path_list_free (file_list);
		return;
	} 

	e_filtered = escape_file_list (filtered);	

	if (move_to_dest_dir) {
		char *temp_dir;
		char *e_temp_dir;

		temp_dir = g_strdup_printf ("%s%s%d",
					    g_get_tmp_dir (),
					    "/file-roller.",
					    getpid ());
		ensure_dir_exists (temp_dir, 0700);
		extract_in_chunks (archive->command,
				   e_filtered,
				   temp_dir,
				   overwrite,
				   skip_older,
				   junk_paths,
				   password);

		move_files_in_chunks (archive, 
				      e_filtered, 
				      temp_dir, 
				      dest_dir);

		/* remove the temp dir. */

		e_temp_dir = shell_escape (temp_dir);
		fr_process_begin_command (archive->process, "rm");
		fr_process_add_arg (archive->process, "-rf");
		fr_process_add_arg (archive->process, e_temp_dir);
		fr_process_end_command (archive->process);
		g_free (e_temp_dir);

		g_free (temp_dir);
	} else
		extract_in_chunks (archive->command,
				   e_filtered,
				   dest_dir,
				   overwrite,
				   skip_older,
				   junk_paths,
				   password);

	path_list_free (e_filtered);
	if (filtered != NULL)
		g_list_free (filtered);

	if (extract_all) 
		/* the list has been created in this function. */
		path_list_free (file_list);

	fr_process_start (archive->process);
}


void
fr_archive_test (FRArchive  *archive,
		 const char *password)
{
	fr_process_clear (archive->process);
	fr_command_test (archive->command, password);
	fr_process_start (archive->process);
}


/*
 * Remember to keep the ext array in alphanumeric order and to scan the array
 * in reverse order, this is because the file 'foo.tar.gz' must return the 
 * '.tar.gz' and not the '.gz' extension.
 */
G_CONST_RETURN gchar *
fr_archive_utils__get_file_name_ext (const char *filename)
{
	static char * ext[] = {
		".bz", 
		".bz2", 
		".ear",
		".gz", 
		".jar",
		".lzh",
		".lzo",
		".rar",
		".tar", 
		".tar.bz", 
		".tar.bz2", 
		".tar.gz", 
		".tar.lzo",
		".tar.Z", 
		".taz", 
		".tbz",
		".tbz2",
		".tgz", 
		".tzo",
		".war",
		".z", 
		".zip",
		".Z" 
	};
	int n = sizeof (ext) / sizeof (char*);
	int i;

	for (i = n - 1; i >= 0; i--) 
		if (file_extension_is (filename, ext[i]))
			return ext[i];

	return NULL;
}


gboolean
fr_archive_utils__file_is_archive (const char *filename)
{
	const char *mime_type;

	mime_type = get_mime_type_from_content (filename);

	if (mime_type != NULL)
		return TRUE;

	g_print ("\n-- CONTENT --> %s <--\n\n", mime_type);

	mime_type = get_mime_type_from_sniffer (filename);

	g_print ("\n-- SNIFFER --> %s <--\n\n", mime_type);

	if (mime_type != NULL)
		return TRUE;

	return fr_archive_utils__get_file_name_ext (filename) != NULL;
}
