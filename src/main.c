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

#define USE_SMCLIENT 1

#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib.h>
#include <gio/gio.h>
#include "actions.h"
#ifdef USE_SMCLIENT
#  include "eggsmclient.h"
#endif
#include "eggdesktopfile.h"
#include "fr-init.h"
#include "gtk-utils.h"


gint              ForceDirectoryCreation;

static char     **remaining_args;
static char      *arg_add_to = NULL;
static int        arg_add = FALSE;
static char      *arg_extract_to = NULL;
static int        arg_extract = FALSE;
static int        arg_extract_here = FALSE;
static char      *arg_default_url = NULL;
static gboolean   arg_version = FALSE;

/* argv[0] from main(); used as the command to restart the program */
static const char *program_argv0 = NULL;

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

	{ "default-dir", '\0', 0, G_OPTION_ARG_STRING, &arg_default_url,
	  N_("Default folder to use for the '--add' and '--extract' commands"),
	  N_("FOLDER") },

	{ "force", '\0', 0, G_OPTION_ARG_NONE, &ForceDirectoryCreation,
	  N_("Create destination folder without asking confirmation"),
	  NULL },

	{ "version", 'v', 0, G_OPTION_ARG_NONE, &arg_version,
	  N_("Show version"), NULL },

	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args,
	  NULL,
	  NULL },

	{ NULL }
};


/* -- app menu -- */


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
	{ "help",  activate_help },
	{ "about", activate_about },
	{ "quit",  activate_quit }
};


static void
initialize_app_menu (GApplication *application)
{
	GtkBuilder *builder;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);

	builder = _gtk_builder_new_from_resource ("app-menu.ui");
	gtk_application_set_app_menu (GTK_APPLICATION (application),
				      G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu")));

	g_object_unref (builder);
}


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
			gchar *key;

			key = g_strdup_printf ("archive%d", i);
			if ((session->archive == NULL) || (session->archive->file == NULL)) {
				g_key_file_set_string (state, "Session", key, "");
			}
			else {
				gchar *uri;

				uri = g_file_get_uri (session->archive->file);
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
		gchar *key, *archive;

		key = g_strdup_printf ("archive%d", i);
		archive = g_key_file_get_string (state, "Session", key, NULL);
		g_free (key);

		window = fr_window_new ();
		if (strlen (archive))
			fr_window_archive_open (FR_WINDOW (window), archive, GTK_WINDOW (window));

		g_free (archive);
	}
}


#endif /* USE_SMCLIENT */


/* -- main application -- */


typedef GtkApplication      FrApplication;
typedef GtkApplicationClass FrApplicationClass;

static gpointer fr_application_parent_class;


G_DEFINE_TYPE (FrApplication, fr_application, GTK_TYPE_APPLICATION)


static void
fr_application_finalize (GObject *object)
{
        G_OBJECT_CLASS (fr_application_parent_class)->finalize (object);
}


static void
fr_application_init (FrApplication *app)
{
#ifdef GDK_WINDOWING_X11
	egg_set_desktop_file (APPLICATIONS_DIR "/file-roller.desktop");
#else
	/* manually set name and icon */
	g_set_application_name (_("File Roller"));
	gtk_window_set_default_icon_name ("file-roller");
#endif
}


static void
fr_application_startup (GApplication *application)
{
	G_APPLICATION_CLASS (fr_application_parent_class)->startup (application);

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
	g_option_context_set_ignore_unknown_options (context, TRUE);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	if (g_once_init_enter (&initialized)) {
		g_option_context_add_group (context, gtk_get_option_group (TRUE));
#ifdef USE_SMCLIENT
		g_option_context_add_group (context, egg_sm_client_get_option_group ());
#endif
		g_once_init_leave (&initialized, TRUE);
	}

	return context;
}


static char *
get_uri_from_command_line (const char *path)
{
	GFile *file;
	char  *uri;

	file = g_file_new_for_commandline_arg (path);
	uri = g_file_get_uri (file);
	g_object_unref (file);

	return uri;
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
	arg_default_url = NULL;
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
	char            *extract_to_uri = NULL;
	char            *add_to_uri = NULL;

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
		gtk_widget_show (fr_window_new ());
		return fr_application_command_line_finished (application, EXIT_SUCCESS);
	}

	if (arg_extract_to != NULL)
		extract_to_uri = get_uri_from_command_line (arg_extract_to);

	if (arg_add_to != NULL)
		add_to_uri = get_uri_from_command_line (arg_add_to);

	if ((arg_add_to != NULL) || (arg_add == 1)) { /* Add files to an archive */
		GtkWidget   *window;
		GList       *file_list = NULL;
		const char  *filename;
		int          i = 0;

		window = fr_window_new ();

		if (arg_default_url != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), arg_default_url, TRUE);

		while ((filename = remaining_args[i++]) != NULL)
			file_list = g_list_prepend (file_list, get_uri_from_command_line (filename));
		file_list = g_list_reverse (file_list);

		fr_window_new_batch (FR_WINDOW (window), _("Compress"));
		fr_window_set_batch__add (FR_WINDOW (window), add_to_uri, file_list);
		fr_window_append_batch_action (FR_WINDOW (window),
					       FR_BATCH_ACTION_QUIT,
					       NULL,
					       NULL);
		fr_window_start_batch (FR_WINDOW (window));
	}
	else if ((arg_extract_to != NULL) || (arg_extract == 1) || (arg_extract_here == 1)) {

		/* Extract all archives. */

		GtkWidget  *window;
		const char *archive;
		int         i = 0;

		window = fr_window_new ();

		if (arg_default_url != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), arg_default_url, TRUE);

		fr_window_new_batch (FR_WINDOW (window), _("Extract archive"));
		while ((archive = remaining_args[i++]) != NULL) {
			char *archive_uri;

			archive_uri = get_uri_from_command_line (archive);
			if (arg_extract_here == 1)
				fr_window_set_batch__extract_here (FR_WINDOW (window),
								   archive_uri);
			else
				fr_window_set_batch__extract (FR_WINDOW (window),
							      archive_uri,
							      extract_to_uri);
			g_free (archive_uri);
		}
		fr_window_append_batch_action (FR_WINDOW (window),
					       FR_BATCH_ACTION_QUIT,
					       NULL,
					       NULL);

		fr_window_start_batch (FR_WINDOW (window));
	}
	else { /* Open each archive in a window */
		const char *filename = NULL;

		int i = 0;
		while ((filename = remaining_args[i++]) != NULL) {
			GtkWidget *window;
			GFile     *file;
			char      *uri;

			window = fr_window_new ();
			gtk_widget_show (window);

			file = g_file_new_for_commandline_arg (filename);
			uri = g_file_get_uri (file);
			fr_window_archive_open (FR_WINDOW (window), uri, GTK_WINDOW (window));

			g_free (uri);
			g_object_unref (file);
		}
	}

	g_free (add_to_uri);
	g_free (extract_to_uri);

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
                             "application-id", "org.gnome.file-roller",
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
