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
#include <string.h>

#include <gnome.h>
#include <glade/glade.h>

#include "bookmarks.h"
#include "dlg-open-with.h"
#include "file-utils.h"
#include "window.h"
#include "typedefs.h"


#define GLADE_FILE "file_roller.glade"


typedef struct {
	FRWindow *   window;
	GladeXML *   gui;

	GtkWidget *  dialog;
	GtkWidget *  c_viewer_radiobutton;
	GtkWidget *  c_app_radiobutton;

	gchar *      filename;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget *widget,
            DialogData *data)
{
	g_object_unref (G_OBJECT (data->gui));
	if (data->filename != NULL)
		g_free (data->filename);
        g_free (data);
}


/* called when the "add" button is pressed. */
static void
ok_clicked_cb (GtkWidget *widget, 
	       DialogData *data)
{
	FRWindow *window = data->window; 
	gboolean viewer;

	viewer = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->c_viewer_radiobutton));

	if (viewer) 
		window_view_file (window, data->filename);
	else {
		GList *file_list = g_list_append (NULL, data->filename);
		dlg_open_with (window, file_list);
		g_list_free (file_list);
	}

	gtk_widget_destroy (data->dialog);
}


void 
dlg_viewer_or_app (FRWindow *window,
		   gchar *filename)
{
        DialogData *data;
	GtkWidget  *cancel_button;
	GtkWidget  *ok_button;

        data = g_new (DialogData, 1);

        data->window = window;
	data->filename = g_strdup (filename);

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (! data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "viewer_or_app_dialog");
	data->c_viewer_radiobutton = glade_xml_get_widget (data->gui, "va_viewer_radiobutton");
	data->c_app_radiobutton = glade_xml_get_widget (data->gui, "va_app_radiobutton");

	ok_button = glade_xml_get_widget (data->gui, "va_ok_button");
	cancel_button = glade_xml_get_widget (data->gui, "va_cancel_button");

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

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
        gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
