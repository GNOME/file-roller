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

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include "file-data.h"
#include "file-utils.h"
#include "fr-process.h"
#include "fr-command.h"
#include "fr-marshal.h"

enum {
	START,
        DONE,
        LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint fr_command_signals[LAST_SIGNAL] = { 0 };

static void fr_command_class_init  (FRCommandClass *class);
static void fr_command_init        (FRCommand *afile);
static void fr_command_finalize    (GObject *object);


GType
fr_command_get_type ()
{
        static guint type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRCommandClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_class_init,
			NULL,
			NULL,
			sizeof (FRCommand),
			0,
			(GInstanceInitFunc) fr_command_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "FRCommand",
					       &type_info,
					       0);
	}

        return type;
}


static void
base_fr_command_list (FRCommand *comm)
{
}


static void
base_fr_command_add (FRCommand     *comm,
		     GList         *file_list,
		     const char    *base_dir,
		     gboolean       update,
		     const char    *password,
		     FRCompression  compression)
{
}


static void
base_fr_command_delete (FRCommand *comm,
			GList     *file_list)
{
}


static void
base_fr_command_extract (FRCommand  *comm,
			 GList      *file_list,
			 const char *dest_dir,
			 gboolean    overwrite,
			 gboolean    skip_older,
			 gboolean    junk_paths,
			 const char *password)
{
}


static void
base_fr_command_test (FRCommand   *comm,
		      const char  *password)
{
}


static void
base_fr_command_uncompress (FRCommand *comm)
{
}


static void
base_fr_command_recompress (FRCommand     *comm,
			    FRCompression  compression)
{
}


static void
base_fr_command_handle_error (FRCommand *comm, 
			      FRProcError *error)
{
}


static void 
fr_command_class_init (FRCommandClass *class)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (class);

        parent_class = g_type_class_peek_parent (class);

	fr_command_signals[START] =
                g_signal_new ("start",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRCommandClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE, 
			      1, G_TYPE_INT);
	fr_command_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRCommandClass, done),
			      NULL, NULL,
			      fr_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT,
			      G_TYPE_POINTER);

        gobject_class->finalize = fr_command_finalize;

	class->list           = base_fr_command_list;
	class->add            = base_fr_command_add;
	class->delete         = base_fr_command_delete;
	class->extract        = base_fr_command_extract;
	class->test           = base_fr_command_test;

	class->uncompress     = base_fr_command_uncompress;
	class->recompress     = base_fr_command_recompress;

	class->handle_error   = base_fr_command_handle_error;

	class->start          = NULL;
	class->done           = NULL;
}


static void 
fr_command_init (FRCommand *comm)
{
	comm->filename = NULL;
	comm->e_filename = NULL;
	comm->file_list = NULL;
}


static void 
fr_command_start (FRProcess *process,
		  gpointer data)
{
	FRCommand *comm = FR_COMMAND (data);
	g_signal_emit (G_OBJECT (comm), 
		       fr_command_signals[START], 
		       0,
		       comm->action);
}


static void 
fr_command_done (FRProcess *process,
		 FRProcError *error, 
		 gpointer data)
{
	FRCommand *comm = FR_COMMAND (data);

	if (error->type != FR_PROC_ERROR_NONE) 
		fr_command_handle_error (comm, error);

	g_signal_emit (G_OBJECT (comm),
		       fr_command_signals[DONE], 
		       0,
		       comm->action, 
		       error);
}


void
fr_command_construct (FRCommand *comm,
		      FRProcess *process,
		      const char *fr_command_name)
{
	fr_command_set_filename (comm, fr_command_name);

	g_object_ref (G_OBJECT (process));
	comm->process = process;
	g_signal_connect (G_OBJECT (comm->process), 
			  "start",
			  G_CALLBACK (fr_command_start),
			  comm);
	g_signal_connect (G_OBJECT (comm->process), 
			  "done",
			  G_CALLBACK (fr_command_done),
			  comm);
}


static void 
fr_command_finalize (GObject *object)
{
        FRCommand* comm;

        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND (object));
  
	comm = FR_COMMAND (object);

	if (comm->filename != NULL)
		g_free (comm->filename);

	if (comm->e_filename != NULL)
		g_free (comm->e_filename);

	if (comm->file_list != NULL) {
		g_list_foreach (comm->file_list, 
				(GFunc) file_data_free, 
				NULL);
		g_list_free (comm->file_list);
	}

	g_signal_handlers_disconnect_matched (G_OBJECT (comm->process), 
					      G_SIGNAL_MATCH_DATA, 
					      0, 
					      0, NULL, 
					      0,
					      comm);
	g_object_unref (G_OBJECT (comm->process));

	/* Chain up */
        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


void
fr_command_set_filename (FRCommand *comm,
			 const char *filename)
{
	g_return_if_fail (FR_IS_COMMAND (comm));

	if (comm->filename != NULL)
		g_free (comm->filename);

	if (comm->e_filename != NULL)
		g_free (comm->e_filename);

	if (! g_path_is_absolute (filename)) {
		char *current_dir;
		current_dir = g_get_current_dir ();
		comm->filename = g_strconcat (current_dir, 
					      "/", 
					      filename, 
					      NULL);
		g_free (current_dir);
	} else 
		comm->filename = g_strdup (filename);

	comm->e_filename = shell_escape (comm->filename);

#ifdef DEBUG
	g_print ("filename : %s\n", comm->filename);
	g_print ("e_filename : %s\n", comm->e_filename);
#endif
}


void
fr_command_list (FRCommand *comm)
{
	g_return_if_fail (FR_IS_COMMAND (comm));

	if (comm->file_list != NULL) {
		g_list_foreach (comm->file_list, (GFunc) file_data_free, NULL);
		g_list_free (comm->file_list);
		comm->file_list = NULL;
	}

	comm->action = FR_ACTION_LIST;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->list (comm);
}


void
fr_command_add (FRCommand     *comm,
		GList         *file_list,
		const char    *base_dir,
		gboolean       update,
		const char    *password,
		FRCompression  compression)
{
	comm->action = FR_ACTION_ADD;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->add (comm, 
						     file_list,
						     base_dir,
						     update,
						     password,
						     compression);
}


void
fr_command_delete (FRCommand *comm,
		   GList *file_list)
{
	comm->action = FR_ACTION_DELETE;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->delete (comm, file_list);
}


void
fr_command_extract (FRCommand  *comm,
		    GList      *file_list,
		    const char *dest_dir,
		    gboolean    overwrite,
		    gboolean    skip_older,
		    gboolean    junk_paths,
		    const char *password)
{
	comm->action = FR_ACTION_EXTRACT;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->extract (comm, 
							 file_list, 
							 dest_dir,
							 overwrite,
							 skip_older,
							 junk_paths,
							 password);
}


void
fr_command_test (FRCommand   *comm,
		 const char  *password)
{
	comm->action = FR_ACTION_TEST;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->test (comm, password);
}


void
fr_command_uncompress (FRCommand *comm)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->uncompress (comm);
}


void
fr_command_recompress (FRCommand     *comm,
		       FRCompression  compression)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->recompress (comm, compression);
}


void
fr_command_handle_error (FRCommand *comm,
			 FRProcError *error)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->handle_error (comm, error);
}
