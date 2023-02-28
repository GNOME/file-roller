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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <gtk/gtk.h>
#include "fr-window.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "dlg-delete.h"


typedef struct {
	FrWindow  *window;
	GList     *selected_files;
	GtkBuilder *builder;

	GtkWidget *dialog;
	GtkWidget *d_all_files_radio;
	GtkWidget *d_selected_files_radio;
	GtkWidget *d_files_radio;
	GtkWidget *d_files_entry;
} DialogData;

/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
           DialogData *data)
{
       _g_string_list_free (data->selected_files);
       g_object_unref (G_OBJECT (data->builder));
       g_free (data);
}

/* called when the dialog is dismissed. */
static void
response_cb (GtkDialog  *widget,
			int         response_id,
			DialogData *data)
{
	gboolean  selected_files;
	gboolean  pattern_files;
	FrWindow *window = data->window;
	GList    *file_list = NULL;
	gboolean  do_not_remove_if_null = FALSE;

	switch (response_id) {
	case GTK_RESPONSE_OK:
		selected_files = gtk_check_button_get_active (GTK_CHECK_BUTTON (data->d_selected_files_radio));
		pattern_files = gtk_check_button_get_active (GTK_CHECK_BUTTON (data->d_files_radio));

		/* create the file list. */

		if (selected_files) {
			file_list = data->selected_files;
			data->selected_files = NULL;       /* do not free the list when destroying the dialog. */
		}
		else if (pattern_files) {
			const char *pattern;

			pattern = gtk_editable_get_text (GTK_EDITABLE (data->d_files_entry));
			file_list = fr_window_get_file_list_pattern (window, pattern);
			if (file_list == NULL)
				do_not_remove_if_null = TRUE;
		}

		/* remove ! */

		if (! do_not_remove_if_null || (file_list != NULL))
			fr_window_archive_remove (window, file_list);

		_g_string_list_free (file_list);
		break;
	}

	/* close the dialog. */
	gtk_window_destroy (GTK_WINDOW (data->dialog));
}


static void
entry_changed_cb (GtkEditable *widget,
		  DialogData *data)
{
	if (! gtk_check_button_get_active (GTK_CHECK_BUTTON (data->d_files_radio)))
		gtk_check_button_set_active (GTK_CHECK_BUTTON (data->d_files_radio), TRUE);
}


static void
dlg_delete__common (FrWindow *window,
	            GList    *selected_files)
{
	DialogData *data;
	GtkWidget  *delete_box;
	GtkWidget  *ok_button;

	data = g_new (DialogData, 1);
	data->window = window;
	data->selected_files = selected_files;

	data->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "delete.ui");

	/* Get the widgets. */

	data->dialog = g_object_new (GTK_TYPE_DIALOG,
				     "transient-for", GTK_WINDOW (window),
				     "modal", TRUE,
				     "use-header-bar", _gtk_settings_get_dialogs_use_header (),
				     "title", _("Delete"),
				     NULL);
	gtk_window_set_default_size (GTK_WINDOW (data->dialog), 500, -1);

	gtk_dialog_add_buttons (GTK_DIALOG (data->dialog),
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				_("_Delete"), GTK_RESPONSE_OK,
				NULL);

	ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "destructive-action");

	delete_box = _gtk_builder_get_widget (data->builder, "delete_box");

	data->d_all_files_radio = _gtk_builder_get_widget (data->builder, "d_all_files_radio");
	data->d_selected_files_radio = _gtk_builder_get_widget (data->builder, "d_selected_files_radio");
	data->d_files_radio = _gtk_builder_get_widget (data->builder, "d_files_radio");
	data->d_files_entry = _gtk_builder_get_widget (data->builder, "d_files_entry");

	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data->dialog))), delete_box);

	/* Set widgets data. */

	if (data->selected_files != NULL)
		gtk_check_button_set_active (GTK_CHECK_BUTTON (data->d_selected_files_radio), TRUE);
	else {
		gtk_widget_set_sensitive (data->d_selected_files_radio, FALSE);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (data->d_all_files_radio), TRUE);
	}

	/* Set the signals handlers. */

	g_signal_connect (GTK_DIALOG (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect (GTK_DIALOG (data->dialog),
			  "response",
			  G_CALLBACK (response_cb),
			  data);
//	g_signal_connect_swapped (GTK_BUTTON (cancel_button),
//				  "clicked",
//				  G_CALLBACK (gtk_window_destroy),
//				  G_OBJECT (data->dialog));
//	g_signal_connect (GTK_BUTTON (ok_button),
//			  "clicked",
//			  G_CALLBACK (ok_clicked_cb),
//			  data);
	g_signal_connect (GTK_ENTRY (data->d_files_entry),
			  "changed",
			  G_CALLBACK (entry_changed_cb),
			  data);

	/* Run dialog. */
	gtk_window_present (GTK_WINDOW (data->dialog));
}


void
dlg_delete (GtkWidget *widget,
	    gpointer   callback_data)
{
	FrWindow *window = callback_data;
	dlg_delete__common (window,
			    fr_window_get_file_list_selection (window, TRUE, FALSE, NULL));
}


void
dlg_delete_from_sidebar (GtkWidget *widget,
			 gpointer   callback_data)
{
	FrWindow *window = callback_data;
	dlg_delete__common (window,
			    fr_window_get_folder_tree_selection (window, TRUE, NULL));
}
