/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>
#include "fr-process.h"
#include "fr-marshal.h"

#define REFRESH_RATE 10

enum {
	START,
	DONE,
	STICKY_ONLY,
	LAST_SIGNAL
};

static GObjectClass *parent_class;
static guint fr_process_signals[LAST_SIGNAL] = { 0 };

static void fr_process_class_init (FRProcessClass *class);
static void fr_process_init       (FRProcess      *fr_proc);
static void fr_process_finalize   (GObject        *object);


static FRCommandInfo *
fr_command_info_new ()
{
	FRCommandInfo *c_info;

	c_info = g_new0 (FRCommandInfo, 1);
	c_info->args = NULL;
	c_info->dir = NULL;
	c_info->sticky = FALSE;
	c_info->ignore_error = FALSE;

	return c_info;
}


static void
fr_command_info_free (FRCommandInfo * c_info)
{
	if (c_info == NULL)
		return;

	if (c_info->args != NULL) {
		g_list_foreach (c_info->args, (GFunc) g_free, NULL);
		g_list_free (c_info->args);
		c_info->args = NULL;
	}

	if (c_info->dir != NULL) {
		g_free (c_info->dir);
		c_info->dir = NULL;
	}

	g_free (c_info);
}


GType
fr_process_get_type (void)
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FRProcessClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_process_class_init,
			NULL,
			NULL,
			sizeof (FRProcess),
			0,
			(GInstanceInitFunc) fr_process_init
                };

		type = g_type_register_static (G_TYPE_OBJECT,
					       "FRProcess",
					       &type_info,
					       0);
        }

        return type;
}


static void
fr_process_class_init (FRProcessClass *class)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (class);

        parent_class = g_type_class_peek_parent (class);

	fr_process_signals[START] =
		g_signal_new ("start",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRProcessClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	fr_process_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRProcessClass, done),
			      NULL, NULL,
			      fr_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	fr_process_signals[STICKY_ONLY] =
		g_signal_new ("sticky_only",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FRProcessClass, sticky_only),
			      NULL, NULL,
			      fr_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gobject_class->finalize = fr_process_finalize;

        class->start = NULL;
        class->done  = NULL;
}


static void
fr_process_init (FRProcess *fr_proc)
{
	fr_proc->term_on_stop = TRUE;

	fr_proc->comm = g_ptr_array_new ();
	fr_proc->n_comm = -1;
	fr_proc->current_comm = -1;

	fr_proc->command_pid = 0;
	fr_proc->output_fd = 0;
	fr_proc->error_fd = 0;

	fr_proc->o_buffer = g_new (char, BUFFER_SIZE + 1);
	fr_proc->e_buffer = g_new (char, BUFFER_SIZE + 1);

	fr_proc->log_timeout = 0;
	fr_proc->o_not_processed = 0;
	fr_proc->e_not_processed = 0;
	fr_proc->raw_output = NULL;
	fr_proc->raw_error = NULL;

	fr_proc->o_proc_line_func = NULL;
	fr_proc->o_proc_line_data = NULL;

	fr_proc->e_proc_line_func = NULL;
	fr_proc->e_proc_line_data = NULL;

	fr_proc->error.gerror = NULL;
	fr_proc->first_error.gerror = NULL;

	fr_proc->running = FALSE;
	fr_proc->stopping = FALSE;
	fr_proc->restart = FALSE;

	fr_proc->use_standard_locale = FALSE;
}


FRProcess * 
fr_process_new ()
{
	return FR_PROCESS (g_object_new (FR_TYPE_PROCESS, NULL));
}


static void _fr_process_stop (FRProcess *fr_proc, gboolean emit_signal);


static void
fr_process_finalize (GObject *object)
{
	FRProcess *fr_proc;

	g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_PROCESS (object));

	fr_proc = FR_PROCESS (object);

	_fr_process_stop (fr_proc, FALSE);
	fr_process_clear (fr_proc);
	g_ptr_array_free (fr_proc->comm, FALSE);

	if (fr_proc->raw_output != NULL) {
		g_list_foreach (fr_proc->raw_output, (GFunc) g_free, NULL);
		g_list_free (fr_proc->raw_output);
		fr_proc->raw_output = NULL;
	}

	if (fr_proc->raw_error != NULL) {
		g_list_foreach (fr_proc->raw_error, (GFunc) g_free, NULL);
		g_list_free (fr_proc->raw_error);
		fr_proc->raw_error = NULL;
	}

	g_free (fr_proc->o_buffer);
	g_free (fr_proc->e_buffer);

	g_clear_error (&fr_proc->error.gerror);
	g_clear_error (&fr_proc->first_error.gerror);

	/* Chain up */

        if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


void
fr_process_begin_command (FRProcess  *fr_proc, 
			  const char *arg)
{
	FRCommandInfo * c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = fr_command_info_new ();
	c_info->args = g_list_prepend (NULL, g_strdup (arg));
	g_ptr_array_add (fr_proc->comm, c_info);
	fr_proc->n_comm++;
	fr_proc->current_comm = fr_proc->n_comm;
}


void
fr_process_begin_command_at (FRProcess  *fr_proc, 
			     const char *arg,
			     int         index)
{
	FRCommandInfo *c_info, *old_c_info;

	g_return_if_fail (fr_proc != NULL);
	g_return_if_fail (index >= 0 && index <= fr_proc->n_comm);

	fr_proc->current_comm = index;

	old_c_info = g_ptr_array_index (fr_proc->comm, index);

	if (old_c_info != NULL)
		fr_command_info_free (old_c_info);

	c_info = fr_command_info_new ();
	c_info->args = g_list_prepend (NULL, g_strdup (arg));
	g_ptr_array_index (fr_proc->comm, index) = c_info;
}


void
fr_process_set_working_dir (FRProcess  *fr_proc, 
			    const char *dir)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	if (c_info->dir != NULL)
		g_free (c_info->dir);
	c_info->dir = g_strdup (dir);
}


void
fr_process_set_sticky (FRProcess *fr_proc, 
		       gboolean   sticky)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->sticky = sticky;
}


void
fr_process_set_ignore_error (FRProcess *fr_proc, 
			     gboolean   ignore_error)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->ignore_error = ignore_error;
}


void
fr_process_add_arg (FRProcess  *fr_proc, 
		    const char *arg)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->args = g_list_prepend (c_info->args, g_strdup (arg));
}


void
fr_process_set_arg_at (FRProcess  *fr_proc, 
		       int         n_comm,
		       int         n_arg,
		       const char *arg_value)
{
	FRCommandInfo *c_info;
	GList         *arg;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, n_comm);
	arg = g_list_nth (c_info->args, n_arg);
	g_return_if_fail (arg != NULL);
	
	g_free (arg->data);
	arg->data = g_strdup (arg_value);
}


void
fr_process_set_begin_func (FRProcess    *fr_proc, 
			   ProcFunc      func,
			   gpointer      func_data)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->begin_func = func;
	c_info->begin_data = func_data;
}


void
fr_process_set_end_func (FRProcess    *fr_proc, 
			 ProcFunc      func,
			 gpointer      func_data)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->end_func = func;
	c_info->end_data = func_data;
}


void
fr_process_end_command (FRProcess *fr_proc)
{
	FRCommandInfo *c_info;

	g_return_if_fail (fr_proc != NULL);

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_comm);
	c_info->args = g_list_reverse (c_info->args);
}


void
fr_process_clear (FRProcess *fr_proc)
{
	gint i;

	g_return_if_fail (fr_proc != NULL);

	for (i = 0; i <= fr_proc->n_comm; i++) {
		FRCommandInfo *c_info;

		c_info = g_ptr_array_index (fr_proc->comm, i);
		fr_command_info_free (c_info);
		g_ptr_array_index (fr_proc->comm, i) = NULL;
	}

	for (i = 0; i <= fr_proc->n_comm; i++) 
		g_ptr_array_remove_index_fast (fr_proc->comm, 0);

	fr_proc->n_comm = -1;
	fr_proc->current_comm = -1;
}


void
fr_process_set_out_line_func (FRProcess    *fr_proc, 
			      ProcLineFunc  func,
			      gpointer      data)
{
	g_return_if_fail (fr_proc != NULL);
	fr_proc->o_proc_line_func = func;
	fr_proc->o_proc_line_data = data;
}


void
fr_process_set_err_line_func (FRProcess    *fr_proc, 
			      ProcLineFunc  func,
			      gpointer      data)
{
	g_return_if_fail (fr_proc != NULL);
	fr_proc->e_proc_line_func = func;
	fr_proc->e_proc_line_data = data;
}


static gboolean
process_output (FRProcess *fr_proc)
{
	int   n, i;
	char *line, *eol;

 again:
	n = read (fr_proc->output_fd, 
		  fr_proc->o_buffer + fr_proc->o_not_processed, 
		  BUFFER_SIZE - fr_proc->o_not_processed);

	if ((n < 0) && (errno == EINTR))
		goto again;

	if (n <= 0)
		return FALSE;

	fr_proc->o_buffer[fr_proc->o_not_processed + n] = 0;

	line = fr_proc->o_buffer;
	while ((eol = strchr (line, '\n')) != NULL) {
		*eol = 0;

		fr_proc->raw_output = g_list_prepend (fr_proc->raw_output, 
						      g_strdup (line));

		if (fr_proc->o_proc_line_func != NULL)
			(*fr_proc->o_proc_line_func) (line, fr_proc->o_proc_line_data);

		line = eol + 1;
	}
	
	/* shift unprocessed text to the beginning. */

	fr_proc->o_not_processed = strlen (line);
	for (i = 0; *line != 0; line++, i++)
		fr_proc->o_buffer[i] = *line;

	return TRUE;
}


static gboolean
process_error (FRProcess *fr_proc)
{
	int   n, i;
	char *line, *eol;

 again:
	n = read (fr_proc->error_fd, 
		  fr_proc->e_buffer + fr_proc->e_not_processed, 
		  BUFFER_SIZE - fr_proc->e_not_processed);

	if ((n < 0) && (errno == EINTR))
		goto again;

	if (n <= 0)
		return FALSE;

	fr_proc->e_buffer[fr_proc->e_not_processed + n] = 0;

	line = fr_proc->e_buffer;
	while ((eol = strchr (line, '\n')) != NULL) {
		*eol = 0;

		fr_proc->raw_error = g_list_prepend (fr_proc->raw_error, 
						     g_strdup (line));

		if (fr_proc->e_proc_line_func != NULL)
			(*fr_proc->e_proc_line_func) (line, fr_proc->e_proc_line_data);

		line = eol + 1;
	}
	
	/* shift unprocessed text to the beginning. */

	fr_proc->e_not_processed = strlen (line);
	for (i = 0; *line != 0; line++, i++)
		fr_proc->e_buffer[i] = *line;

	return TRUE;
}


static gboolean check_child (gpointer data);


static void child_setup (gpointer user_data)
{
	FRProcess *fr_proc = user_data;

	if (fr_proc->use_standard_locale)
		putenv ("LC_ALL=C");
}


static void
start_current_command (FRProcess *fr_proc)
{
	FRCommandInfo  *c_info;
	GList          *arg_list, *scan;
	GString        *command;
	char           *dir;
	char          **argv;
	int             i = 0;

#ifdef DEBUG
	g_print ("%d/%d) ", fr_proc->current_command, fr_proc->n_comm);
#endif
	
	c_info = g_ptr_array_index (fr_proc->comm, 
				    fr_proc->current_command);
	arg_list = c_info->args;
	dir = c_info->dir;

	if (dir != NULL) {
#ifdef DEBUG
		g_print ("cd %s\n", dir); 
#endif
	}
	
	command = NULL;
	
	argv = g_new (char *, 4);
	argv[i++] = "/bin/sh";
	argv[i++] = "-c";

	command = g_string_new ("");
	for (scan = arg_list; scan; scan = scan->next) {
		g_string_append (command, scan->data);
		if (scan->next != NULL)
			g_string_append_c (command, ' ');
	}
	
	argv[i++] = command->str;
	argv[i] = NULL;
	
#ifdef DEBUG
	{
		int j;
		
		g_print ("/bin/sh "); 
		for (j = 0; j < i; j++)
			g_print ("%s ", argv[j]);
		g_print ("\n"); 
	}
#endif

	if (c_info->begin_func != NULL)
		(*c_info->begin_func) (c_info->begin_data);

	if (! g_spawn_async_with_pipes (dir,
					argv,
					NULL,
					(G_SPAWN_LEAVE_DESCRIPTORS_OPEN
					 | G_SPAWN_SEARCH_PATH 
					 | G_SPAWN_DO_NOT_REAP_CHILD),
					child_setup,
					fr_proc,
					&fr_proc->command_pid,
					NULL,
					&fr_proc->output_fd,
					&fr_proc->error_fd,
					&fr_proc->error.gerror)) {
		fr_proc->error.type = FR_PROC_ERROR_SPAWN;
		g_signal_emit (G_OBJECT (fr_proc), 
			       fr_process_signals[DONE],
			       0,
			       &fr_proc->error);

		g_free (argv);
		g_string_free (command, TRUE);

		return;
	}

	g_free (argv);
	g_string_free (command, TRUE);

	fcntl (fr_proc->output_fd, F_SETFL, O_NONBLOCK);
	fcntl (fr_proc->error_fd, F_SETFL, O_NONBLOCK);

	fr_proc->o_not_processed = 0;
	fr_proc->e_not_processed = 0;
	fr_proc->log_timeout = g_timeout_add (REFRESH_RATE, 
					      check_child,
					      fr_proc);
}


static gboolean
command_is_sticky (FRProcess *fr_proc, 
		   int        i)
{
	FRCommandInfo *c_info;
	c_info = g_ptr_array_index (fr_proc->comm, i);
	return c_info->sticky;
}


static void
allow_sticky_processes_only (FRProcess *fr_proc,
			     gboolean   emit_signal) 
{
	if (! fr_proc->sticky_only) {
		/* Remember the first error. */
		fr_proc->error_command = fr_proc->current_command;
		fr_proc->first_error.type = fr_proc->error.type;
		fr_proc->first_error.status = fr_proc->error.status;
		g_clear_error (&fr_proc->first_error.gerror);
		if (fr_proc->error.gerror != NULL)
			fr_proc->first_error.gerror = g_error_copy (fr_proc->error.gerror);
	}
	
	fr_proc->sticky_only = TRUE;
	if (emit_signal)
		g_signal_emit (G_OBJECT (fr_proc), 
			       fr_process_signals[STICKY_ONLY],
			       0);
}


static gint
check_child (gpointer data)
{
	FRProcess      *fr_proc = data;
	FRCommandInfo  *c_info;
	pid_t           pid;
	int             status;

	c_info = g_ptr_array_index (fr_proc->comm, fr_proc->current_command);

	/* Remove check. */

	g_source_remove (fr_proc->log_timeout);	
	fr_proc->log_timeout = 0;

	process_output (fr_proc);
	process_error (fr_proc);

	pid = waitpid (fr_proc->command_pid, &status, WNOHANG);
	if (pid != fr_proc->command_pid) {
		/* Add check again. */
		fr_proc->log_timeout = g_timeout_add (REFRESH_RATE, 
						      check_child,
						      fr_proc);
		return FALSE;
	}

	if (c_info->ignore_error) {
		fr_proc->error.type = FR_PROC_ERROR_NONE;
		g_print ("[ignore error]\n");
	}

	if (fr_proc->error.type != FR_PROC_ERROR_STOPPED) {
		if (WIFEXITED (status)) {
			if (WEXITSTATUS (status) == 0)
				fr_proc->error.type = FR_PROC_ERROR_NONE;
			else if (WEXITSTATUS (status) == 255) 
				fr_proc->error.type = FR_PROC_ERROR_COMMAND_NOT_FOUND;
			else {
				fr_proc->error.type = FR_PROC_ERROR_GENERIC;
				fr_proc->error.status = WEXITSTATUS (status);
			}
		} else
			fr_proc->error.type = FR_PROC_ERROR_EXITED_ABNORMALLY;
	}

	fr_proc->command_pid = 0;

	/* Read all pending output. */

	if (fr_proc->error.type == FR_PROC_ERROR_NONE) {
		while (process_output (fr_proc)) ;
		while (process_error (fr_proc)) ;
	}

	close (fr_proc->output_fd);
	close (fr_proc->error_fd);

	fr_proc->output_fd = 0;
	fr_proc->error_fd = 0;

	/**/

	if (c_info->end_func != NULL)
		(*c_info->end_func) (c_info->end_data);

	/* Execute next command. */

	if (fr_proc->error.type != FR_PROC_ERROR_NONE) 
		allow_sticky_processes_only (fr_proc, TRUE);

	if (fr_proc->sticky_only) {
		do {
			fr_proc->current_command++;
		} while ((fr_proc->current_command <= fr_proc->n_comm)
			 && ! command_is_sticky (fr_proc, 
						 fr_proc->current_command));
	} else
		fr_proc->current_command++;

	if (fr_proc->current_command <= fr_proc->n_comm) {
		start_current_command (fr_proc);
		return FALSE;
	}

	/* Done */

	fr_proc->current_command = -1;

	if (fr_proc->raw_output != NULL)
		fr_proc->raw_output = g_list_reverse (fr_proc->raw_output);
	if (fr_proc->raw_error != NULL)
		fr_proc->raw_error = g_list_reverse (fr_proc->raw_error);

	fr_proc->running = FALSE;
	fr_proc->stopping = FALSE;

	if (fr_proc->sticky_only) {
		/* Restore the first error. */
		fr_proc->error.type = fr_proc->first_error.type;
		fr_proc->error.status = fr_proc->first_error.status;
		g_clear_error (&fr_proc->error.gerror);
		if (fr_proc->first_error.gerror != NULL)
			fr_proc->error.gerror = g_error_copy (fr_proc->first_error.gerror);
	}

	g_signal_emit (G_OBJECT (fr_proc), 
		       fr_process_signals[DONE],
		       0,
		       &fr_proc->error);

	return FALSE;
}


void
fr_process_use_standard_locale (FRProcess *fr_proc,
				gboolean   use_stand_locale)
{
	g_return_if_fail (fr_proc != NULL);
	fr_proc->use_standard_locale = use_stand_locale;
}


void
fr_process_start (FRProcess *fr_proc)
{
	g_return_if_fail (fr_proc != NULL);

	if (fr_proc->running)
		return;

	if (fr_proc->raw_output != NULL) {
		g_list_foreach (fr_proc->raw_output, (GFunc) g_free, NULL);
		g_list_free (fr_proc->raw_output);
		fr_proc->raw_output = NULL;
	}

	if (fr_proc->raw_error != NULL) {
		g_list_foreach (fr_proc->raw_error, (GFunc) g_free, NULL);
		g_list_free (fr_proc->raw_error);
		fr_proc->raw_error = NULL;
	}

	if (!fr_proc->restart)
		g_signal_emit (G_OBJECT (fr_proc), 
			       fr_process_signals[START],
			       0);

	fr_proc->sticky_only = FALSE;
	fr_proc->current_command = 0;
	fr_proc->error.type = FR_PROC_ERROR_NONE;

	fr_proc->stopping = FALSE;

	if (fr_proc->n_comm == -1) {
		fr_proc->running = FALSE;
		g_signal_emit (G_OBJECT (fr_proc), 
			       fr_process_signals[DONE],
			       0,
			       &fr_proc->error);

	} else {
		fr_proc->running = TRUE;
		start_current_command (fr_proc);
	}
}


static void
_fr_process_stop (FRProcess *fr_proc,
		  gboolean   emit_signal)
{
	g_return_if_fail (fr_proc != NULL);

	if (! fr_proc->running)
		return;

	if (fr_proc->stopping)
		return;

	fr_proc->stopping = TRUE;
	fr_proc->error.type = FR_PROC_ERROR_STOPPED;

	if (command_is_sticky (fr_proc, fr_proc->current_command)) 
		allow_sticky_processes_only (fr_proc, emit_signal);

	else if (fr_proc->term_on_stop) 
		kill (fr_proc->command_pid, SIGTERM);

	else {
		if (fr_proc->log_timeout != 0) {
			g_source_remove (fr_proc->log_timeout);
			fr_proc->log_timeout = 0;
		}
		
		fr_proc->command_pid = 0;

		close (fr_proc->output_fd);
		close (fr_proc->error_fd);

		fr_proc->output_fd = 0;
		fr_proc->error_fd = 0;
		
		fr_proc->running = FALSE;

		if (emit_signal)
			g_signal_emit (G_OBJECT (fr_proc), 
				       fr_process_signals[DONE],
				       0,
				       &fr_proc->error);
	}
}


void
fr_process_stop (FRProcess *fr_proc)
{
	_fr_process_stop (fr_proc, TRUE);
}
