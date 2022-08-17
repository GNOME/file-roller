/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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

#ifndef FR_COMMAND_H
#define FR_COMMAND_H

#include <glib.h>
#include "fr-archive.h"
#include "fr-process.h"

#define FR_PACKAGES(x) (x)

#define FR_TYPE_COMMAND            (fr_command_get_type ())
#define FR_COMMAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_COMMAND, FrCommand))
#define FR_COMMAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_COMMAND, FrCommandClass))
#define FR_IS_COMMAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_COMMAND))
#define FR_IS_COMMAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_COMMAND))
#define FR_COMMAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_COMMAND, FrCommandClass))

typedef struct _FrCommand        FrCommand;
typedef struct _FrCommandClass   FrCommandClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FrCommand, g_object_unref)

struct _FrCommand {
	FrArchive  __parent;

	/*<protected, read only>*/

	FrProcess *process;         /* the process object used to execute
				     * commands. */
	char      *filename;        /* local archive file path. */
	char      *e_filename;      /* escaped filename. */
	gboolean   creating_archive;
};

struct _FrCommandClass {
	FrArchiveClass __parent_class;

	/*<virtual functions>*/

	gboolean  (*list)           (FrCommand   *comm);
	void      (*add)            (FrCommand   *comm,
				     const char  *from_file,
				     GList       *file_list,
				     const char  *base_dir,
				     gboolean     update,
				     gboolean     follow_links);
	void      (*delete)         (FrCommand   *comm,
		                     const char  *from_file,
		                     GList       *file_list);
	void      (*extract)        (FrCommand   *comm,
		                     const char  *from_file,
		                     GList       *file_list,
		                     const char  *dest_dir,
		                     gboolean     overwrite,
		                     gboolean     skip_older,
		                     gboolean     junk_paths);
	void      (*test)           (FrCommand   *comm);
	void      (*uncompress)     (FrCommand   *comm);
	void      (*recompress)     (FrCommand   *comm);
	void      (*handle_error)   (FrCommand   *comm,
			             FrError     *error);
};

GType    fr_command_get_type         (void);

/**
 * fr_command_get_last_output:
 * Returns: (element-type guint8*) (transfer none): List of raw stderr (or stdout, if stderr is not present) lines of the last execution of the command, in their original encoding.
 */
GList *  fr_command_get_last_output  (FrCommand *command);

#endif /* FR_COMMAND_H */
