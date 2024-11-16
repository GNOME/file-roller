/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2005 Free Software Foundation, Inc.
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
#include "file-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "fr-init.h"
#include "fr-window.h"
#include "dlg-open-with.h"

#ifdef USE_NATIVE_APPCHOOSER
# include <libportal/portal.h>
# include <libportal-gtk4/portal-gtk4.h>
#endif

typedef struct {
	FrWindow *window;
	GList    *file_list;
} OpenData;


#ifdef USE_NATIVE_APPCHOOSER
static void
open_with_portal_cb (GObject	  *source_obj,
		     GAsyncResult *result,
		     gpointer	   user_data)
{
	XdpPortal *portal = XDP_PORTAL (source_obj);
	GtkWindow *window = GTK_WINDOW (user_data);
	g_autoptr (GError) error = NULL;

	if (!xdp_portal_open_uri_finish (portal, result, &error)
	    && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
	{
		_gtk_error_dialog_run (GTK_WINDOW (window),
				       _("Could not perform the operation"),
				       "%s",
				       error->message);
	}
}


static void
dlg_open_with_native_appchooser (FrWindow *window,
				 GFile    *file)
{
	g_autoptr(XdpParent) parent = NULL;
	g_autoptr(XdpPortal) portal = NULL;
	g_autoptr(GError) error = NULL;

	portal = xdp_portal_initable_new (&error);

	if (error) {
		g_warning ("Failed to create XdpPortal instance: %s", error->message);
		return;
	}

	parent = xdp_parent_new_gtk (GTK_WINDOW (window));

	g_autofree char *uri;
	uri = g_file_get_uri (file);
	xdp_portal_open_uri (portal, parent, uri,
			XDP_OPEN_URI_FLAG_ASK, NULL,
			open_with_portal_cb, window);
}
#endif


static void
app_chooser_response_cb (GtkDialog *dialog,
			 int        response_id,
			 gpointer   user_data)
{
	OpenData *o_data = user_data;
	GAppInfo *app_info;

	switch (response_id) {
	case GTK_RESPONSE_OK:
		app_info = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (dialog));
		if (app_info != NULL) {
			fr_window_open_files_with_application (o_data->window, o_data->file_list, app_info);
			g_object_unref (app_info);
		}
		g_free (o_data);
		gtk_window_destroy (GTK_WINDOW (dialog));
		break;

	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		g_free (o_data);
		gtk_window_destroy (GTK_WINDOW (dialog));
		break;

	default:
		break;
	}
}


static void
dlg_open_with_nonnative_appchooser (FrWindow *window,
				    GList    *file_list)
{
	OpenData  *o_data;
	GtkWidget *app_chooser;

	o_data = g_new0 (OpenData, 1);
	o_data->window = window;
	o_data->file_list = file_list;

	app_chooser = gtk_app_chooser_dialog_new (GTK_WINDOW (window),
						  GTK_DIALOG_MODAL,
						  G_FILE (file_list->data));
	g_signal_connect (GTK_APP_CHOOSER_DIALOG (app_chooser),
			  "response",
			  G_CALLBACK (app_chooser_response_cb),
			  o_data);
	gtk_widget_show (app_chooser);
}


void
dlg_open_with (FrWindow *window,
	       GList    *file_list)
{
#ifdef USE_NATIVE_APPCHOOSER
	// Use the native appchooser only for a single file.
	if (file_list->next == NULL) {
		GFile *file = (GFile *) file_list->data;
		dlg_open_with_native_appchooser (window, file);
	}
	else
#endif
	dlg_open_with_nonnative_appchooser (window, file_list);
}


void
open_with_cb (GtkWidget *widget,
	      void      *callback_data)
{
	FrWindow *window = callback_data;
	GList    *file_list;

	file_list = fr_window_get_file_list_selection (window, FALSE, FALSE, NULL);
	if (file_list == NULL)
		return;

	fr_window_open_files (window, file_list, TRUE);
	_g_string_list_free (file_list);
}
