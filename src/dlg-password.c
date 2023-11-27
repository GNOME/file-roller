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
#include "gio-utils.h"
#include "gtk-utils.h"
#include "glib-utils.h"
#include "preferences.h"
#include "dlg-password.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (data->builder, (x)))


typedef struct {
	GtkBuilder *builder;
	FrWindow   *window;
	GtkWidget  *dialog;
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
response_cb (GtkDialog  *dialog,
	     int         response_id,
	     DialogData *data)
{
	if (response_id == GTK_RESPONSE_OK) {
		char      *password;
		gboolean   encrypt_header;

		password = _gtk_entry_get_locale_text (GTK_ENTRY (GET_WIDGET ("password_entry")));
		encrypt_header = gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")));
		fr_window_archive_encrypt (data->window, password, encrypt_header);

		g_free (password);
	}

	gtk_window_destroy (GTK_WINDOW (data->dialog));
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
	data->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "password.ui");

	/* Set widgets data. */

	data->dialog = g_object_new (GTK_TYPE_DIALOG,
				     "transient-for", GTK_WINDOW (window),
				     "modal", TRUE,
				     "use-header-bar", _gtk_settings_get_dialogs_use_header (),
				     "title", _("Password"),
				     NULL);
	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data->dialog))), GET_WIDGET ("password_vbox"));
	gtk_dialog_add_buttons (GTK_DIALOG (data->dialog),
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				_GTK_LABEL_SAVE, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK)),
				     "suggested-action");

	basename = _g_file_get_display_name (fr_archive_get_file (window->archive));
	title = g_strdup_printf (_("Enter a password for “%s”"), basename);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("title_label")), title);

	g_free (title);
	g_free (basename);

	_gtk_entry_use_as_password_entry (GTK_ENTRY (GET_WIDGET ("password_entry")));
	_gtk_entry_set_locale_text (GTK_ENTRY (GET_WIDGET ("password_entry")),
				    fr_window_get_password (window));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")),
				     fr_window_get_encrypt_header (window));

	if (! fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_ENCRYPT_HEADER)) {
		gtk_check_button_set_inconsistent (GTK_CHECK_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")), TRUE);
		gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), FALSE);
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

	/* Run dialog. */

	gtk_widget_grab_focus (GET_WIDGET ("password_entry"));
	gtk_window_present (GTK_WINDOW (data->dialog));
}
