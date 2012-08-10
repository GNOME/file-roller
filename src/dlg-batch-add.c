/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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
#include <glib/gi18n.h>
#include "dlg-batch-add.h"
#include "file-utils.h"
#include "fr-new-archive-dialog.h"
#include "fr-window.h"
#include "glib-utils.h"


static void
dialog_response_cb (GtkDialog *dialog,
		    int        response_id,
		    gpointer   user_data)
{
	FrWindow *window = user_data;

	if (response_id == GTK_RESPONSE_OK) {
		GFile      *file;
		const char *mime_type;

		file = fr_new_archive_dialog_get_file (FR_NEW_ARCHIVE_DIALOG (dialog), &mime_type);
		if (file == NULL)
			return;

		fr_window_create_archive_and_continue (window, file, mime_type, NULL);
		g_object_unref (file);
	}
	else
		fr_window_stop_batch (window);

	gtk_widget_destroy (GTK_WIDGET (dialog));
}


void
dlg_batch_add_files (FrWindow *window,
		     GList    *file_list)
{
	GFile     *first_file;
	GFile     *parent;
	char      *filename;
	GtkWidget *dialog;

	g_return_if_fail (file_list != NULL);

	first_file = G_FILE (file_list->data);
	parent = g_file_get_parent (first_file);

	filename = NULL;
	if (file_list->next == NULL)
		filename = g_file_get_basename (first_file);
	else
		filename = g_file_get_basename (parent);

	if (! _g_file_check_permissions (parent, R_OK | W_OK)) {
		g_object_unref (parent);
		parent = g_object_ref (_g_file_get_home ());
	}

	dialog = fr_new_archive_dialog_new (_("Compress"),
					    NULL,
					    FR_NEW_ARCHIVE_ACTION_NEW,
					    parent,
					    filename);
	g_signal_connect (dialog,
			  "response",
			  G_CALLBACK (dialog_response_cb),
			  window);
	gtk_window_present (GTK_WINDOW (dialog));

	g_object_unref (parent);
	g_free (filename);
}
