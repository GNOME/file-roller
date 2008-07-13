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
#include "glib-utils.h"
#include "fr-process.h"
#include "fr-command.h"
#include "fr-marshal.h"

#define INITIAL_SIZE 256

/* Signals */
enum {
	START,
	DONE,
	PROGRESS,
	MESSAGE,
	LAST_SIGNAL
};

/* Properties */
enum {
        PROP_0,
        PROP_FILENAME,
        PROP_MIME_TYPE,
        PROP_PROCESS
};

static GObjectClass *parent_class = NULL;
static guint fr_command_signals[LAST_SIGNAL] = { 0 };

static void fr_command_class_init  (FrCommandClass *class);
static void fr_command_init        (FrCommand *afile);
static void fr_command_finalize    (GObject *object);

char *action_names[] = { "NONE",
			 "CREATING_NEW_ARCHIVE",
			 "LOADING_ARCHIVE",
			 "LISTING_CONTENT",
			 "DELETING_FILES",
			 "TESTING_ARCHIVE",
			 "GETTING_FILE_LIST",
			 "COPYING_FILES_FROM_REMOTE",
			 "ADDING_FILES",
			 "EXTRACTING_FILES",
			 "COPYING_FILES_TO_REMOTE",
			 "CREATING_ARCHIVE",
			 "SAVING_REMOTE_ARCHIVE" };

GType
fr_command_get_type ()
{
	static GType type = 0;

	if (! type) {
		GTypeInfo type_info = {
			sizeof (FrCommandClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_class_init,
			NULL,
			NULL,
			sizeof (FrCommand),
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
base_fr_command_list (FrCommand  *comm,
		      const char *password)
{
}


static void
base_fr_command_add (FrCommand     *comm,
		     GList         *file_list,
		     const char    *base_dir,
		     gboolean       update,
		     gboolean       recursive,
		     const char    *password,
		     FrCompression  compression)
{
}


static void
base_fr_command_delete (FrCommand *comm,
			GList     *file_list)
{
}


static void
base_fr_command_extract (FrCommand  *comm,
			 GList      *file_list,
			 const char *dest_dir,
			 gboolean    overwrite,
			 gboolean    skip_older,
			 gboolean    junk_paths,
			 const char *password)
{
}


static void
base_fr_command_test (FrCommand   *comm,
		      const char  *password)
{
}


static void
base_fr_command_uncompress (FrCommand *comm)
{
}


static void
base_fr_command_recompress (FrCommand     *comm,
			    FrCompression  compression)
{
}


static void
base_fr_command_handle_error (FrCommand *comm,
			      FrProcError *error)
{
}


const char **void_mime_types = { NULL };


const char **
base_fr_command_get_mime_types (FrCommand *comm)
{
	return void_mime_types;
}


FrCommandCap
base_fr_command_get_capabilities (FrCommand  *comm,
			          const char *mime_type)
{
	return FR_COMMAND_CAN_DO_NOTHING;
}


static void
base_fr_command_set_mime_type (FrCommand  *comm,
			       const char *mime_type)
{
	comm->mime_type = get_static_string (mime_type);
	comm->capabilities = fr_command_get_capabilities (comm, comm->mime_type);
}


static void
fr_command_start (FrProcess *process,
		  gpointer   data)
{
	FrCommand *comm = FR_COMMAND (data);

	g_signal_emit (G_OBJECT (comm),
		       fr_command_signals[START],
		       0,
		       comm->action);
}


static void
fr_command_done (FrProcess   *process,
		 gpointer     data)
{
	FrCommand *comm = FR_COMMAND (data);

	comm->process->restart = FALSE;
	if (process->error.type != FR_PROC_ERROR_NONE)
		fr_command_handle_error (comm, &process->error);

	if (comm->process->restart)
		fr_process_start (comm->process);
	else
		g_signal_emit (G_OBJECT (comm),
			       fr_command_signals[DONE],
			       0,
			       comm->action,
			       &process->error);
}


static void
fr_command_set_process (FrCommand  *comm,
		        FrProcess  *process)
{
	if (comm->process != NULL) {
		g_signal_handlers_disconnect_matched (G_OBJECT (comm->process),
					      G_SIGNAL_MATCH_DATA,
					      0,
					      0, NULL,
					      0,
					      comm);
		g_object_unref (G_OBJECT (comm->process));
		comm->process = NULL;
	}

	if (process == NULL)
		return;

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
fr_command_set_property (GObject      *object,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
        FrCommand *comm;

        comm = FR_COMMAND (object);

        switch (prop_id) {
        case PROP_PROCESS:
                fr_command_set_process (comm, g_value_get_object (value));
                break;
        case PROP_FILENAME:
        	fr_command_set_filename (comm, g_value_get_string (value));
                break;
        case PROP_MIME_TYPE:
        	fr_command_set_mime_type (comm, g_value_get_string (value));
                break;
        default:
                break;
        }
}


static void
fr_command_get_property (GObject    *object,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
        FrCommand *comm;

        comm = FR_COMMAND (object);

        switch (prop_id) {
        case PROP_PROCESS:
                g_value_set_object (value, comm->process);
                break;
        case PROP_FILENAME:
        	g_value_set_string (value, comm->filename);
                break;
        case PROP_MIME_TYPE:
                g_value_set_static_string (value, comm->mime_type);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}


static void
fr_command_class_init (FrCommandClass *class)
{
	GObjectClass *gobject_class;

	parent_class = g_type_class_peek_parent (class);

	gobject_class = G_OBJECT_CLASS (class);

	/* virtual functions */

	gobject_class->finalize = fr_command_finalize;
	gobject_class->set_property = fr_command_set_property;
        gobject_class->get_property = fr_command_get_property;

	class->list             = base_fr_command_list;
	class->add              = base_fr_command_add;
	class->delete           = base_fr_command_delete;
	class->extract          = base_fr_command_extract;
	class->test             = base_fr_command_test;
	class->uncompress       = base_fr_command_uncompress;
	class->recompress       = base_fr_command_recompress;
	class->handle_error     = base_fr_command_handle_error;
	class->get_mime_types   = base_fr_command_get_mime_types;
	class->get_capabilities = base_fr_command_get_capabilities;
	class->set_mime_type    = base_fr_command_set_mime_type;
	class->start            = NULL;
	class->done             = NULL;
	class->progress         = NULL;
	class->message          = NULL;

	/* signals */

	fr_command_signals[START] =
		g_signal_new ("start",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrCommandClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);
	fr_command_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrCommandClass, done),
			      NULL, NULL,
			      fr_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT,
			      G_TYPE_POINTER);
	fr_command_signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrCommandClass, progress),
			      NULL, NULL,
			      fr_marshal_VOID__DOUBLE,
			      G_TYPE_NONE, 1,
			      G_TYPE_DOUBLE);
	fr_command_signals[MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrCommandClass, message),
			      NULL, NULL,
			      fr_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	/* properties */

	g_object_class_install_property (gobject_class,
					 PROP_PROCESS,
					 g_param_spec_object ("process",
							      "Processs",
							      "The process object used by the command",
							      FR_TYPE_PROCESS,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_FILENAME,
					 g_param_spec_string ("filename",
							      "Filename",
							      "The archive filename",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_MIME_TYPE,
					 g_param_spec_string ("mime-type",
							      "Mime type",
							      "The file mime-type",
							      NULL,
							      G_PARAM_READWRITE));
}


static void
fr_command_init (FrCommand *comm)
{
	comm->files = g_ptr_array_sized_new (INITIAL_SIZE);

	comm->filename = NULL;
	comm->e_filename = NULL;
	comm->fake_load = FALSE;

	comm->propAddCanUpdate = FALSE;
	comm->propAddCanReplace = FALSE;
	comm->propAddCanStoreFolders = FALSE;
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder = FALSE;
	comm->propExtractCanJunkPaths = FALSE;
	comm->propPassword = FALSE;
	comm->propTest = FALSE;
	comm->propCanExtractAll = TRUE;
	comm->propCanDeleteNonEmptyFolders = TRUE;
	comm->propCanExtractNonEmptyFolders = TRUE;
}


static void
fr_command_finalize (GObject *object)
{
	FrCommand* comm;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND (object));

	comm = FR_COMMAND (object);

	if (comm->filename != NULL)
		g_free (comm->filename);
	if (comm->e_filename != NULL)
		g_free (comm->e_filename);
	if (comm->files != NULL)
		g_ptr_array_free_full (comm->files, (GFunc) file_data_free, NULL);
	fr_command_set_process (comm, NULL);

	/* Chain up */
	if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


void
fr_command_set_filename (FrCommand  *comm,
			 const char *filename)
{
	g_return_if_fail (FR_IS_COMMAND (comm));

	if (comm->filename != NULL) {
		g_free (comm->filename);
		comm->filename = NULL;
	}

	if (comm->e_filename != NULL) {
		g_free (comm->e_filename);
		comm->e_filename = NULL;
	}

	if (filename == NULL)
		return;

	if (! g_path_is_absolute (filename)) {
		char *current_dir;

		current_dir = g_get_current_dir ();
		comm->filename = g_strconcat (current_dir,
					      "/",
					      filename,
					      NULL);
		g_free (current_dir);
	}
	else
		comm->filename = g_strdup (filename);

	comm->e_filename = g_shell_quote (comm->filename);

	debug (DEBUG_INFO, "filename : %s\n", comm->filename);
	debug (DEBUG_INFO, "e_filename : %s\n", comm->e_filename);
}


void
fr_command_list (FrCommand  *comm,
		 const char *password)
{
	g_return_if_fail (FR_IS_COMMAND (comm));

	fr_command_progress (comm, -1.0);

	if (comm->files != NULL) {
		g_ptr_array_free_full (comm->files, (GFunc) file_data_free, NULL);
		comm->files = g_ptr_array_sized_new (INITIAL_SIZE);
	}

	comm->action = FR_ACTION_LISTING_CONTENT;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_use_standard_locale (FR_COMMAND (comm)->process, TRUE);

	if (!comm->fake_load)
		FR_COMMAND_GET_CLASS (G_OBJECT (comm))->list (comm, password);
}


void
fr_command_add (FrCommand     *comm,
		GList         *file_list,
		const char    *base_dir,
		gboolean       update,
		gboolean       recursive,
		const char    *password,
		gboolean       encrypt_header,
		FrCompression  compression)
{
	fr_command_progress (comm, -1.0);

	comm->encrypt_header = encrypt_header;
	comm->action = FR_ACTION_ADDING_FILES;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->add (comm,
						     file_list,
						     base_dir,
						     update,
						     recursive,
						     password,
						     compression);
}


void
fr_command_delete (FrCommand *comm,
		   GList *file_list)
{
	fr_command_progress (comm, -1.0);

	comm->action = FR_ACTION_DELETING_FILES;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->delete (comm, file_list);
}


void
fr_command_extract (FrCommand  *comm,
		    GList      *file_list,
		    const char *dest_dir,
		    gboolean    overwrite,
		    gboolean    skip_older,
		    gboolean    junk_paths,
		    const char *password)
{
	fr_command_progress (comm, -1.0);

	comm->action = FR_ACTION_EXTRACTING_FILES;
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
fr_command_test (FrCommand   *comm,
		 const char  *password)
{
	fr_command_progress (comm, -1.0);

	comm->action = FR_ACTION_TESTING_ARCHIVE;
	fr_process_set_out_line_func (FR_COMMAND (comm)->process, NULL, NULL);
	fr_process_set_err_line_func (FR_COMMAND (comm)->process, NULL, NULL);

	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->test (comm, password);
}


void
fr_command_uncompress (FrCommand *comm)
{
	fr_command_progress (comm, -1.0);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->uncompress (comm);
}


void
fr_command_recompress (FrCommand     *comm,
		       FrCompression  compression)
{
	fr_command_progress (comm, -1.0);
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->recompress (comm, compression);
}


const char **
fr_command_get_mime_types (FrCommand *comm)
{
	return FR_COMMAND_GET_CLASS (G_OBJECT (comm))->get_mime_types (comm);
}


FrCommandCap
fr_command_get_capabilities (FrCommand  *comm,
			     const char *mime_type)
{
	return FR_COMMAND_GET_CLASS (G_OBJECT (comm))->get_capabilities (comm, mime_type);
}


gboolean
fr_command_is_capable_of (FrCommand     *comm,
			  FrCommandCaps  requested_capabilities)
{
	return (((comm->capabilities ^ requested_capabilities) & requested_capabilities) == 0);
}


/* fraction == -1 means : I don't known how much time the current operation
 *                        will take, the dialog will display this info pulsing
 *                        the progress bar.
 * fraction in [0.0, 1.0] means the amount of work, in percentage,
 *                        accomplished.
 */
void
fr_command_progress (FrCommand *comm,
		     double     fraction)
{
	g_signal_emit (G_OBJECT (comm),
		       fr_command_signals[PROGRESS],
		       0,
		       fraction);
}


void
fr_command_message (FrCommand  *comm,
		    const char *msg)
{
	g_signal_emit (G_OBJECT (comm),
		       fr_command_signals[MESSAGE],
		       0,
		       msg);
}


void
fr_command_set_n_files (FrCommand *comm,
			int        n_files)
{
	comm->n_files = n_files;
	comm->n_file = 0;
}


void
fr_command_add_file (FrCommand *comm,
		     FileData  *fdata)
{
	file_data_update_content_type (fdata);
	g_ptr_array_add (comm->files, fdata);
}


void
fr_command_set_mime_type (FrCommand  *comm,
			  const char *mime_type)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->set_mime_type (comm, mime_type);
}


void
fr_command_handle_error (FrCommand *comm,
			 FrProcError *error)
{
	FR_COMMAND_GET_CLASS (G_OBJECT (comm))->handle_error (comm, error);
}
