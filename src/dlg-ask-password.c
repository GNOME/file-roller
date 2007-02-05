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
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "gtk-utils.h"
#include "window.h"


#define PROP_GLADE_FILE "ask-password.glade"


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
ask_password__response_cb (GtkWidget  *dialog,
			   int         response_id,
			   DialogData *data)
{
	char *password;

	switch (response_id) {
	case GTK_RESPONSE_OK:
		password = _gtk_entry_get_locale_text (GTK_ENTRY (data->pw_password_entry));
		window_set_password (data->window, password);
		g_free (password);
		if (data->window->batch_mode)
			window_batch_mode_resume (data->window);
		else
			window_restart_current_action (data->window);
		break;

	default:
		if (data->window->batch_mode)
			window_close (data->window);
		else
			window_current_action_description_reset (data->window);
		break;
	}

	gtk_widget_destroy (data->dialog);
}


void
dlg_ask_password (FRWindow *window)
{
        DialogData *data;
	GtkWidget  *label;
	char       *text;

        data = g_new0 (DialogData, 1);
	data->window = window;
	data->gui = glade_xml_new (GLADEDIR "/" PROP_GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " PROP_GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "password_dialog");
	data->pw_password_entry = glade_xml_get_widget (data->gui, "pw_password_entry");

	label = glade_xml_get_widget (data->gui, "pw_password_label");

	/* Set widgets data. */

	text = g_strdup_printf (_("Enter the password for the archive '%s'."), g_filename_display_basename (window->archive_filename));
	gtk_label_set_label (GTK_LABEL (label), text);
	g_free (text);
	if (window->password != NULL)
		_gtk_entry_set_locale_text (GTK_ENTRY (data->pw_password_entry), window->password);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (data->dialog),
			  "response",
			  G_CALLBACK (ask_password__response_cb),
			  data);

	/* Run dialog. */

	gtk_widget_grab_focus (data->pw_password_entry);
	if (GTK_WIDGET_REALIZED (window->app))
		gtk_window_set_transient_for (GTK_WINDOW (data->dialog),
					      GTK_WINDOW (window->app));
        gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
