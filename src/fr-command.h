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

#ifndef FR_COMMAND_H
#define FR_COMMAND_H

#include <glib.h>
#include "file-data.h"
#include "fr-process.h"

#define FR_TYPE_COMMAND            (fr_command_get_type ())
#define FR_COMMAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_COMMAND, FRCommand))
#define FR_COMMAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_COMMAND, FRCommandClass))
#define FR_IS_COMMAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_COMMAND))
#define FR_IS_COMMAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_COMMAND))
#define FR_COMMAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_COMMAND, FRCommandClass))

typedef struct _FRCommand       FRCommand;
typedef struct _FRCommandClass  FRCommandClass;

typedef enum {
	FR_ACTION_LIST,
	FR_ACTION_ADD,
	FR_ACTION_DELETE,
	FR_ACTION_EXTRACT,
	FR_ACTION_TEST,
	FR_ACTION_GET_LIST
} FRAction;

struct _FRCommand
{
        GObject  __parent;

	GList * file_list;       /* FileData elements */

	/*<protected>*/

	/* properties the command support. */

	uint propAddCanUpdate : 1;
	uint propAddCanReplace : 1;
	uint propExtractCanAvoidOverwrite : 1;
	uint propExtractCanSkipOlder : 1;
	uint propExtractCanJunkPaths : 1;
	uint propPassword : 1;
	uint propTest : 1;

	/* used by the progress signal */

	int     n_file;
	int     n_files;         /* used by the progress signal */

	/*<private>*/

	FRProcess *process;      /* the process object used to execute 
				  * commands. */
	FRAction   action;       /* current action. */
	char      *filename;     /* archive filename. */
	char      *e_filename;   /* escaped archive filename. */

	gboolean   fake_load;    /* if TRUE does nothing when the list 
				  * operation is invoked. */
};

struct _FRCommandClass
{
        GObjectClass __parent_class;

	/*<virtual functions>*/

	void        (*list)           (FRCommand     *comm);

	void        (*add)            (FRCommand     *comm,
				       GList         *file_list,
				       const char    *base_dir,
				       gboolean       update,
				       const char    *password,
				       FRCompression  compression); 

	void        (*delete)         (FRCommand     *comm,
				       GList         *file_list); 

	void        (*extract)        (FRCommand     *comm,
				       GList         *file_list,
				       const char    *dest_dir,
				       gboolean       overwrite,
				       gboolean       skip_older,
				       gboolean       junk_paths,
				       const char    *password);

	void        (*test)           (FRCommand     *comm,
				       const char    *password);

	void        (*uncompress)     (FRCommand     *comm);

	void        (*recompress)     (FRCommand     *comm,
				       FRCompression  compression);

	void        (*handle_error)   (FRCommand     *comm,
				       FRProcError   *error);


	/*<signals>*/

	void        (*start)          (FRCommand   *comm,
				       FRAction     action); 

	void        (*done)           (FRCommand   *comm,
				       FRAction     action,
				       FRProcError *error);

        void        (*progress)       (FRCommand   *comm,
				       double       fraction);

        void        (*message)        (FRCommand   *comm,
				       const char  *msg);
};

GType          fr_command_get_type           (void);

void           fr_command_construct          (FRCommand     *comm,
					      FRProcess     *process,
					      const char    *filename);

void           fr_command_set_filename       (FRCommand     *comm,
					      const char    *filename);

void           fr_command_list               (FRCommand     *comm);

void           fr_command_add                (FRCommand     *comm,
					      GList         *file_list,
					      const char    *base_dir,
					      gboolean       update,
					      const char    *password,
					      FRCompression  compression); 

void           fr_command_delete             (FRCommand     *comm,
					      GList         *file_list); 

void           fr_command_extract            (FRCommand     *comm,
					      GList         *file_list,
					      const char    *dest_dir,
					      gboolean       overwrite,
					      gboolean       skip_older,
					      gboolean       junk_paths,
					      const char    *password);

void           fr_command_test               (FRCommand     *comm,
					      const char    *password);

void           fr_command_uncompress         (FRCommand     *comm);

void           fr_command_recompress         (FRCommand     *comm,
					      FRCompression  compression);

/* protected functions */

void           fr_command_progress           (FRCommand     *comm,
					      double         fraction);

void           fr_command_message            (FRCommand     *comm,
					      const char    *msg);

void           fr_command_set_n_files        (FRCommand     *comm,
					      int            n_files);

/* private functions */

void           fr_command_handle_error       (FRCommand     *comm,
					      FRProcError   *error);

#endif /* FR_COMMAND_H */
