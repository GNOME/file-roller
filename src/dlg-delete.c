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

#include <config.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomeui/gnome-file-entry.h>
#include "window.h"
#include "misc.h"


#define EXTRACT_GLADE_FILE "file_roller.glade2"


typedef struct {
	FRWindow  *window;
	GladeXML  *gui;

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
        g_object_unref (G_OBJECT (data->gui));
        g_free (data);
}


/* called when the "ok" button is pressed. */
static void
ok_clicked_cb (GtkWidget  *widget, 
	       DialogData *data)
{
	gboolean  selected_files;
	gboolean  pattern_files;
	FRWindow *window = data->window;
	GList    *file_list;

	selected_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->d_selected_files_radio));
	pattern_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->d_files_radio));

	/* create the file list. */

	file_list = NULL;
	if (selected_files) 
		file_list = window_get_file_list_selection (window, TRUE, NULL);
	else if (pattern_files) {
		const gchar *pattern;
		pattern = gtk_entry_get_text (GTK_ENTRY (data->d_files_entry));
		file_list = window_get_file_list_pattern (window, pattern);
	}

	/* close the dialog. */

	gtk_widget_destroy (data->dialog);

	/* remove ! */

	fr_archive_remove (window->archive, file_list, window->compression);

	if (file_list != NULL) {
		g_list_foreach (file_list, (GFunc) g_free, NULL);
		g_list_free (file_list);
	}
}


static void
entry_changed_cb (GtkWidget  *widget, 
		  DialogData *data)
{
	if (! GTK_TOGGLE_BUTTON (data->d_files_radio)->active)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->d_files_radio), TRUE);
}


void
dlg_delete (GtkWidget *widget,
	    gpointer   callback_data)
{
        DialogData *data;
	FRWindow   *window = callback_data;
	GtkWidget  *cancel_button;
	GtkWidget  *ok_button;

        data = g_new (DialogData, 1);

        data->window = window;

	data->gui = glade_xml_new (GLADEDIR "/" EXTRACT_GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " EXTRACT_GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "delete_dialog");
	data->d_all_files_radio = glade_xml_get_widget (data->gui, "d_all_files_radio");
	data->d_selected_files_radio = glade_xml_get_widget (data->gui, "d_selected_files_radio");
	data->d_files_radio = glade_xml_get_widget (data->gui, "d_files_radio");
	data->d_files_entry = glade_xml_get_widget (data->gui, "d_files_entry");

	ok_button = glade_xml_get_widget (data->gui, "d_ok_button");
	cancel_button = glade_xml_get_widget (data->gui, "d_cancel_button");

	/* Set widgets data. */

	if (misc_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view))) > 0)
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
	g_signal_connect_swapped (G_OBJECT (cancel_button), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (ok_button), 
			  "clicked",
			  G_CALLBACK (ok_clicked_cb),
			  data);
	g_signal_connect (G_OBJECT (data->d_files_entry), 
			  "changed",
			  G_CALLBACK (entry_changed_cb),
			  data);

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
        gtk_window_set_modal         (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
