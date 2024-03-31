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
#include "gtk-utils.h"


static void
new_archive_get_file_cb (FrNewArchiveDialog *dialog,
			 GFile              *file,
			 const char         *mime_type,
			 gpointer            user_data)
{
	FrWindow *window = user_data;

	if (file == NULL)
		return;

	fr_window_set_password (window, fr_new_archive_dialog_get_password (FR_NEW_ARCHIVE_DIALOG (dialog)));
	fr_window_set_encrypt_header (window, fr_new_archive_dialog_get_encrypt_header (FR_NEW_ARCHIVE_DIALOG (dialog)));
	fr_window_set_volume_size (window, fr_new_archive_dialog_get_volume_size (FR_NEW_ARCHIVE_DIALOG (dialog)));
	fr_window_create_archive_and_continue (window, file, mime_type, NULL);

	gtk_window_destroy (GTK_WINDOW (dialog));
}


static void
dialog_response_cb (GtkDialog *dialog,
		    int        response_id,
		    gpointer   user_data)
{
	FrWindow *window = user_data;

	if (response_id != GTK_RESPONSE_OK) {
		fr_window_batch_stop (window);
		gtk_window_destroy (GTK_WINDOW (dialog));
		return;
	}

	fr_new_archive_dialog_get_file (FR_NEW_ARCHIVE_DIALOG (dialog),
					new_archive_get_file_cb,
					user_data);
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
	parent = _g_object_ref (fr_window_get_add_default_dir (window));
	if (parent == NULL)
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
					    ((file_list->next == NULL) ? FR_NEW_ARCHIVE_ACTION_NEW_SINGLE_FILE : FR_NEW_ARCHIVE_ACTION_NEW_MANY_FILES),
					    parent,
					    filename,
					    NULL);
	fr_new_archive_dialog_set_files_to_add (FR_NEW_ARCHIVE_DIALOG (dialog), file_list);

	g_signal_connect (GTK_DIALOG (dialog),
			  "response",
			  G_CALLBACK (dialog_response_cb),
			  window);

	fr_new_archive_dialog_show_options (FR_NEW_ARCHIVE_DIALOG (dialog));

	g_object_unref (parent);
	g_free (filename);
}
