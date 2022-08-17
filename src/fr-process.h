/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2008 Free Software Foundation, Inc.
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

#ifndef FR_PROCESS_H
#define FR_PROCESS_H

#include <glib.h>
#include <gio/gio.h>
#include <sys/types.h>
#include "fr-error.h"
#include "typedefs.h"

#define FR_TYPE_PROCESS (fr_process_get_type ())
G_DECLARE_FINAL_TYPE (FrProcess, fr_process, FR, PROCESS, GObject)

typedef void     (*FrProcFunc)     (gpointer data);
typedef gboolean (*FrContinueFunc) (FrError **error, gpointer data);
typedef void     (*FrLineFunc)     (char *line, gpointer data);

typedef struct {
	GIOChannel *source;
	GList      *raw;
	FrLineFunc    line_func;
	gpointer    line_data;
	GIOStatus   status;
	GError     *error;
} FrChannelData;

struct _FrProcess {
	GObject  __parent;

	FrChannelData     out;
	FrChannelData     err;
	/* whether to restart the process after an error. */
	gboolean          restart;
};

FrProcess * fr_process_new                  (void);
void        fr_process_clear                (FrProcess            *fr_proc);
void        fr_process_begin_command        (FrProcess            *fr_proc,
					     const char           *arg);
void        fr_process_begin_command_at     (FrProcess            *fr_proc,
					     const char           *arg,
					     int                   index);
void        fr_process_add_arg              (FrProcess            *fr_proc,
					     const char           *arg);
void        fr_process_add_arg_concat       (FrProcess            *fr_proc,
					     const char           *arg,
					     ...) G_GNUC_NULL_TERMINATED;
void        fr_process_add_arg_printf       (FrProcess            *fr_proc,
					     const char           *format,
					     ...) G_GNUC_PRINTF (2, 3);
void        fr_process_add_arg_file         (FrProcess            *process,
					     GFile                *file);
void        fr_process_set_arg_at           (FrProcess            *fr_proc,
					     int                   n_comm,
					     int                   n_arg,
					     const char           *arg);
void        fr_process_set_begin_func       (FrProcess            *fr_proc,
					     FrProcFunc              func,
					     gpointer              func_data);
void        fr_process_set_end_func         (FrProcess            *fr_proc,
					     FrProcFunc              func,
					     gpointer              func_data);
void        fr_process_set_continue_func    (FrProcess            *fr_proc,
					     FrContinueFunc          func,
					     gpointer              func_data);
void        fr_process_end_command          (FrProcess            *fr_proc);
void        fr_process_set_working_dir      (FrProcess            *fr_proc,
					     const char           *arg);
void        fr_process_set_working_dir_file (FrProcess            *fr_proc,
					     GFile                *folder);
void        fr_process_set_sticky           (FrProcess            *fr_proc,
					     gboolean              sticky);
void        fr_process_set_ignore_error     (FrProcess            *fr_proc,
					     gboolean              ignore_error);
void        fr_process_use_standard_locale  (FrProcess            *fr_proc,
					     gboolean              use_stand_locale);
void        fr_process_set_out_line_func    (FrProcess            *fr_proc,
					     FrLineFunc              func,
					     gpointer              func_data);
void        fr_process_set_err_line_func    (FrProcess            *fr_proc,
					     FrLineFunc              func,
					     gpointer              func_data);
void        fr_process_execute              (FrProcess            *process,
					     GCancellable         *cancellable,
					     GAsyncReadyCallback   callback,
					     gpointer              user_data);
gboolean    fr_process_execute_finish       (FrProcess            *process,
					     GAsyncResult         *result,
					     FrError             **error);
void        fr_process_restart              (FrProcess            *process);
void        fr_process_cancel               (FrProcess            *process);

#endif /* FR_PROCESS_H */
