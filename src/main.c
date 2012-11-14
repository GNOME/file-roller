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

#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib.h>
#include <gio/gio.h>
#ifdef ENABLE_NOTIFICATION
#  include <libnotify/notify.h>
#endif
#include "actions.h"
#ifdef USE_SMCLIENT
#  include "eggsmclient.h"
#endif
#include "eggdesktopfile.h"
#include "file-utils.h"
#include "fr-init.h"
#include "glib-utils.h"
#include "gtk-utils.h"


#define ORG_GNOME_ARCHIVEMANAGER_XML "/org/gnome/FileRoller/../data/org.gnome.ArchiveManager1.xml"
#define SERVICE_TIMEOUT 10


gint                ForceDirectoryCreation;
static char       **remaining_args;
static char        *arg_add_to = NULL;
static int          arg_add = FALSE;
static char        *arg_extract_to = NULL;
static int          arg_extract = FALSE;
static int          arg_extract_here = FALSE;
static char        *arg_default_dir = NULL;
static gboolean     arg_version = FALSE;
static gboolean     arg_service = FALSE;
static gboolean     arg_notify = FALSE;
static const char  *program_argv0 = NULL; /* argv[0] from main(); used as the command to restart the program */


static const GOptionEntry options[] = {
	{ "add-to", 'a', 0, G_OPTION_ARG_STRING, &arg_add_to,
	  N_("Add files to the specified archive and quit the program"),
	  N_("ARCHIVE") },

	{ "add", 'd', 0, G_OPTION_ARG_NONE, &arg_add,
	  N_("Add files asking the name of the archive and quit the program"),
	  NULL },

	{ "extract-to", 'e', 0, G_OPTION_ARG_STRING, &arg_extract_to,
	  N_("Extract archives to the specified folder and quit the program"),
	  N_("FOLDER") },

	{ "extract", 'f', 0, G_OPTION_ARG_NONE, &arg_extract,
	  N_("Extract archives asking the destination folder and quit the program"),
	  NULL },

	{ "extract-here", 'h', 0, G_OPTION_ARG_NONE, &arg_extract_here,
	  N_("Extract the contents of the archives in the archive folder and quit the program"),
	  NULL },

	{ "default-dir", '\0', 0, G_OPTION_ARG_STRING, &arg_default_dir,
	  N_("Default folder to use for the '--add' and '--extract' commands"),
	  N_("FOLDER") },

	{ "force", '\0', 0, G_OPTION_ARG_NONE, &ForceDirectoryCreation,
	  N_("Create destination folder without asking confirmation"),
	  NULL },

	{ "notify", '\0', 0, G_OPTION_ARG_NONE, &arg_notify,
	  N_("Use the notification system to notify the operation completion"), NULL },

	{ "service", '\0', 0, G_OPTION_ARG_NONE, &arg_service,
	  N_("Start as a service"), NULL },

	{ "version", 'v', 0, G_OPTION_ARG_NONE, &arg_version,
	  N_("Show version"), NULL },

	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args,
	  NULL,
	  NULL },

	{ NULL }
};


/* -- app menu -- */


static void
activate_new (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_new (NULL, windows->data);
}


static void
activate_help (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_manual (NULL, windows->data);
}


static void
activate_about (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_about (NULL, windows->data);
}


static void
activate_quit (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	activate_action_quit (NULL, NULL);
}


static const GActionEntry app_menu_entries[] = {
	{ "new",  activate_new },
	{ "help",  activate_help },
	{ "about", activate_about },
	{ "quit",  activate_quit }
};


static void
initialize_app_menu (GApplication *application)
{
	gboolean    show_app_menu;
	GtkBuilder *builder;

	g_object_get (gtk_settings_get_default (),
		      "gtk-shell-shows-app-menu", &show_app_menu,
		      NULL);
	if (! show_app_menu)
		return;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);

	builder = _gtk_builder_new_from_resource ("app-menu.ui");
	gtk_application_set_app_menu (GTK_APPLICATION (application),
				      G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu")));

	g_object_unref (builder);
}


/* -- session management -- */


#ifdef USE_SMCLIENT


static void
client_save_state (EggSMClient *client,
		   GKeyFile    *state,
		   gpointer     user_data)
{
	/* discard command is automatically set by EggSMClient */

	GApplication *application;
	const char   *argv[2] = { NULL };
	guint         i;

	/* restart command */
	argv[0] = program_argv0;
	argv[1] = NULL;

	egg_sm_client_set_restart_command (client, 1, argv);

	/* state */
	application = g_application_get_default ();
	if (application != NULL) {
		GList *window;

		for (window = gtk_application_get_windows (GTK_APPLICATION (application)), i = 0;
		     window != NULL;
		     window = window->next, i++)
		{
			FrWindow *session = window->data;
			gchar    *key;

			key = g_strdup_printf ("archive%d", i);
			if ((session->archive == NULL) || (fr_archive_get_file (session->archive) == NULL)) {
				g_key_file_set_string (state, "Session", key, "");
			}
			else {
				gchar *uri;

				uri = g_file_get_uri (fr_archive_get_file (session->archive));
				g_key_file_set_string (state, "Session", key, uri);
				g_free (uri);
			}
			g_free (key);
		}
	}

	g_key_file_set_integer (state, "Session", "archives", i);
}


static void
client_quit_cb (EggSMClient *client,
		gpointer     data)
{
	gtk_main_quit ();
}


static void
fr_restore_session (EggSMClient *client)
{
	GKeyFile *state = NULL;
	guint i;

	state = egg_sm_client_get_state_file (client);

	i = g_key_file_get_integer (state, "Session", "archives", NULL);

	for (; i > 0; i--) {
		GtkWidget *window;
		char      *key;
		char      *uri;

		key = g_strdup_printf ("archive%d", i);
		uri = g_key_file_get_string (state, "Session", key, NULL);

		window = fr_window_new ();
		if (strlen (uri) > 0) {
			GFile *file;

			file = g_file_new_for_uri (uri);
			fr_window_archive_open (FR_WINDOW (window), file, GTK_WINDOW (window));

			g_object_unref (file);
		}

		g_free (uri);
		g_free (key);
	}
}


#endif /* USE_SMCLIENT */


/* -- service -- */


static void
window_ready_cb (GtkWidget *widget,
		 GError    *error,
		 gpointer   user_data)
{
	if (error == NULL)
		g_dbus_method_invocation_return_value ((GDBusMethodInvocation *) user_data, NULL);
	else
		g_dbus_method_invocation_return_error ((GDBusMethodInvocation *) user_data,
						       error->domain,
						       error->code,
						       "%s",
						       error->message);
}


static gboolean
window_progress_cb (FrWindow *window,
		    double    fraction,
		    char     *details,
		    gpointer  user_data)
{
	GDBusConnection *connection = user_data;

	g_dbus_connection_emit_signal (connection,
				       NULL,
				       "org/gnome/ArchiveManager1",
				       "org.gnome.ArchiveManager1",
				       "Progress",
				       g_variant_new ("(ds)",
						      fraction,
						      details),
				       NULL);

	return TRUE;
}


static void
handle_method_call (GDBusConnection       *connection,
		    const char            *sender,
		    const char            *object_path,
		    const char            *interface_name,
		    const char            *method_name,
		    GVariant              *parameters,
		    GDBusMethodInvocation *invocation,
		    gpointer               user_data)
{
	update_registered_archives_capabilities ();

	if (g_strcmp0 (method_name, "GetSupportedTypes") == 0) {
		char *action;
		int  *supported_types = NULL;

		g_variant_get (parameters, "(s)", &action);

		if (g_strcmp0 (action, "create") == 0) {
			supported_types = save_type;
		}
		else if (g_strcmp0 (action, "create_single_file") == 0) {
			supported_types = single_file_save_type;
		}
		else if (g_strcmp0 (action, "extract") == 0) {
			supported_types = open_type;
		}

		if (supported_types == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       G_IO_ERROR,
							       G_IO_ERROR_INVALID_ARGUMENT,
							       "Invalid action '%s', valid values are: create, create_single_file, extract",
							       action);
		}
		else {
			GVariantBuilder builder;
			int             i;

			g_variant_builder_init (&builder, G_VARIANT_TYPE ("(aa{ss})"));
			g_variant_builder_open (&builder, G_VARIANT_TYPE ("aa{ss}"));
			for (i = 0; supported_types[i] != -1; i++) {
				g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{ss}"));
				g_variant_builder_add (&builder, "{ss}",
						       "mime-type",
						       mime_type_desc[supported_types[i]].mime_type);
				g_variant_builder_add (&builder, "{ss}",
						       "default-extension",
						       mime_type_desc[supported_types[i]].default_ext);
				g_variant_builder_close (&builder);
			}
			g_variant_builder_close (&builder);

			g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (&builder));
		}

		g_free (action);
	}
	else if (g_strcmp0 (method_name, "AddToArchive") == 0) {
		char       *archive_uri;
		char      **files;
		gboolean    use_progress_dialog;
		int         i;
		GFile      *file;
		GList      *file_list = NULL;
		GtkWidget  *window;

		g_variant_get (parameters, "(s^asb)", &archive_uri, &files, &use_progress_dialog);

		file = g_file_new_for_uri (archive_uri);
		for (i = 0; files[i] != NULL; i++)
			file_list = g_list_prepend (file_list, g_file_new_for_uri (files[i]));
		file_list = g_list_reverse (file_list);

		window = fr_window_new ();
		fr_window_use_progress_dialog (FR_WINDOW (window), use_progress_dialog);

		g_signal_connect (window, "progress", G_CALLBACK (window_progress_cb), connection);
		g_signal_connect (window, "ready", G_CALLBACK (window_ready_cb), invocation);

		fr_window_new_batch (FR_WINDOW (window), _("Compress"));
		fr_window_set_batch__add (FR_WINDOW (window), file, file_list);
		fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		fr_window_start_batch (FR_WINDOW (window));

		g_object_unref (file);
		_g_object_list_unref (file_list);
		g_free (archive_uri);
	}
	else if (g_strcmp0 (method_name, "Compress") == 0) {
		char      **files;
		char       *destination_uri;
		gboolean    use_progress_dialog;
		int         i;
		GList      *file_list = NULL;
		GFile      *destination;
		GtkWidget  *window;

		g_variant_get (parameters, "(^assb)", &files, &destination_uri, &use_progress_dialog);

		if ((destination_uri != NULL) && (strcmp (destination_uri, "") != 0))
			destination = g_file_new_for_uri (destination_uri);
		else
			destination = g_file_get_parent (G_FILE (file_list->data));

		for (i = 0; files[i] != NULL; i++)
			file_list = g_list_prepend (file_list, g_file_new_for_uri (files[i]));
		file_list = g_list_reverse (file_list);

		window = fr_window_new ();
		fr_window_use_progress_dialog (FR_WINDOW (window), use_progress_dialog);
		fr_window_set_default_dir (FR_WINDOW (window), destination, TRUE);

		g_signal_connect (window, "progress", G_CALLBACK (window_progress_cb), connection);
		g_signal_connect (window, "ready", G_CALLBACK (window_ready_cb), invocation);

		fr_window_new_batch (FR_WINDOW (window), _("Extract archive"));
		fr_window_set_batch__add (FR_WINDOW (window), NULL, file_list);
		fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		fr_window_start_batch (FR_WINDOW (window));

		_g_object_list_unref (file_list);
		g_object_unref (destination);
		g_free (destination_uri);
	}
	else if (g_strcmp0 (method_name, "Extract") == 0) {
		char      *archive_uri;
		char      *destination_uri;
		gboolean   use_progress_dialog;
		GtkWidget *window;
		GFile     *archive;
		GFile     *destination;

		g_variant_get (parameters, "(ssb)", &archive_uri, &destination_uri, &use_progress_dialog);

		archive = g_file_new_for_uri (archive_uri);
		destination = g_file_new_for_uri (destination_uri);

		window = fr_window_new ();
		fr_window_use_progress_dialog (FR_WINDOW (window), use_progress_dialog);
		if ((destination_uri != NULL) & (strcmp (destination_uri, "") != 0)) {
			GFile *file;

			file = g_file_new_for_uri (destination_uri);
			fr_window_set_default_dir (FR_WINDOW (window), file, TRUE);

			g_object_unref (file);
		}

		g_signal_connect (window, "progress", G_CALLBACK (window_progress_cb), connection);
		g_signal_connect (window, "ready", G_CALLBACK (window_ready_cb), invocation);

		fr_window_new_batch (FR_WINDOW (window), _("Extract archive"));
		fr_window_set_batch__extract (FR_WINDOW (window), archive, destination);
		fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		fr_window_start_batch (FR_WINDOW (window));

		g_object_unref (archive);
		g_object_unref (destination);
		g_free (destination_uri);
		g_free (archive_uri);
	}
	else if (g_strcmp0 (method_name, "ExtractHere") == 0) {
		char      *uri;
		GFile     *archive;
		gboolean   use_progress_dialog;
		GtkWidget *window;

		g_variant_get (parameters, "(sb)", &uri, &use_progress_dialog);

		archive = g_file_new_for_uri (uri);

		window = fr_window_new ();
		fr_window_use_progress_dialog (FR_WINDOW (window), use_progress_dialog);

		g_signal_connect (window, "progress", G_CALLBACK (window_progress_cb), connection);
		g_signal_connect (window, "ready", G_CALLBACK (window_ready_cb), invocation);

		fr_window_new_batch (FR_WINDOW (window), _("Extract archive"));
		fr_window_set_batch__extract_here (FR_WINDOW (window), archive);
		fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		fr_window_start_batch (FR_WINDOW (window));

		g_object_unref (archive);
		g_free (uri);
	}
}


static const GDBusInterfaceVTable interface_vtable = {
	handle_method_call,
	NULL, 			/* handle_get_property */
	NULL 			/* handle_set_property */
};


/* -- main application -- */


#define FR_APPLICATION(x) ((FrApplication *)(x))

typedef struct {
	GtkApplication __parent;
	GDBusNodeInfo  *introspection_data;
	guint           owner_id;
} FrApplication;

typedef GtkApplicationClass FrApplicationClass;

static gpointer fr_application_parent_class;

GType fr_application_get_type (void); /* added to suppress a compiler warning */
G_DEFINE_TYPE (FrApplication, fr_application, GTK_TYPE_APPLICATION)


static void
fr_application_finalize (GObject *object)
{
	FrApplication *self = FR_APPLICATION (object);

	if (self->introspection_data != NULL)
		g_dbus_node_info_unref (self->introspection_data);
	if (self->owner_id != 0)
		g_bus_unown_name (self->owner_id);

        G_OBJECT_CLASS (fr_application_parent_class)->finalize (object);
}


static void
on_bus_acquired_for_archive_manager (GDBusConnection *connection,
				     const char      *name,
				     gpointer         user_data)
{
	FrApplication *self = user_data;
	guint          registration_id;
	GError        *error = NULL;

	registration_id = g_dbus_connection_register_object (connection,
							     "/org/gnome/ArchiveManager1",
							     self->introspection_data->interfaces[0],
							     &interface_vtable,
							     NULL,
							     NULL,  /* user_data_free_func */
							     &error); /* GError** */
	if (registration_id == 0) {
		g_error ("%s", error->message);
		g_clear_error (&error);
	}
}


static gboolean
service_timeout_cb (gpointer user_data)
{
	g_application_release (G_APPLICATION (user_data));
	return FALSE;
}


static void
fr_application_register_archive_manager_service (FrApplication *self)
{
	gsize         size;
	guchar       *buffer;
	GInputStream *stream;
	gsize         bytes_read;
	GError       *error = NULL;

	g_application_hold (G_APPLICATION (self));

	g_resources_get_info (ORG_GNOME_ARCHIVEMANAGER_XML, 0, &size, NULL, NULL);
	buffer = g_new (guchar, size + 1);
	stream = g_resources_open_stream (ORG_GNOME_ARCHIVEMANAGER_XML, 0, NULL);
	if (g_input_stream_read_all (stream, buffer, size, &bytes_read, NULL, NULL)) {
		buffer[bytes_read] = '\0';

		self->introspection_data = g_dbus_node_info_new_for_xml ((gchar *) buffer, &error);
		if (self->introspection_data != NULL) {
			self->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
							 "org.gnome.ArchiveManager1",
							 G_BUS_NAME_OWNER_FLAGS_NONE,
							 on_bus_acquired_for_archive_manager,
							 NULL /*on_name_acquired*/,
							 NULL /*on_name_lost*/,
							 self,
							 NULL);
		}
		else {
			g_warning ("%s", error->message);
			g_clear_error (&error);
		}
	}

	g_timeout_add_seconds (SERVICE_TIMEOUT, service_timeout_cb, self);

	g_free (buffer);
}


static void
fr_application_init (FrApplication *self)
{
#ifdef GDK_WINDOWING_X11
	egg_set_desktop_file (APPLICATIONS_DIR "/file-roller.desktop");
#else
	/* manually set name and icon */
	g_set_application_name (_("File Roller"));
	gtk_window_set_default_icon_name ("file-roller");
#endif

#ifdef ENABLE_NOTIFICATION
	if (! notify_init (g_get_application_name ()))
                g_warning ("Cannot initialize notification system.");
#endif /* ENABLE_NOTIFICATION */

	self->owner_id = 0;
	self->introspection_data = NULL;
}


static void
fr_application_startup (GApplication *application)
{
	G_APPLICATION_CLASS (fr_application_parent_class)->startup (application);

	fr_application_register_archive_manager_service (FR_APPLICATION (application));
	initialize_data ();
	initialize_app_menu (application);
}


static GOptionContext *
fr_application_create_option_context (void)
{
	GOptionContext *context;
	static gsize    initialized = FALSE;

	context = g_option_context_new (N_("- Create and modify an archive"));
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);
	g_option_context_set_ignore_unknown_options (context, TRUE);

	if (g_once_init_enter (&initialized)) {
		g_option_context_add_group (context, gtk_get_option_group (TRUE));
#ifdef USE_SMCLIENT
		g_option_context_add_group (context, egg_sm_client_get_option_group ());
#endif
		g_once_init_leave (&initialized, TRUE);
	}

	return context;
}


static int
fr_application_command_line_finished (GApplication *application,
				      int           status)
{
	if (status == EXIT_SUCCESS)
		gdk_notify_startup_complete ();

	/* reset arguments */

	remaining_args = NULL;
	arg_add_to = NULL;
	arg_add = FALSE;
	arg_extract_to = NULL;
	arg_extract = FALSE;
	arg_extract_here = FALSE;
	arg_default_dir = NULL;
	arg_version = FALSE;

	return status;
}


static int
fr_application_command_line (GApplication            *application,
                             GApplicationCommandLine *command_line)
{
	char           **argv;
	int              argc;
	GOptionContext  *context;
	GError          *error = NULL;
	GFile           *extraction_destination = NULL;
	GFile           *add_to_archive = NULL;
	GFile           *default_directory = NULL;

	argv = g_application_command_line_get_arguments (command_line, &argc);

	/* parse command line options */

	context = fr_application_create_option_context ();
	if (! g_option_context_parse (context, &argc, &argv, &error)) {
		g_critical ("Failed to parse arguments: %s", error->message);
		g_error_free (error);
		g_option_context_free (context);

		return fr_application_command_line_finished (application, EXIT_FAILURE);
	}
	g_option_context_free (context);

	/* restore the session */

#ifdef USE_SMCLIENT
	{
		EggSMClient *client;

		client = egg_sm_client_get ();
		g_signal_connect (client,
				  "save_state",
				  G_CALLBACK (client_save_state),
				  NULL);
		g_signal_connect (client,
				  "quit",
				  G_CALLBACK (client_quit_cb),
				  NULL);
		if (egg_sm_client_is_resumed (client)) {
			fr_restore_session (client);
			return fr_application_command_line_finished (application, EXIT_SUCCESS);
		}
	}
#endif

	if (remaining_args == NULL) { /* No archive specified. */
		if (! arg_service)
			gtk_widget_show (fr_window_new ());
		return fr_application_command_line_finished (application, EXIT_SUCCESS);
	}

	if (arg_extract_to != NULL)
		extraction_destination = g_file_new_for_commandline_arg (arg_extract_to);

	if (arg_add_to != NULL)
		add_to_archive = g_file_new_for_commandline_arg (arg_add_to);

	if (arg_default_dir != NULL)
		default_directory = g_file_new_for_commandline_arg (arg_default_dir);

	if ((arg_add_to != NULL) || (arg_add == 1)) { /* Add files to an archive */
		GtkWidget   *window;
		GList       *file_list;
		const char  *filename;
		int          i = 0;

		window = fr_window_new ();

		if (default_directory != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), default_directory, TRUE);

		file_list = NULL;
		while ((filename = remaining_args[i++]) != NULL)
			file_list = g_list_prepend (file_list, g_file_new_for_commandline_arg (filename));
		file_list = g_list_reverse (file_list);

		fr_window_new_batch (FR_WINDOW (window), _("Compress"));
		fr_window_set_batch__add (FR_WINDOW (window), add_to_archive, file_list);

		if (! arg_notify)
			fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		else
			fr_window_set_notify (FR_WINDOW (window), TRUE);
		fr_window_start_batch (FR_WINDOW (window));

		_g_object_list_unref (file_list);
	}
	else if ((arg_extract_to != NULL) || (arg_extract == 1) || (arg_extract_here == 1)) {

		/* Extract all archives. */

		GtkWidget  *window;
		const char *archive;
		int         i = 0;

		window = fr_window_new ();

		if (default_directory != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), default_directory, TRUE);

		fr_window_new_batch (FR_WINDOW (window), _("Extract archive"));
		while ((archive = remaining_args[i++]) != NULL) {
			GFile *file;

			file = g_file_new_for_commandline_arg (archive);
			if (arg_extract_here == 1)
				fr_window_set_batch__extract_here (FR_WINDOW (window), file);
			else
				fr_window_set_batch__extract (FR_WINDOW (window), file, extraction_destination);

			g_object_unref (file);
		}
		if (! arg_notify)
			fr_window_append_batch_action (FR_WINDOW (window), FR_BATCH_ACTION_QUIT, NULL, NULL);
		else
			fr_window_set_notify (FR_WINDOW (window), TRUE);

		fr_window_start_batch (FR_WINDOW (window));
	}
	else { /* Open each archive in a window */
		const char *filename = NULL;

		int i = 0;
		while ((filename = remaining_args[i++]) != NULL) {
			GtkWidget *window;
			GFile     *file;

			window = fr_window_new ();
			gtk_widget_show (window);

			file = g_file_new_for_commandline_arg (filename);
			fr_window_archive_open (FR_WINDOW (window), file, GTK_WINDOW (window));

			g_object_unref (file);
		}
	}

	_g_object_unref (default_directory);
	_g_object_unref (add_to_archive);
	_g_object_unref (extraction_destination);

	return fr_application_command_line_finished (application, EXIT_SUCCESS);
}


static gboolean
fr_application_local_command_line (GApplication   *application,
                                   char         ***arguments,
                                   int            *exit_status)
{
        char           **local_argv;
        int              local_argc;
        GOptionContext  *context;
        GError          *error = NULL;
        gboolean         handled_locally = FALSE;

        local_argv = g_strdupv (*arguments);
        local_argc = g_strv_length (local_argv);

        *exit_status = 0;

        context = fr_application_create_option_context ();
        g_option_context_set_ignore_unknown_options (context, TRUE);
	if (! g_option_context_parse (context, &local_argc, &local_argv, &error)) {
		*exit_status = EXIT_FAILURE;
		g_critical ("Failed to parse arguments: %s", error->message);
                g_clear_error (&error);
                handled_locally = TRUE;
	}

	if (arg_version) {
		g_printf ("%s %s, Copyright © 2001-2012 Free Software Foundation, Inc.\n", PACKAGE_NAME, PACKAGE_VERSION);
		handled_locally = TRUE;
	}

	g_option_context_free (context);
        g_strfreev (local_argv);

        return handled_locally;
}


static void
fr_application_activate (GApplication *application)
{
	GList *link;

	for (link = gtk_application_get_windows (GTK_APPLICATION (application));
	     link != NULL;
	     link = link->next)
	{
		if (! fr_window_is_batch_mode (FR_WINDOW (link->data)))
			gtk_widget_show (GTK_WIDGET (link->data));
	}

	gdk_notify_startup_complete ();
}


static void
fr_application_class_init (FrApplicationClass *klass)
{
	GObjectClass      *object_class;
	GApplicationClass *application_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fr_application_finalize;

	application_class = G_APPLICATION_CLASS (klass);
	application_class->startup = fr_application_startup;
	application_class->command_line = fr_application_command_line;
	application_class->local_command_line = fr_application_local_command_line;
	application_class->activate = fr_application_activate;
}


static GtkApplication *
fr_application_new (void)
{
        return g_object_new (fr_application_get_type (),
                             "application-id", "org.gnome.FileRoller",
                             "flags", G_APPLICATION_FLAGS_NONE,
                             NULL);
}


/* -- main -- */


int
main (int argc, char **argv)
{
	GtkApplication *app;
	int             status;

	program_argv0 = argv[0];

	g_type_init ();

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	app = fr_application_new ();
	status = g_application_run (G_APPLICATION (app), argc, argv);

	release_data ();
	g_object_unref (app);

	return status;
}
