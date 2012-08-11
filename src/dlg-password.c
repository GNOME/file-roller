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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include "fr-window.h"
#include "gtk-utils.h"
#include "glib-utils.h"
#include "preferences.h"
#include "dlg-password.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (data->builder, (x)))


typedef struct {
	GtkBuilder *builder;
	FrWindow   *window;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
	    DialogData *data)
{
	g_object_unref (data->builder);
	g_free (data);
}


static void
response_cb (GtkWidget  *dialog,
	     int         response_id,
	     DialogData *data)
{
	if (response_id == GTK_RESPONSE_OK) {
		char      *password;
		gboolean   encrypt_header;

		password = _gtk_entry_get_locale_text (GTK_ENTRY (GET_WIDGET ("password_entry")));
		encrypt_header = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")));
		fr_window_archive_encrypt (data->window, password, encrypt_header);

		g_free (password);
	}

	gtk_widget_destroy (GET_WIDGET ("dialog"));
}


void
dlg_password (GtkWidget *widget,
	      gpointer   callback_data)
{
	FrWindow   *window = callback_data;
	DialogData *data;
	char       *basename;
	char       *title;

	data = g_new0 (DialogData, 1);
	data->window = window;
	data->builder = _gtk_builder_new_from_resource ("password.ui");
	if (data->builder == NULL) {
		g_free (data);
		return;
	}

	/* Set widgets data. */

	basename = _g_file_get_display_basename (fr_archive_get_file (window->archive));
	title = g_strdup_printf (_("Enter a password for \"%s\""), basename);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("title_label")), title);

	g_free (title);
	g_free (basename);

	_gtk_entry_set_locale_text (GTK_ENTRY (GET_WIDGET ("password_entry")),
				    fr_window_get_password (window));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")),
				      fr_window_get_encrypt_header (window));

	if (! fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_ENCRYPT_HEADER)) {
		gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")), TRUE);
		gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), FALSE);
	}

	/* Set the signals handlers. */

	g_signal_connect (GET_WIDGET ("dialog"),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect (GET_WIDGET ("dialog"),
			  "response",
			  G_CALLBACK (response_cb),
			  data);

	/* Run dialog. */

	gtk_widget_grab_focus (GET_WIDGET ("password_entry"));
	gtk_window_set_transient_for (GTK_WINDOW (GET_WIDGET ("dialog")), GTK_WINDOW (window));
	gtk_window_set_modal (GTK_WINDOW (GET_WIDGET ("dialog")), TRUE);
	gtk_widget_show (GET_WIDGET ("dialog"));
}
