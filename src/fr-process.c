/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2008, 2012 Free Software Foundation, Inc.
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

#include <config.h>
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
#include "file-utils.h"
#include "fr-process.h"
#include "glib-utils.h"

#define REFRESH_RATE 20
#define BUFFER_SIZE 16384


/* -- FrCommandInfo --  */


typedef struct {
	GList        *args;              /* command to execute */
	char         *dir;               /* working directory */
	guint         sticky : 1;        /* whether the command must be
					  * executed even if a previous
					  * command has failed. */
	guint         ignore_error : 1;  /* whether to continue to execute
					  * other commands if this command
					  * fails. */
	FrContinueFunc  continue_func;
	gpointer      continue_data;
	FrProcFunc      begin_func;
	gpointer      begin_data;
	FrProcFunc      end_func;
	gpointer      end_data;
} FrCommandInfo;


static FrCommandInfo *
fr_command_info_new (void)
{
	FrCommandInfo *info;

	info = g_new0 (FrCommandInfo, 1);
	info->args = NULL;
	info->dir = NULL;
	info->sticky = FALSE;
	info->ignore_error = FALSE;

	return info;
}


static void
fr_command_info_free (FrCommandInfo *info)
{
	if (info == NULL)
		return;

	if (info->args != NULL) {
		g_list_free_full (info->args, g_free);
		info->args = NULL;
	}

	if (info->dir != NULL) {
		g_free (info->dir);
		info->dir = NULL;
	}

	g_free (info);
}


/* -- FrChannelData -- */


static void
fr_channel_data_init (FrChannelData *channel)
{
	channel->source = NULL;
	channel->raw = NULL;
	channel->status = G_IO_STATUS_NORMAL;
	channel->error = NULL;
}


static void
fr_channel_data_close_source (FrChannelData *channel)
{
	if (channel->source != NULL) {
		g_io_channel_shutdown (channel->source, FALSE, NULL);
		g_io_channel_unref (channel->source);
		channel->source = NULL;
	}
}


static GIOStatus
fr_channel_data_read (FrChannelData *channel)
{
	char  *line;
	gsize  length;
	gsize  terminator_pos;

	channel->status = G_IO_STATUS_NORMAL;
	g_clear_error (&channel->error);

	while ((channel->status = g_io_channel_read_line (channel->source,
							  &line,
							  &length,
							  &terminator_pos,
							  &channel->error)) == G_IO_STATUS_NORMAL)
	{
		line[terminator_pos] = 0;
		channel->raw = g_list_prepend (channel->raw, line);
		if (channel->line_func != NULL)
			(*channel->line_func) (line, channel->line_data);
	}

	return channel->status;
}


static GIOStatus
fr_channel_data_flush (FrChannelData *channel)
{
	GIOStatus status;

	while (((status = fr_channel_data_read (channel)) != G_IO_STATUS_ERROR) && (status != G_IO_STATUS_EOF))
		/* void */;
	fr_channel_data_close_source (channel);

	return status;
}


static void
fr_channel_data_reset (FrChannelData *channel)
{
	fr_channel_data_close_source (channel);

	if (channel->raw != NULL) {
		g_list_free_full (channel->raw, g_free);
		channel->raw = NULL;
	}
}


static void
fr_channel_data_free (FrChannelData *channel)
{
	fr_channel_data_reset (channel);
}


static void
fr_channel_data_set_fd (FrChannelData *channel,
			int            fd,
			const char    *charset)
{
	fr_channel_data_reset (channel);

	channel->source = g_io_channel_unix_new (fd);
	g_io_channel_set_flags (channel->source, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffer_size (channel->source, BUFFER_SIZE);
	if (charset != NULL)
		g_io_channel_set_encoding (channel->source, charset, NULL);
}


/* -- ExecData -- */


typedef struct {
	FrProcess          *process;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
	gulong              cancel_id;
	int                 error_command;       /* command that caused an error. */
	FrError            *error;
	FrError            *first_error;
	GList              *first_error_stdout;
	GList              *first_error_stderr;
} ExecuteData;


static void
execute_data_free (ExecuteData *exec_data)
{
	if (exec_data == NULL)
		return;

	if (exec_data->cancel_id != 0)
		g_cancellable_disconnect (exec_data->cancellable, exec_data->cancel_id);

	_g_object_unref (exec_data->process);
	_g_object_unref (exec_data->cancellable);
	_g_object_unref (exec_data->result);
	fr_error_free (exec_data->error);
	fr_error_free (exec_data->first_error);
	_g_string_list_free (exec_data->first_error_stdout);
	_g_string_list_free (exec_data->first_error_stderr);
	g_free (exec_data);
}


/* -- FrProcess  -- */


enum {
	STICKY_ONLY,
	LAST_SIGNAL
};


static guint       fr_process_signals[LAST_SIGNAL] = { 0 };
static const char *try_charsets[] = { "UTF-8", "ISO-8859-1", "WINDOWS-1252" };
static int         n_charsets = G_N_ELEMENTS (try_charsets);


typedef struct {
	GPtrArray   *comm;                /* FrCommandInfo elements. */
	gint         n_comm;              /* total number of commands */
	gint         current_comm;        /* currently editing command. */

	GPid         command_pid;
	guint        check_timeout;

	gboolean     running;
	gboolean     stopping;
	gint         current_command;

	gboolean     use_standard_locale;
	gboolean     sticky_only;         /* whether to execute only sticky
			 		   * commands. */
	int          current_charset;

	ExecuteData *exec_data;
} FrProcessPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE (FrProcess, fr_process, G_TYPE_OBJECT)


static void
fr_process_finalize (GObject *object)
{
	FrProcess *process;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_PROCESS (object));

	process = FR_PROCESS (object);

	FrProcessPrivate *private = fr_process_get_instance_private (process);

	execute_data_free (private->exec_data);
	fr_process_clear (process);
	g_ptr_array_free (private->comm, FALSE);
	fr_channel_data_free (&process->out);
	fr_channel_data_free (&process->err);

	if (G_OBJECT_CLASS (fr_process_parent_class)->finalize)
		G_OBJECT_CLASS (fr_process_parent_class)->finalize (object);
}


static void
fr_process_class_init (FrProcessClass *klass)
{
	GObjectClass *gobject_class;

	fr_process_parent_class = g_type_class_peek_parent (klass);

	fr_process_signals[STICKY_ONLY] =
		g_signal_newv ("sticky_only",
		               G_TYPE_FROM_CLASS (klass),
		               G_SIGNAL_RUN_LAST,
		               /* class_closure = */ NULL,
		               NULL, NULL,
		               g_cclosure_marshal_VOID__VOID,
		               G_TYPE_NONE, 0,
		               NULL);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_process_finalize;
}


static void
fr_process_init (FrProcess *process)
{
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	private->comm = g_ptr_array_new ();
	private->n_comm = -1;
	private->current_comm = -1;

	private->command_pid = 0;
	fr_channel_data_init (&process->out);
	fr_channel_data_init (&process->err);

	private->check_timeout = 0;
	private->running = FALSE;
	private->stopping = FALSE;
	process->restart = FALSE;

	private->current_charset = -1;
	private->use_standard_locale = FALSE;
	private->exec_data = NULL;
}


FrProcess *
fr_process_new (void)
{
	return FR_PROCESS (g_object_new (FR_TYPE_PROCESS, NULL));
}


void
fr_process_clear (FrProcess *process)
{
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	gint i;

	g_return_if_fail (process != NULL);

	for (i = 0; i <= private->n_comm; i++) {
		FrCommandInfo *info;

		info = g_ptr_array_index (private->comm, i);
		fr_command_info_free (info);
		g_ptr_array_index (private->comm, i) = NULL;
	}

	for (i = 0; i <= private->n_comm; i++)
		g_ptr_array_remove_index_fast (private->comm, 0);

	private->n_comm = -1;
	private->current_comm = -1;
}


void
fr_process_begin_command (FrProcess  *process,
			  const char *arg)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);

	FrProcessPrivate *private = fr_process_get_instance_private (process);

	info = fr_command_info_new ();
	info->args = g_list_prepend (NULL, g_strdup (arg));

	g_ptr_array_add (private->comm, info);

	private->n_comm++;
	private->current_comm = private->n_comm;
}


void
fr_process_begin_command_at (FrProcess  *process,
			     const char *arg,
			     int         index)
{
	FrCommandInfo *info, *old_c_info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	g_return_if_fail (index >= 0 && index <= private->n_comm);

	private->current_comm = index;

	old_c_info = g_ptr_array_index (private->comm, index);

	if (old_c_info != NULL)
		fr_command_info_free (old_c_info);

	info = fr_command_info_new ();
	info->args = g_list_prepend (NULL, g_strdup (arg));

	g_ptr_array_index (private->comm, index) = info;
}


void
fr_process_set_working_dir (FrProcess  *process,
			    const char *dir)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	g_return_if_fail (private->current_comm >= 0);

	info = g_ptr_array_index (private->comm, private->current_comm);
	if (info->dir != NULL)
		g_free (info->dir);
	info->dir = g_strdup (dir);
}


void
fr_process_set_working_dir_file (FrProcess *process,
				 GFile     *folder)
{
	char *path;

	path = g_file_get_path (folder);
	fr_process_set_working_dir (process, path);

	g_free (path);
}

void
fr_process_set_sticky (FrProcess *process,
		       gboolean   sticky)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	g_return_if_fail (private->current_comm >= 0);

	info = g_ptr_array_index (private->comm, private->current_comm);
	info->sticky = sticky;
}


void
fr_process_set_ignore_error (FrProcess *process,
			     gboolean   ignore_error)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	g_return_if_fail (private->current_comm >= 0);

	info = g_ptr_array_index (private->comm, private->current_comm);
	info->ignore_error = ignore_error;
}


void
fr_process_add_arg (FrProcess  *process,
		    const char *arg)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	g_return_if_fail (private->current_comm >= 0);

	info = g_ptr_array_index (private->comm, private->current_comm);
	info->args = g_list_prepend (info->args, g_strdup (arg));
}


void
fr_process_add_arg_concat (FrProcess  *process,
			   const char *arg1,
			   ...)
{
	GString *arg;
	va_list  args;
	char    *s;

	arg = g_string_new (arg1);

	va_start (args, arg1);
	while ((s = va_arg (args, char*)) != NULL)
		g_string_append (arg, s);
	va_end (args);

	fr_process_add_arg (process, arg->str);
	g_string_free (arg, TRUE);
}


void
fr_process_add_arg_printf (FrProcess    *fr_proc,
			   const char   *format,
			   ...)
{
	va_list  args;
	char    *arg;

	va_start (args, format);
	arg = g_strdup_vprintf (format, args);
	va_end (args);

	fr_process_add_arg (fr_proc, arg);

	g_free (arg);
}


void
fr_process_add_arg_file (FrProcess *process,
			 GFile     *file)
{
	char *path;

	path = g_file_get_path (file);
	fr_process_add_arg (process, path);

	g_free (path);
}


void
fr_process_set_arg_at (FrProcess  *process,
		       int         n_comm,
		       int         n_arg,
		       const char *arg_value)
{
	FrCommandInfo *info;
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	GList         *arg;

	g_return_if_fail (process != NULL);

	info = g_ptr_array_index (private->comm, n_comm);
	arg = g_list_nth (info->args, n_arg);
	g_return_if_fail (arg != NULL);

	g_free (arg->data);
	arg->data = g_strdup (arg_value);
}


void
fr_process_set_begin_func (FrProcess    *process,
			   FrProcFunc      func,
			   gpointer      func_data)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	info = g_ptr_array_index (private->comm, private->current_comm);
	info->begin_func = func;
	info->begin_data = func_data;
}


void
fr_process_set_end_func (FrProcess    *process,
			 FrProcFunc      func,
			 gpointer      func_data)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);

	FrProcessPrivate *private = fr_process_get_instance_private (process);
	info = g_ptr_array_index (private->comm, private->current_comm);
	info->end_func = func;
	info->end_data = func_data;
}


void
fr_process_set_continue_func (FrProcess    *process,
			      FrContinueFunc  func,
			      gpointer      func_data)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);

	FrProcessPrivate *private = fr_process_get_instance_private (process);
	if (private->current_comm < 0)
		return;

	info = g_ptr_array_index (private->comm, private->current_comm);
	info->continue_func = func;
	info->continue_data = func_data;
}


void
fr_process_end_command (FrProcess *process)
{
	FrCommandInfo *info;

	g_return_if_fail (process != NULL);

	FrProcessPrivate *private = fr_process_get_instance_private (process);
	info = g_ptr_array_index (private->comm, private->current_comm);
	info->args = g_list_reverse (info->args);
}


void
fr_process_use_standard_locale (FrProcess *process,
				gboolean   use_stand_locale)
{
	g_return_if_fail (process != NULL);
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	private->use_standard_locale = use_stand_locale;
}


void
fr_process_set_out_line_func (FrProcess *process,
			      FrLineFunc   func,
			      gpointer   data)
{
	g_return_if_fail (process != NULL);

	process->out.line_func = func;
	process->out.line_data = data;
}


void
fr_process_set_err_line_func (FrProcess *process,
			      FrLineFunc   func,
			      gpointer   data)
{
	g_return_if_fail (process != NULL);

	process->err.line_func = func;
	process->err.line_data = data;
}


/* fr_process_execute */


static gboolean
command_is_sticky (FrProcess *process,
		   int        i)
{
	FrCommandInfo *info;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	info = g_ptr_array_index (private->comm, i);
	return info->sticky;
}


static void
allow_sticky_processes_only (ExecuteData *exec_data)
{
	FrProcess *process = exec_data->process;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	if (! private->sticky_only) {
		/* Remember the first error. */

		exec_data->error_command = private->current_command;
		exec_data->first_error = fr_error_copy (exec_data->error);
		exec_data->first_error_stdout = g_list_reverse (_g_string_list_dup (process->out.raw));
		exec_data->first_error_stderr = g_list_reverse (_g_string_list_dup (process->err.raw));
	}

	private->sticky_only = TRUE;

	if (! private->stopping)
		g_signal_emit (G_OBJECT (process),
			       fr_process_signals[STICKY_ONLY],
			       0);
}


static void
execute_cancelled_cb (GCancellable *cancellable,
		      gpointer      user_data)
{
	ExecuteData *exec_data = user_data;
	FrProcess   *process = exec_data->process;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	if (! private->running)
		return;

	if (private->stopping)
		return;

	private->stopping = TRUE;
	exec_data->error = fr_error_new (FR_ERROR_STOPPED, 0, NULL);

	if (command_is_sticky (process, private->current_command))
		allow_sticky_processes_only (exec_data);

	else if (private->command_pid > 0)
		killpg (private->command_pid, SIGTERM);

	else {
		if (private->check_timeout != 0) {
			g_source_remove (private->check_timeout);
			private->check_timeout = 0;
		}

		private->command_pid = 0;
		fr_channel_data_close_source (&process->out);
		fr_channel_data_close_source (&process->err);

		private->running = FALSE;

		if (exec_data->cancel_id != 0) {
			g_signal_handler_disconnect (exec_data->cancellable, exec_data->cancel_id);
			exec_data->cancel_id = 0;
		}
		g_simple_async_result_complete_in_idle (exec_data->result);
	}
}


static void _fr_process_start (ExecuteData *exec_data);


static void
_fr_process_restart (ExecuteData *exec_data)
{
	exec_data->process->restart = TRUE;
	_fr_process_start (exec_data);
}


static void  execute_current_command (ExecuteData *exec_data);


static void
child_setup (gpointer user_data)
{
	FrProcess *process = user_data;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	if (private->use_standard_locale)
		putenv ("LC_MESSAGES=C");

	/* detach from the tty */

	setsid ();

	/* create a process group to kill all the child processes when
	 * canceling the operation. */

	setpgid (0, 0);
}


static const char *
_fr_process_get_charset (FrProcess *process)
{
	const char *charset = NULL;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	if (private->current_charset >= 0)
		charset = try_charsets[private->current_charset];
	else if (g_get_charset (&charset))
		charset = NULL;

	return charset;
}


static void
_fr_process_execute_complete_in_idle (ExecuteData *exec_data)
{
	if (exec_data->cancel_id != 0) {
		g_cancellable_disconnect (exec_data->cancellable, exec_data->cancel_id);
		exec_data->cancel_id = 0;
	}
	g_simple_async_result_complete_in_idle (exec_data->result);
}


static gint
check_child (gpointer data)
{
	ExecuteData   *exec_data = data;
	FrProcess     *process = exec_data->process;
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	FrCommandInfo *info;
	pid_t          pid;
	int            status;
	gboolean       continue_process;

	info = g_ptr_array_index (private->comm, private->current_command);

	/* Remove check. */

	g_source_remove (private->check_timeout);
	private->check_timeout = 0;

	if (fr_channel_data_read (&process->out) == G_IO_STATUS_ERROR) {
		exec_data->error = fr_error_new (FR_ERROR_IO_CHANNEL, 0, process->out.error);
	}
	else if (fr_channel_data_read (&process->err) == G_IO_STATUS_ERROR) {
		exec_data->error = fr_error_new (FR_ERROR_IO_CHANNEL, 0, process->err.error);
	}
	else {
		pid = waitpid (private->command_pid, &status, WNOHANG);
		if (pid != private->command_pid) {
			/* Add check again. */
			private->check_timeout = g_timeout_add (REFRESH_RATE,
							              check_child,
							              exec_data);
			return FALSE;
		}
	}

	if (info->ignore_error && (exec_data->error != NULL)) {
#ifdef DEBUG
			{
				GList *scan;

				g_print ("** ERROR [1] **\n");
				g_print ("%s\n", exec_data->error->gerror->message);
				for (scan = process->err.raw; scan; scan = scan->next)
					g_print ("%s\n", (char *)scan->data);
			}
#endif
		fr_clear_error (&exec_data->error);
		debug (DEBUG_INFO, "[error ignored]\n");
	}
	else if (exec_data->error == NULL) {
		if (WIFEXITED (status)) {
			/*if (WEXITSTATUS (status) == 255) {
				exec_data->error = fr_error_new (FR_ERROR_COMMAND_NOT_FOUND, 0, NULL);
			}
			else*/
			if (WEXITSTATUS (status) != 0) {
				exec_data->error = fr_error_new (FR_ERROR_COMMAND_ERROR, WEXITSTATUS (status), NULL);
			}
		}
		else {
			exec_data->error = fr_error_new (FR_ERROR_EXITED_ABNORMALLY, 255, NULL);
		}
	}

	private->command_pid = 0;

	if (exec_data->error == NULL) {
		if (fr_channel_data_flush (&process->out) == G_IO_STATUS_ERROR)
			exec_data->error = fr_error_new (FR_ERROR_IO_CHANNEL, 0, process->out.error);
		else if (fr_channel_data_flush (&process->err) == G_IO_STATUS_ERROR)
			exec_data->error = fr_error_new (FR_ERROR_IO_CHANNEL, 0, process->err.error);
	}

	if (info->end_func != NULL)
		(*info->end_func) (info->end_data);

	/**/

	if ((exec_data->error != NULL)
	    && (exec_data->error->type == FR_ERROR_IO_CHANNEL)
	    && g_error_matches (exec_data->error->gerror, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE))
	{
		if (private->current_charset < n_charsets - 1) {
			/* try with another charset */
			private->current_charset++;
			_fr_process_restart (exec_data);
			return FALSE;
		}
		fr_error_free (exec_data->error);
		exec_data->error = fr_error_new (FR_ERROR_BAD_CHARSET, 0, exec_data->error->gerror);
	}

	/* Check whether to continue or stop the process */

	continue_process = TRUE;
	if (info->continue_func != NULL)
		continue_process = (*info->continue_func) (&exec_data->error, info->continue_data);

	/* Execute the next command. */
	if (continue_process) {
		if (exec_data->error != NULL) {
			allow_sticky_processes_only (exec_data);
#ifdef DEBUG
			{
				GList *scan;

				g_print ("** ERROR [2] : (status: %d) (type: %d) **\n", exec_data->error->status, exec_data->error->type);
				for (scan = process->err.raw; scan; scan = scan->next)
					g_print ("%s\n", (char *)scan->data);
			}
#endif
		}

		if (private->sticky_only) {
			do {
				private->current_command++;
			}
			while ((private->current_command <= private->n_comm)
				&& ! command_is_sticky (process, private->current_command));
		}
		else
			private->current_command++;

		if (private->current_command <= private->n_comm) {
			execute_current_command (exec_data);
			return FALSE;
		}
	}

	/* Done */

	private->current_command = -1;
	private->use_standard_locale = FALSE;

	if (process->out.raw != NULL)
		process->out.raw = g_list_reverse (process->out.raw);
	if (process->err.raw != NULL)
		process->err.raw = g_list_reverse (process->err.raw);

	private->running = FALSE;
	private->stopping = FALSE;

	if (private->sticky_only) {
		/* Restore the first error. */

		fr_error_free (exec_data->error);
		exec_data->error = fr_error_copy (exec_data->first_error);

		/* Restore the first error output as well. */

		_g_string_list_free (process->out.raw);
		process->out.raw = exec_data->first_error_stdout;
		exec_data->first_error_stdout = NULL;

		_g_string_list_free (process->err.raw);
		process->err.raw = exec_data->first_error_stderr;
		exec_data->first_error_stderr = NULL;
	}

	_fr_process_execute_complete_in_idle (exec_data);

	return FALSE;
}


static void
execute_current_command (ExecuteData *exec_data)
{
	FrProcess      *process = exec_data->process;
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	FrCommandInfo  *info;
	GList          *scan;
	char          **argv;
	int             out_fd, err_fd;
	int             i = 0;
	GError         *error = NULL;

	debug (DEBUG_INFO, "%d/%d) ", private->current_command, private->n_comm);

	info = g_ptr_array_index (private->comm, private->current_command);

	argv = g_new (char *, g_list_length (info->args) + 1);
	for (scan = info->args; scan; scan = scan->next)
		argv[i++] = scan->data;
	argv[i] = NULL;

#ifdef DEBUG
	{
		int j;

		if (private->use_standard_locale)
			g_print ("\tLC_MESSAGES=C\n");

		if (info->dir != NULL)
			g_print ("\tcd %s\n", info->dir);

		if (info->ignore_error)
			g_print ("\t[ignore error]\n");

		g_print ("\t");
		for (j = 0; j < i; j++)
			g_print ("%s ", argv[j]);
		g_print ("\n");
	}
#endif

	if (info->begin_func != NULL)
		(*info->begin_func) (info->begin_data);

	if (! g_spawn_async_with_pipes (info->dir,
					argv,
					NULL,
					(G_SPAWN_LEAVE_DESCRIPTORS_OPEN
					 | G_SPAWN_SEARCH_PATH
					 | G_SPAWN_DO_NOT_REAP_CHILD),
					child_setup,
					process,
					&private->command_pid,
					NULL,
					&out_fd,
					&err_fd,
					&error))
	{
		exec_data->error = fr_error_new (FR_ERROR_SPAWN, 0, error);
		_fr_process_execute_complete_in_idle (exec_data);

		g_error_free (error);
		g_free (argv);
		return;
	}

	g_free (argv);

	fr_channel_data_set_fd (&process->out, out_fd, _fr_process_get_charset (process));
	fr_channel_data_set_fd (&process->err, err_fd, _fr_process_get_charset (process));

	private->check_timeout = g_timeout_add (REFRESH_RATE,
					              check_child,
					              exec_data);
}


static void
_fr_process_start (ExecuteData *exec_data)
{
	FrProcess *process = exec_data->process;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	_g_string_list_free (exec_data->first_error_stdout);
	exec_data->first_error_stdout = NULL;

	_g_string_list_free (exec_data->first_error_stderr);
	exec_data->first_error_stderr = NULL;

        fr_error_free (exec_data->error);
        exec_data->error = NULL;

	fr_channel_data_reset (&process->out);
	fr_channel_data_reset (&process->err);

	private->sticky_only = FALSE;
	private->current_command = 0;
	private->stopping = FALSE;

	if (private->n_comm == -1) {
		private->running = FALSE;
		_fr_process_execute_complete_in_idle (exec_data);
	}
	else {
		private->running = TRUE;
		execute_current_command (exec_data);
	}
}


void
fr_process_execute (FrProcess           *process,
		    GCancellable        *cancellable,
		    GAsyncReadyCallback  callback,
		    gpointer             user_data)
{
	ExecuteData *exec_data;
	FrProcessPrivate *private = fr_process_get_instance_private (process);

	g_return_if_fail (! private->running);

	execute_data_free (private->exec_data);

	private->exec_data = exec_data = g_new0 (ExecuteData, 1);
	exec_data->process = g_object_ref (process);
	exec_data->cancellable = _g_object_ref (cancellable);
	exec_data->cancel_id = 0;
	exec_data->result = g_simple_async_result_new (G_OBJECT (process),
						       callback,
						       user_data,
						       fr_process_execute);

        g_simple_async_result_set_op_res_gpointer (exec_data->result, exec_data, NULL);

	if (! process->restart)
		private->current_charset = -1;

	if (cancellable != NULL) {
		GError *error = NULL;

		if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
			exec_data->error = fr_error_new (FR_ERROR_STOPPED, 0, error);
			_fr_process_execute_complete_in_idle (exec_data);

			g_error_free (error);
			return;
		}

		exec_data->cancel_id = g_cancellable_connect (cancellable,
							      G_CALLBACK (execute_cancelled_cb),
							      exec_data,
							      NULL);
	}

	_fr_process_start (exec_data);
}


gboolean
fr_process_execute_finish (FrProcess     *process,
			   GAsyncResult  *result,
			   FrError      **error)
{
	ExecuteData *exec_data;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (process), fr_process_execute), FALSE);

	exec_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	if (exec_data->error == NULL)
		return TRUE;

	if (error != NULL) {
		if (exec_data->error->gerror == NULL)
			exec_data->error->gerror = g_error_new_literal (FR_ERROR, exec_data->error->type, "");
		*error = fr_error_copy (exec_data->error);
	}

	return FALSE;
}


void
fr_process_restart (FrProcess *process)
{
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	if (private->exec_data != NULL)
		_fr_process_start (private->exec_data);
}


void
fr_process_cancel (FrProcess *process)
{
	FrProcessPrivate *private = fr_process_get_instance_private (process);
	if (! private->running)
		return;
	if (private->exec_data == NULL)
		return;
	if (private->exec_data->cancellable == NULL)
		return;
	g_cancellable_cancel (private->exec_data->cancellable);
}
