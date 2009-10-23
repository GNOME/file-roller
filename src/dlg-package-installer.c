/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001-2009 The Free Software Foundation, Inc.
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
#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>
#include "dlg-package-installer.h"


typedef struct {
	FrWindow   *window;
	FrArchive  *archive;
	FrAction    action;
	const char *packages;
} InstallerData;


static void
installer_data_free (InstallerData *idata)
{
	g_object_unref (idata->archive);
	g_object_unref (idata->window);
	g_free (idata);
}


static void
package_installer_terminated (InstallerData *idata,
			      const char    *error)
{
	if (error != NULL) {
		fr_archive_action_completed (idata->archive,
					     FR_ACTION_CREATING_NEW_ARCHIVE,
					     FR_PROC_ERROR_GENERIC,
					     error);
	}
	else {
		if (fr_window_is_batch_mode (idata->window))
			fr_window_resume_batch (idata->window);
		else
			fr_window_restart_current_batch_action (idata->window);
	}

	installer_data_free (idata);
}


static void
packagekit_install_package_call_notify_cb (DBusGProxy     *proxy,
					   DBusGProxyCall *call,
					   gpointer        user_data)
{
	InstallerData *idata = user_data;
	gboolean       success;
	GError        *error = NULL;
	char          *message = NULL;

	success = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID);
	if (! success) {
		const char *remote = NULL;

		if (error->domain == DBUS_GERROR && error->code == DBUS_GERROR_REMOTE_EXCEPTION)
			remote = dbus_g_error_get_name (error);
		if ((remote == NULL) || (strcmp (remote, "org.freedesktop.PackageKit.Modify.Failed") == 0))
			message = g_strdup_printf ("%s\n%s",
                                                   _("There was an internal error trying to search for applications:"),
                                                   error->message);
		g_error_free (error);
	}

	package_installer_terminated (idata, message);

	g_free (message);
	g_object_unref (proxy);
}


static void
install_packages (InstallerData *idata)
{
	gboolean         success = FALSE;
	DBusGConnection *connection;

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	if (connection != NULL) {
		DBusGProxy *proxy;

		proxy = dbus_g_proxy_new_for_name (connection,
						   "org.freedesktop.PackageKit",
						   "/org/freedesktop/PackageKit",
						   "org.freedesktop.PackageKit.Modify");

		if (proxy != NULL) {
			GdkWindow       *window;
			guint            xid;
			char           **names;
			DBusGProxyCall  *call;

			window = gtk_widget_get_window (GTK_WIDGET (idata->window));
		        if (window != NULL)
		        	xid = GDK_WINDOW_XID (window);
		        else
		        	xid = 0;

		        names = g_strsplit (idata->packages, ",", -1);
			call = dbus_g_proxy_begin_call (proxy,
							"InstallPackageNames",
							(DBusGProxyCallNotify) packagekit_install_package_call_notify_cb,
							idata,
							NULL,
							G_TYPE_UINT, xid,
							G_TYPE_STRV, names,
							G_TYPE_STRING, "hide-confirm-search,hide-finished,hide-warning",
							G_TYPE_INVALID);
			success = (call != NULL);

			g_strfreev (names);
		}
	}

	if (! success)
		package_installer_terminated (idata, _("Archive type not supported."));
}


static void
confirm_search_dialog_response_cb (GtkDialog *dialog,
				   int        response_id,
				   gpointer   user_data)
{
	InstallerData *idata = user_data;

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (response_id == GTK_RESPONSE_YES) {
		install_packages (idata);
	}
	else {
		fr_window_stop_batch (idata->window);
		installer_data_free (idata);
	}
}


static void
dbus_name_has_owner_call_notify_cb (DBusGProxy     *proxy,
				    DBusGProxyCall *call,
				    gpointer        user_data)
{
	InstallerData *idata = user_data;
	GError        *error = NULL;
	gboolean       success;
	gboolean       present;
	GtkWidget     *dialog;

	success = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_BOOLEAN, &present, G_TYPE_INVALID);
	if (! success) {
		package_installer_terminated (idata, error->message);
		g_error_free (error);
		return;
	}

	if (! present) {
		package_installer_terminated (idata, _("Archive type not supported."));
		return;
	}

	dialog = gtk_message_dialog_new (GTK_WINDOW (idata->window),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_YES_NO,
					 "%s", "error");
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  _("There is no command installed for %s files.\nDo you want to search for an command to open this file?"),
						  g_content_type_get_description (idata->archive->content_type));
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	g_signal_connect (dialog, "response", G_CALLBACK (confirm_search_dialog_response_cb), idata);
	gtk_widget_show_all (dialog);
}


void
dlg_package_installer (FrWindow  *window,
		       FrArchive *archive,
		       FrAction   action)
{
	InstallerData   *idata;
	GType            command_type;
	FrCommand       *command;
	DBusGConnection *connection;
	gboolean         success = FALSE;

	idata = g_new0 (InstallerData, 1);
	idata->window = g_object_ref (window);
	idata->archive = g_object_ref (archive);
	idata->action = action;

	command_type = get_preferred_command_for_mime_type (idata->archive->content_type, FR_COMMAND_CAN_READ_WRITE);
	if (command_type == 0)
		command_type = get_preferred_command_for_mime_type (idata->archive->content_type, FR_COMMAND_CAN_READ);
	if (command_type == 0) {
		package_installer_terminated (idata, _("Archive type not supported."));
		return;
	}

	command = g_object_new (command_type, 0);
	idata->packages = fr_command_get_packages (command, idata->archive->content_type);
	g_object_unref (command);

	if (idata->packages == NULL) {
		package_installer_terminated (idata, _("Archive type not supported."));
		return;
	}

#ifdef ENABLE_PACKAGEKIT
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	if (connection != NULL) {
		DBusGProxy *proxy;

		proxy = dbus_g_proxy_new_for_name (connection,
						   DBUS_SERVICE_DBUS,
						   DBUS_PATH_DBUS,
						   DBUS_INTERFACE_DBUS);
		if (proxy != NULL) {
			DBusGProxyCall *call;

			call = dbus_g_proxy_begin_call (proxy,
							"NameHasOwner",
							(DBusGProxyCallNotify) dbus_name_has_owner_call_notify_cb,
							idata,
							NULL,
							G_TYPE_STRING, "org.freedesktop.PackageKit",
							G_TYPE_INVALID);
			success = (call != NULL);
		}
	}
#endif /* ENABLE_PACKAGEKIT */

	if (! success)
		package_installer_terminated (idata, _("Archive type not supported."));
}
