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
response_cb (GtkWidget  *widget,
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
		selected_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->d_selected_files_radio));
		pattern_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->d_files_radio));

		/* create the file list. */

		if (selected_files) {
			file_list = data->selected_files;
			data->selected_files = NULL;       /* do not free the list when destroying the dialog. */
		}
		else if (pattern_files) {
			const char *pattern;

			pattern = gtk_entry_get_text (GTK_ENTRY (data->d_files_entry));
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
	gtk_widget_destroy (data->dialog);
}


static void
entry_changed_cb (GtkWidget  *widget,
		  DialogData *data)
{
	if (! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->d_files_radio)))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->d_files_radio), TRUE);
}


static void
dlg_delete__common (FrWindow *window,
	            GList    *selected_files)
{
	DialogData *data;
	GtkWidget  *content_area;
	GtkWidget  *delete_box;
	GtkWidget  *ok_button;

	data = g_new (DialogData, 1);
	data->window = window;
	data->selected_files = selected_files;

	data->builder = _gtk_builder_new_from_resource ("delete.ui");
	if (data->builder == NULL) {
		g_free (data);
		return;
	}

	/* Get the widgets. */

	data->dialog = g_object_new (GTK_TYPE_DIALOG,
				     "transient-for", GTK_WINDOW (window),
				     "modal", TRUE,
				     "use-header-bar", _gtk_settings_get_dialogs_use_header (),
				     NULL);

	gtk_dialog_add_buttons (GTK_DIALOG (data->dialog),
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				_("_Delete"), GTK_RESPONSE_OK,
				NULL);

	ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), GTK_STYLE_CLASS_DESTRUCTIVE_ACTION);

	delete_box = _gtk_builder_get_widget (data->builder, "delete_box");

	data->d_all_files_radio = _gtk_builder_get_widget (data->builder, "d_all_files_radio");
	data->d_selected_files_radio = _gtk_builder_get_widget (data->builder, "d_selected_files_radio");
	data->d_files_radio = _gtk_builder_get_widget (data->builder, "d_files_radio");
	data->d_files_entry = _gtk_builder_get_widget (data->builder, "d_files_entry");

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (data->dialog));
	gtk_container_add (GTK_CONTAINER (content_area), delete_box);

	/* Set widgets data. */

	if (data->selected_files != NULL)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->d_selected_files_radio), TRUE);
	else {
		gtk_widget_set_sensitive (data->d_selected_files_radio, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->d_all_files_radio), TRUE);
	}

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect (G_OBJECT (data->dialog),
			  "response",
			  G_CALLBACK (response_cb),
			  data);
//	g_signal_connect_swapped (G_OBJECT (cancel_button),
//				  "clicked",
//				  G_CALLBACK (gtk_widget_destroy),
//				  G_OBJECT (data->dialog));
//	g_signal_connect (G_OBJECT (ok_button),
//			  "clicked",
//			  G_CALLBACK (ok_clicked_cb),
//			  data);
	g_signal_connect (G_OBJECT (data->d_files_entry),
			  "changed",
			  G_CALLBACK (entry_changed_cb),
			  data);

	/* Run dialog. */
	gtk_widget_show (data->dialog);
}


void
dlg_delete (GtkWidget *widget,
	    gpointer   callback_data)
{
	FrWindow *window = callback_data;
	dlg_delete__common (window,
			    fr_window_get_file_list_selection (window, TRUE, NULL));
}


void
dlg_delete_from_sidebar (GtkWidget *widget,
			 gpointer   callback_data)
{
	FrWindow *window = callback_data;
	dlg_delete__common (window,
			    fr_window_get_folder_tree_selection (window, TRUE, NULL));
}
