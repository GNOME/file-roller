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

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomeui/gnome-file-entry.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "gtk-utils.h"
#include "window.h"


#define PROP_GLADE_FILE "file_roller.glade"


typedef struct {
	GladeXML  *gui;
	FRWindow  *window;
	GtkWidget *dialog;
	GtkWidget *pw_password_entry;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	g_object_unref (data->gui);
        g_free (data);
}


static void
ok_clicked_cb (GtkWidget  *widget,
	       DialogData *data)
{
	char *password;

	password = _gtk_entry_get_locale_text (GTK_ENTRY (data->pw_password_entry));
	window_set_password (data->window, password);
	g_free (password);

	gtk_widget_destroy (data->dialog);
}


static void
response_cb (GtkWidget  *dialog,
	     int         response_id,
	     DialogData *data)
{
	switch (response_id) {
	case GTK_RESPONSE_OK:
		ok_clicked_cb (NULL, data);
		break;
	case GTK_RESPONSE_CLOSE:
	default:
		gtk_widget_destroy (data->dialog);
		break;
	}
}


void
dlg_password (GtkWidget *widget,
	      gpointer   callback_data)
{
        DialogData       *data;
	FRWindow         *window = callback_data;

        data = g_new (DialogData, 1);
	data->window = window;
	data->gui = glade_xml_new (GLADEDIR "/" PROP_GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " PROP_GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "password_dialog");
	data->pw_password_entry = glade_xml_get_widget (data->gui, "pw_password_entry");

	/* Set widgets data. */

	if (window->password != NULL)
		_gtk_entry_set_locale_text (GTK_ENTRY (data->pw_password_entry), window->password);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (data->dialog), 
			  "response",
			  G_CALLBACK (response_cb),
			  data);

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
        gtk_window_set_modal         (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
