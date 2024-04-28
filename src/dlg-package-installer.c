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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "dlg-package-installer.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "fr-init.h"


#define BUFFER_SIZE_FOR_PRELOAD 32


typedef struct {
	FrWindow     *window;
	FrAction      action;
	GCancellable *cancellable;
	const char   *packages;
} InstallerData;


static void
installer_data_free (InstallerData *idata)
{
	g_object_unref (idata->window);
	_g_object_ref (idata->cancellable);
	g_free (idata);
}


static void
package_installer_terminated (InstallerData *idata,
			      FrErrorType    error_type,
			      const char    *error_message)
{
	gtk_widget_set_cursor (GTK_WIDGET (idata->window), NULL);

	if (error_type != FR_ERROR_NONE) {
		fr_window_batch_stop_with_error (idata->window,
						 idata->action,
						 error_type,
						 error_message);
	}
	else {
		fr_update_registered_archives_capabilities ();
		if (fr_window_is_batch_mode (idata->window))
			fr_window_batch_resume (idata->window);
		else
			fr_window_restart_current_action (idata->window);
	}

	installer_data_free (idata);
}


#ifdef ENABLE_PACKAGEKIT


static void
packagekit_install_package_names_ready_cb (GObject      *source_object,
					   GAsyncResult *res,
					   gpointer      user_data)
{
	InstallerData   *idata = user_data;
	GDBusProxy      *proxy;
	GVariant        *values;
	GError          *error = NULL;
	FrErrorType      error_type = FR_ERROR_NONE;
	char            *error_message = NULL;

	proxy = G_DBUS_PROXY (source_object);
	values = g_dbus_proxy_call_finish (proxy, res, &error);
	if (values == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
		    || (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)
			&& (error->message != NULL)
			&& (strstr (error->message, "org.freedesktop.Packagekit.Modify2.Cancelled") != NULL)))
		{
			error_type = FR_ERROR_STOPPED;
			error_message = NULL;
		}
		else {
			error_type = FR_ERROR_GENERIC;
			error_message = g_strdup_printf ("%s\n%s",
							 _("There was an internal error trying to search for applications:"),
							 error->message);
		}
		g_clear_error (&error);
	}

	package_installer_terminated (idata, error_type, error_message);

	g_free (error_message);
	if (values != NULL)
		g_variant_unref (values);
	g_object_unref (proxy);
}


static char **
get_packages_real_names (char **names)
{
	char     **real_names;
	GKeyFile  *key_file;
	char      *filename;
	int        i;

	real_names = g_new0 (char *, g_strv_length (names));
	key_file = g_key_file_new ();
	filename = g_build_filename (PRIVDATADIR, "packages.match", NULL);
	g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, NULL);

	for (i = 0; names[i] != NULL; i++) {
		char *real_name;

		real_name = g_key_file_get_string (key_file, "Package Matches", names[i], NULL);
		if (real_name != NULL)
			real_name = g_strstrip (real_name);
		if ((real_name == NULL) || (strncmp (real_name, "", 1) == 0)) {
			g_free (real_name);
			real_name = g_strdup (names[i]);
		}
		real_names[i] = real_name;
		real_name = NULL;
	}

	g_free (filename);
	g_key_file_free (key_file);

	return real_names;
}


static void
install_packages (InstallerData *idata)
{
	GDBusConnection *connection;
	GError          *error = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, idata->cancellable, &error);
	if (connection != NULL) {
		GDBusProxy *proxy;

		gtk_widget_set_cursor_from_name (GTK_WIDGET (idata->window), "wait");

		proxy = g_dbus_proxy_new_sync (connection,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify2",
					       idata->cancellable,
					       &error);

		if (proxy != NULL) {
			char     **names;
			char     **real_names;
			char      *desktop_startup_id;
			GVariant  *platform_data;

			names = g_strsplit (idata->packages, ",", -1);
			real_names = get_packages_real_names (names);
			desktop_startup_id = g_strdup_printf ("_TIME%i", 0 /* FIXME: delete if not needed */);
			platform_data = g_variant_new_parsed ("{'desktop-startup-id': %v}", g_variant_new_take_string (desktop_startup_id));

			g_dbus_proxy_call (proxy,
					   "InstallPackageNames",
					   g_variant_new ("(^asss@a{sv})",
							  real_names,
							  "hide-confirm-search,hide-finished,hide-warning",
							  "org.gnome.FileRoller",
							  platform_data),
					   G_DBUS_CALL_FLAGS_NONE,
					   G_MAXINT,
					   idata->cancellable,
					   packagekit_install_package_names_ready_cb,
					   idata);

			g_strfreev (real_names);
			g_strfreev (names);
		}
	}

	if (error != NULL) {
		char *message;

		message = g_strdup_printf ("%s\n%s",
					   _("There was an internal error trying to search for applications:"),
					   error->message);
		package_installer_terminated (idata, FR_ERROR_GENERIC, message);

		g_clear_error (&error);
	}
}


static void
confirm_search_dialog_response_cb (GtkDialog *dialog,
				   int        response_id,
				   gpointer   user_data)
{
	InstallerData *idata = user_data;

	gtk_window_destroy (GTK_WINDOW (dialog));

	if (response_id == GTK_RESPONSE_YES) {
		install_packages (idata);
	}
	else {
		fr_window_batch_stop (idata->window);
		installer_data_free (idata);
	}
}


#endif /* ENABLE_PACKAGEKIT */


static void
file_buffer_ready_cb (GObject      *source_object,
		      GAsyncResult *result,
		      gpointer      user_data)
{
	InstallerData *idata = user_data;
	GFile         *file;
	char          *buffer;
	gsize          buffer_size;
	GError        *error = NULL;
	char          *uri;
	const char    *mime_type;
	gboolean       result_uncertain;
	GType          archive_type;
	FrArchive     *preferred_archive;

	file = G_FILE (source_object);
	if (! _g_file_load_buffer_finish (file, result, &buffer, &buffer_size, &error)) {
		package_installer_terminated (idata, FR_ERROR_GENERIC, error->message);
		g_error_free (error);
		return;
	}

	if (buffer == NULL) {
		package_installer_terminated (idata, FR_ERROR_GENERIC, _("Archive type not supported."));
		return;
	}

	uri = g_file_get_uri (file);
	mime_type = g_content_type_guess (uri, (guchar *) buffer, buffer_size, &result_uncertain);
	if (result_uncertain) {
		mime_type = _g_mime_type_get_from_content (buffer, buffer_size);
		if (mime_type == NULL)
			mime_type = _g_mime_type_get_from_filename (file);
	}

	g_free (uri);
	g_free (buffer);

	archive_type = fr_get_preferred_archive_for_mime_type (mime_type, FR_ARCHIVE_CAN_READ_WRITE);
	if (archive_type == 0)
		archive_type = fr_get_preferred_archive_for_mime_type (mime_type, FR_ARCHIVE_CAN_READ);
	if (archive_type == 0) {
		package_installer_terminated (idata, FR_ERROR_GENERIC, _("Archive type not supported."));
		return;
	}

	preferred_archive = g_object_new (archive_type, 0);
	idata->packages = fr_archive_get_packages (preferred_archive, mime_type);
	g_object_unref (preferred_archive);

	if (idata->packages == NULL) {
		package_installer_terminated (idata, FR_ERROR_GENERIC, _("Archive type not supported."));
		return;
	}

#ifdef ENABLE_PACKAGEKIT

	{
		char      *secondary_text;
		GtkWidget *dialog;
		g_autofree char *description;

		description = g_content_type_get_description (mime_type);
		secondary_text = g_strdup_printf (_("There is no command installed for %s files.\nDo you want to search for a command to open this file?"),
		                                  description);
		dialog = _gtk_message_dialog_new (GTK_WINDOW (idata->window),
						  GTK_DIALOG_MODAL,
						  _("Could not open this file type"),
						  secondary_text,
						  _GTK_LABEL_CANCEL, GTK_RESPONSE_NO,
						  _("_Search Command"), GTK_RESPONSE_YES,
						  NULL);
		g_signal_connect (GTK_MESSAGE_DIALOG (dialog), "response", G_CALLBACK (confirm_search_dialog_response_cb), idata);
		gtk_window_present (GTK_WINDOW (dialog));

		g_free (secondary_text);
	}

#else /* ! ENABLE_PACKAGEKIT */

	package_installer_terminated (idata, FR_ERROR_GENERIC, _("Archive type not supported."));

#endif /* ENABLE_PACKAGEKIT */
}


void
dlg_package_installer (FrWindow     *window,
		       GFile        *file,
		       FrAction      action,
		       GCancellable *cancellable)
{
	InstallerData   *idata;

	idata = g_new0 (InstallerData, 1);
	idata->window = g_object_ref (window);
	idata->action = action;
	idata->cancellable = _g_object_ref (cancellable);

	_g_file_load_buffer_async (file,
				   BUFFER_SIZE_FOR_PRELOAD,
				   cancellable,
				   file_buffer_ready_cb,
				   idata);
}
