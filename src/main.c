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
#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <glade/glade.h>
#include "file-utils.h"
#include "fr-process.h"
#include "gconf-utils.h"
#include "window.h"
#include "typedefs.h"
#include "preferences.h"
#include "main.h"

static void     prepare_app         (poptContext pctx);
static void     initialize_data     (void);
static void     release_data        (void);

static void     init_session        (char **argv);
static gboolean session_is_restored (void);
static gboolean load_session        (void);


GList        *window_list = NULL;
GList        *viewer_list = NULL;
GList        *command_list = NULL;
gint          force_directory_creation;

static gchar *add_to = NULL;
static gint   add;
static gchar *extract_to = NULL;
static gint   extract;
static gchar *default_url = NULL;

struct poptOption options[] = {
	{ "add-to", 'a', POPT_ARG_STRING, &add_to, 0,
	  N_("Add files to the specified archive and quit the program"),
	  N_("ARCHIVE") },

	{ "add", 'd', POPT_ARG_NONE, &add, 0,
	  N_("Add files asking the name of the archive and quit the program"),
	  0 },

	{ "extract-to", 'e', POPT_ARG_STRING, &extract_to, 0,
          N_("Extract archives to the specified folder and quit the program"),
          N_("FOLDER") },

	{ "extract", 'f', POPT_ARG_NONE, &extract, 0,
          N_("Extract archives asking the destination folder and quit the program"),
          0 },

	{ "default-dir", 0, POPT_ARG_STRING, &default_url, 0,
	  N_("Default folder to use for the '--add' and '--extract' commands"),
	  N_("FOLDER") },

        { "force", 0, POPT_ARG_NONE, &force_directory_creation, 0,
          N_("Create destination folder without asking confirmation"),
          0 },
 
	{ NULL, '\0', 0, NULL, 0 }
};


/* -- Main -- */

int main (int argc, char **argv)
{
	GnomeProgram *program;
	GValue value = { 0 };
	poptContext pctx;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	program = gnome_program_init ("file-roller", VERSION,
				      LIBGNOMEUI_MODULE, argc, argv,
				      GNOME_PARAM_POPT_TABLE, options,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("File Roller"),
				      GNOME_PARAM_APP_PREFIX, FR_PREFIX,
                                      GNOME_PARAM_APP_SYSCONFDIR, FR_SYSCONFDIR,
                                      GNOME_PARAM_APP_DATADIR, FR_DATADIR,
                                      GNOME_PARAM_APP_LIBDIR, FR_LIBDIR,

				      NULL);

	g_object_get_property (G_OBJECT (program),
			       GNOME_PARAM_POPT_CONTEXT,
			       g_value_init (&value, G_TYPE_POINTER));
	pctx = g_value_get_pointer (&value);

	if (! gnome_vfs_init ()) 
                g_error ("Cannot initialize the Virtual File System.");

	glade_gnome_init ();

	init_session (argv);

	initialize_data ();

	prepare_app (pctx);
	poptFreeContext (pctx);

	gtk_main ();

	release_data ();

	return 0;
}

/* Initialize application data. */


static void 
initialize_data ()
{
	char *icon_path = PIXMAPSDIR "/file-roller.png";

	if (! g_file_test (icon_path, G_FILE_TEST_EXISTS))
                g_warning ("Could not find %s", icon_path);
	else
		gnome_window_icon_set_default_from_file (icon_path);

	eel_gconf_monitor_add ("/apps/file-roller");
}

/* Free application data. */

void 
viewer_done (ViewerData *vdata)
{
	if ((vdata->temp_dir != NULL) 
	    && path_is_dir (vdata->temp_dir)) {
		char *argv[4];
		
		argv[0] = "rm";
		argv[1] = "-rf";
		argv[2] = vdata->temp_dir;
		argv[3] = NULL;
		g_spawn_sync (g_get_tmp_dir (), argv, NULL, 
			      G_SPAWN_SEARCH_PATH, 
			      NULL, NULL, 
			      NULL, NULL, NULL,
			      NULL);
	}

	g_free (vdata->filename);
	g_free (vdata->e_filename);
	g_free (vdata->temp_dir);
	if (vdata->process != NULL) 
		g_object_unref (vdata->process);

	viewer_list = g_list_remove (viewer_list, vdata);
	g_free (vdata);
}


void 
command_done (CommandData *cdata)
{
	if ((cdata->temp_dir != NULL) 
	    && path_is_dir (cdata->temp_dir)) {
		char *argv[4];
		
		argv[0] = "rm";
		argv[1] = "-rf";
		argv[2] = cdata->temp_dir;
		argv[3] = NULL;
		g_spawn_sync (g_get_tmp_dir (), argv, NULL, 
			      G_SPAWN_SEARCH_PATH, 
			      NULL, NULL, 
			      NULL, NULL, NULL,
			      NULL);
	}

	g_free (cdata->command);
	path_list_free (cdata->file_list);
	g_free (cdata->temp_dir);
	if (cdata->process != NULL) 
		g_object_unref (cdata->process);

	command_list = g_list_remove (command_list, cdata);
	g_free (cdata);
}


static void 
release_data ()
{
	eel_global_client_free ();

	while (viewer_list != NULL) {
		ViewerData *vdata = viewer_list->data;
		viewer_done (vdata);
	}

	while (command_list != NULL) {
		CommandData *cdata = command_list->data;
		command_done (cdata);
	}
}

/* Create the windows. */

static char *
get_path_from_url (char *url)
{
	GnomeVFSURI *uri;
	char        *path;
	char        *escaped;

	if (url == NULL)
		return NULL;

	uri = gnome_vfs_uri_new (url);
	escaped = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
	gnome_vfs_uri_unref (uri);
	path = gnome_vfs_unescape_string (escaped, NULL);
	g_free (escaped);

	{
		char *line = g_strdup_printf ("echo \"[1] %s\" >> /home/gino/fr_log", path);
		system (line);
		g_free (line);
	}


	return path;
}


static void
migrate_dir_from_to (const char *from_dir,
                     const char *to_dir)
{
        char *from_path;
        char *to_path;

        from_path = get_home_relative_dir (from_dir);
        to_path = get_home_relative_dir (to_dir);

        if (path_is_dir (from_path) && ! path_is_dir (to_path)) {
                char *line;
                char *e1;
                char *e2;

                e1 = shell_escape (from_path);
                e2 = shell_escape (to_path);
                line = g_strdup_printf ("mv -f %s %s", e1, e2);
                g_free (e1);
                g_free (e2);

                g_spawn_command_line_sync (line, NULL, NULL, NULL, NULL);  
                g_free (line);
        }

        g_free (from_path);
        g_free (to_path);
}


static void
migrate_file_from_to (const char *from_file,
                      const char *to_file)
{
        char *from_path;
        char *to_path;

        from_path = get_home_relative_dir (from_file);
        to_path = get_home_relative_dir (to_file);

        if (path_is_file (from_path) && ! path_is_file (to_path)) {
                char *line;
                char *e1;
                char *e2;

                e1 = shell_escape (from_path);
                e2 = shell_escape (to_path);
                line = g_strdup_printf ("mv -f %s %s", e1, e2);
                g_free (e1);
                g_free (e2);

                g_spawn_command_line_sync (line, NULL, NULL, NULL, NULL);  
                g_free (line);
        }

        g_free (from_path);
        g_free (to_path);
}


static void
migrate_to_new_directories (void)
{
	migrate_dir_from_to  (OLD_RC_OPTIONS_DIR, RC_OPTIONS_DIR);
	migrate_file_from_to (OLD_RC_BOOKMARKS_FILE, RC_BOOKMARKS_FILE);
        migrate_file_from_to (OLD_RC_RECENT_FILE, RC_RECENT_FILE);

	eel_gconf_set_boolean (PREF_MIGRATE_DIRECTORIES, FALSE);
}


static void 
prepare_app (poptContext pctx)
{
	char *path;
	char *default_dir;
	char *extract_to_path;
        char *add_to_path;

	/* create the config dir if necessary. */

	path = get_home_relative_dir (RC_DIR);
	ensure_dir_exists (path, 0700);
	g_free (path);

	if (eel_gconf_get_boolean (PREF_MIGRATE_DIRECTORIES))
                migrate_to_new_directories ();

	if (session_is_restored ()) {
		load_session ();
		return;
	}

	/**/

	if (poptPeekArg (pctx) == NULL) { /* No archive specified. */
		FRWindow *window;
		window = window_new ();
		gtk_widget_show (window->app);
		return;
	} 

	default_dir = get_path_from_url (default_url);
	extract_to_path = get_path_from_url (extract_to);
        add_to_path = get_path_from_url (add_to);

	if ((add_to != NULL) || (add == 1)) { /* Add files to an archive */
		FRWindow    *window;
		GList       *file_list = NULL;
		const char  *filename;
		
		window = window_new ();
		if (default_dir != NULL)
			window_set_default_dir (window, default_dir);
		gtk_widget_show (window->app);
		
		while ((filename = poptGetArg (pctx)) != NULL) {
			char *path;
			
			if (! g_path_is_absolute (filename)) {
				char *current_dir;
				current_dir = g_get_current_dir ();
				path = g_strconcat (current_dir, 
						    "/", 
						    filename, 
						    NULL);
				g_free (current_dir);
			} else
				path = g_strdup (filename);
			file_list = g_list_prepend (file_list, path);
		}
		file_list = g_list_reverse (file_list);
		
		window_archive__open_add (window, add_to_path, file_list);
		window_archive__close (window);
		window_batch_mode_start (window);

	} else if ((extract_to != NULL) || (extract == 1)) { /* Extract all archives. */
		FRWindow   *window;
		const char *archive;
		
		window = window_new ();
		if (default_dir != NULL)
			window_set_default_dir (window, default_dir);
		gtk_widget_show (window->app);
		
		while ((archive = poptGetArg (pctx)) != NULL) 
			window_archive__open_extract (window, 
						      archive, 
						      extract_to_path);
		window_archive__close (window);
		window_batch_mode_start (window);

	} else { /* Open each archives in a window */
		const char *archive;
		
		while ((archive = poptGetArg (pctx)) != NULL) {
			FRWindow *window;
			
			window = window_new ();
			gtk_widget_show (window->app);
			window_archive_open (window, archive);
		}
	}
	
	g_free (default_dir);
	g_free (add_to_path);
        g_free (extract_to_path);
}


 /* SM support */


/* The master client we use for SM */
static GnomeClient *master_client = NULL;

/* argv[0] from main(); used as the command to restart the program */
static const char *program_argv0 = NULL;


static void
save_session (GnomeClient *client)
{
	const char  *prefix;
	GList        *scan;
	int          i = 0;

	prefix = gnome_client_get_config_prefix (client);
	gnome_config_push_prefix (prefix);

	for (scan = window_list; scan; scan = scan->next) {
		FRWindow *window = scan->data;
		char     *key;

		if ((window->archive == NULL) 
		    || (window->archive->filename == NULL))
			continue;

		key = g_strdup_printf ("Session/archive%d", i);
		gnome_config_set_string (key, window->archive->filename);
		g_free (key);

		i++;
	}

	gnome_config_set_int ("Session/archives", i);

	gnome_config_pop_prefix ();
	gnome_config_sync ();
}


/* save_yourself handler for the master client */
static gboolean
client_save_yourself_cb (GnomeClient *client,
			 gint phase,
			 GnomeSaveStyle save_style,
			 gboolean shutdown,
			 GnomeInteractStyle interact_style,
			 gboolean fast,
			 gpointer data)
{
	const char *prefix;
	char       *argv[4] = { NULL };

	save_session (client);

	prefix = gnome_client_get_config_prefix (client);

	/* Tell the session manager how to discard this save */

	argv[0] = "rm";
	argv[1] = "-rf";
	argv[2] = gnome_config_get_real_path (prefix);
	argv[3] = NULL;
	gnome_client_set_discard_command (client, 3, argv);

	/* Tell the session manager how to clone or restart this instance */

	argv[0] = (char *) program_argv0;
	argv[1] = NULL; /* "--debug-session"; */
	
	gnome_client_set_clone_command (client, 1, argv);
	gnome_client_set_restart_command (client, 1, argv);

	return TRUE;
}

/* die handler for the master client */
static void
client_die_cb (GnomeClient *client, gpointer data)
{
	if (! client->save_yourself_emitted)
		save_session (client);

	gtk_main_quit ();
}


static void
init_session (char **argv)
{
	if (master_client != NULL)
		return;

	program_argv0 = argv[0];

	master_client = gnome_master_client ();

	g_signal_connect (master_client, "save_yourself",
			  G_CALLBACK (client_save_yourself_cb),
			  NULL);

	g_signal_connect (master_client, "die",
			  G_CALLBACK (client_die_cb),
			  NULL);
}


gboolean
session_is_restored (void)
{
	gboolean restored;
	
	if (! master_client)
		return FALSE;

	restored = (gnome_client_get_flags (master_client) & GNOME_CLIENT_RESTORED) != 0;

	return restored;
}


gboolean
load_session (void)
{
	int i, n;
	
	gnome_config_push_prefix (gnome_client_get_config_prefix (master_client));

	n = gnome_config_get_int ("Session/archives");
	for (i = 0; i < n; i++) {
		FRWindow *window;
		char     *key;
		char     *filename;

		key = g_strdup_printf ("Session/archive%d", i);
		filename = gnome_config_get_string (key);
		g_free (key);

		window = window_new ();
		gtk_widget_show (window->app);
		window_archive_open (window, filename);

		g_free (filename);
	}

	gnome_config_pop_prefix ();

	return TRUE;
}
