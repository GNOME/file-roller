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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <string.h>

#include <libgnome/gnome-config.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <glade/glade.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-process.h"
#include "fr-stock.h"
#include "gconf-utils.h"
#include "fr-window.h"
#include "typedefs.h"
#include "preferences.h"
#include "file-data.h"
#include "main.h"

static void     prepare_app         (void);
static void     initialize_data     (void);
static void     release_data        (void);

static void     init_session        (char **argv);
static gboolean session_is_restored (void);
static gboolean load_session        (void);

GList        *WindowList = NULL;
GList        *ViewerList = NULL;
GList        *CommandList = NULL;
gint          ForceDirectoryCreation;
GHashTable   *ProgramsCache = NULL;

static gchar **remaining_args;

static gchar *add_to = NULL;
static gint   add;
static gchar *extract_to = NULL;
static gint   extract;
static gint   extract_here;
static gchar *default_url = NULL;

FRFileTypeDescription file_type_desc[] = {
	{ FR_FILE_TYPE_ACE,          ".ace",     "application/x-ace", N_("Ace (.ace)") },
	{ FR_FILE_TYPE_AR,           ".ar",      "application/x-ar", N_("Ar (.ar)") },
	{ FR_FILE_TYPE_ARJ,          ".arj",     "application/x-arj", N_("Arj (.arj)") },
	{ FR_FILE_TYPE_BZIP,         ".bz",      "application/x-bzip", NULL },
	{ FR_FILE_TYPE_BZIP2,        ".bz2",     "application/x-bzip", NULL },
	{ FR_FILE_TYPE_COMPRESS,     ".Z",       "application/x-compress", NULL },
	{ FR_FILE_TYPE_CPIO,         ".cpio",    "application/x-cpio", NULL },
	{ FR_FILE_TYPE_DEB,          ".deb",     "application/x-deb", NULL },
	{ FR_FILE_TYPE_ISO,          ".iso",     "application/x-cd-image", NULL },
	{ FR_FILE_TYPE_EAR,          ".ear",     "application/x-ear", N_("Ear (.ear)") },
	{ FR_FILE_TYPE_EXE,          ".exe",     "application/x-ms-dos-executable", N_("Self-extracting zip (.exe)") },
	{ FR_FILE_TYPE_GZIP,         ".gz",      "application/x-gzip", NULL},
	{ FR_FILE_TYPE_JAR,          ".jar",     "application/x-jar", N_("Jar (.jar)")},
	{ FR_FILE_TYPE_LHA,          ".lzh",     "application/x-lha", N_("Lha (.lzh)") },
	{ FR_FILE_TYPE_LZMA,         ".lzma",    "application/x-lzma", NULL },
	{ FR_FILE_TYPE_LZOP,         ".lzo",     "application/x-lzop", NULL },
	{ FR_FILE_TYPE_RAR,          ".rar",     "application/x-rar", N_("Rar (.rar)") },
	{ FR_FILE_TYPE_RPM,          ".rpm",     "application/x-rpm", NULL },
	{ FR_FILE_TYPE_TAR,          ".tar",     "application/x-tar", N_("Tar uncompressed (.tar)") },
	{ FR_FILE_TYPE_TAR_BZ,       ".tar.bz",  "application/x-bzip-compressed-tar", N_("Tar compressed with bzip (.tar.bz)") },
	{ FR_FILE_TYPE_TAR_BZ2,      ".tar.bz2", "application/x-bzip-compressed-tar", N_("Tar compressed with bzip2 (.tar.bz2)") },
	{ FR_FILE_TYPE_TAR_GZ,       ".tar.gz",  "application/x-compressed-tar", N_("Tar compressed with gzip (.tar.gz)") },
	{ FR_FILE_TYPE_TAR_LZMA,     ".tar.lzma","application/x-lzma-compressed-tar", N_("Tar compressed with lzma (.tar.lzma)") },
	{ FR_FILE_TYPE_TAR_LZOP,     ".tar.lzo", "application/x-lzop-compressed-tar", N_("Tar compressed with lzop (.tar.lzo)") },
	{ FR_FILE_TYPE_TAR_COMPRESS, ".tar.Z",   "application/x-compressed-tar", N_("Tar compressed with compress (.tar.Z)") },
	{ FR_FILE_TYPE_STUFFIT,      ".sit",     "application/x-stuffit", NULL },
	{ FR_FILE_TYPE_WAR,          ".war",     "application/zip", N_("War (.war)") },
	{ FR_FILE_TYPE_ZIP,          ".zip",     "application/zip", N_("Zip (.zip)") },
	{ FR_FILE_TYPE_ZOO,          ".zoo",     "application/x-zoo", N_("Zoo (.zoo)") },
	{ FR_FILE_TYPE_7ZIP,         ".7z",      "application/x-7z-compressed", N_("7-Zip (.7z)") }
};


FRFileType single_file_save_type[32];
FRFileType save_type[32];
FRFileType open_type[32];


FRCommandDescription command_desc[] = {
	{ "tar",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR },
	{ "zip",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_ZIP },
	{ "unzip",      TRUE,  FALSE, TRUE,  FR_FILE_TYPE_ZIP },
	{ "rar",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_RAR },
	{ "gzip",       TRUE,  TRUE,  FALSE, FR_FILE_TYPE_GZIP },
	{ "bzip2",      TRUE,  TRUE,  FALSE, FR_FILE_TYPE_BZIP2 },
	{ "unace",      TRUE,  FALSE, TRUE,  FR_FILE_TYPE_ACE },
	{ "ar",         TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_AR },
	{ "ar",         TRUE,  FALSE, TRUE,  FR_FILE_TYPE_DEB },
	{ "arj",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_ARJ },
	{ "bzip2",      TRUE,  FALSE, FALSE, FR_FILE_TYPE_BZIP },
	{ "compress",   TRUE,  TRUE,  FALSE, FR_FILE_TYPE_COMPRESS },
	{ "cpio",       TRUE,  FALSE, FALSE, FR_FILE_TYPE_CPIO },
	{ "isoinfo",    TRUE,  FALSE, TRUE,  FR_FILE_TYPE_ISO },
	{ "zip",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_EAR },
	{ "zip",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_JAR },
	{ "zip",        TRUE,  FALSE,  TRUE,  FR_FILE_TYPE_EXE },
	{ "lha",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_LHA },
	{ "lzma",       TRUE,  TRUE,  FALSE, FR_FILE_TYPE_LZMA },
	{ "lzop",       TRUE,  TRUE,  FALSE, FR_FILE_TYPE_LZOP },
	{ "rpm2cpio",   TRUE,  FALSE, TRUE,  FR_FILE_TYPE_RPM },
	{ "uncompress", TRUE,  FALSE, FALSE, FR_FILE_TYPE_COMPRESS },
	{ "unstuff",    TRUE,  FALSE, FALSE, FR_FILE_TYPE_STUFFIT },
	{ "zip",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_WAR },
	{ "zoo",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_ZOO },
	{ "7za",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_7ZIP },
	{ "7zr",        TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_7ZIP }
};

FRCommandDescription tar_command_desc[] = {
	{ "gzip",      TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR_GZ },
	{ "bzip2",     TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR_BZ2 },
	{ "bzip",      FALSE, TRUE,  TRUE,  FR_FILE_TYPE_TAR_BZ },
	{ "lzma",      TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR_LZMA },
	{ "lzop",      TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR_LZOP },
	{ "compress",  TRUE,  TRUE,  TRUE,  FR_FILE_TYPE_TAR_COMPRESS }
};


static const GOptionEntry options[] = {
	{ "add-to", 'a', 0, G_OPTION_ARG_STRING, &add_to,
	  N_("Add files to the specified archive and quit the program"),
	  N_("ARCHIVE") },

	{ "add", 'd', 0, G_OPTION_ARG_NONE, &add,
	  N_("Add files asking the name of the archive and quit the program"),
	  NULL },

	{ "extract-to", 'e', 0, G_OPTION_ARG_STRING, &extract_to,
	  N_("Extract archives to the specified folder and quit the program"),
	  N_("FOLDER") },

	{ "extract", 'f', 0, G_OPTION_ARG_NONE, &extract,
	  N_("Extract archives asking the destination folder and quit the program"),
	  NULL },

	{ "extract-here", 'h', 0, G_OPTION_ARG_NONE, &extract_here,
	  N_("Extract archives using the archive name as destination folder and quit the program"),
	  NULL },

	{ "default-dir", '\0', 0, G_OPTION_ARG_STRING, &default_url,
	  N_("Default folder to use for the '--add' and '--extract' commands"),
	  N_("FOLDER") },

	{ "force", '\0', 0, G_OPTION_ARG_NONE, &ForceDirectoryCreation,
	  N_("Create destination folder without asking confirmation"),
	  NULL },

	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args,
	  NULL,
	  NULL },

	{ NULL }
};


/* -- Main -- */


static guint startup_id = 0;
static GOptionContext *context;


static gboolean
startup_cb (gpointer data)
{
	g_source_remove (startup_id);
	startup_id = 0;

	initialize_data ();
	prepare_app ();

	return FALSE;
}


int
main (int argc, char **argv)
{
	GnomeProgram *program;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (N_("- Create and modify an archive"));
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);
	
	program = gnome_program_init ("file-roller", VERSION,
				      LIBGNOMEUI_MODULE, 
				      argc, argv,
				      GNOME_PARAM_GOPTION_CONTEXT, context,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Archive Manager"),
				      GNOME_PARAM_APP_PREFIX, FR_PREFIX,
				      GNOME_PARAM_APP_SYSCONFDIR, FR_SYSCONFDIR,
				      GNOME_PARAM_APP_DATADIR, FR_DATADIR,
				      GNOME_PARAM_APP_LIBDIR, FR_LIBDIR,
				      NULL);

	g_set_application_name (_("File Roller"));
	gtk_window_set_default_icon_name ("file-roller");

	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PKG_DATA_DIR G_DIR_SEPARATOR_S "icons");

/*
	if (! gnome_vfs_init ())
		g_error ("Cannot initialize the Virtual File System.");
*/

	gnome_authentication_manager_init ();
	glade_init ();
	fr_stock_init ();
	init_session (argv);
	startup_id = g_idle_add (startup_cb, NULL);
	gtk_main ();
	release_data ();

	return 0;
}

/* Initialize application data. */


static void
initialize_data (void)
{
	eel_gconf_monitor_add ("/apps/file-roller");
	eel_gconf_monitor_add (PREF_NAUTILUS_CLICK_POLICY);

	ProgramsCache = g_hash_table_new_full (g_str_hash,
						g_str_equal,
						g_free,
						NULL);
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

	ViewerList = g_list_remove (ViewerList, vdata);
	g_free (vdata);
}


void
command_done (CommandData *cdata)
{
	if (cdata == NULL)
		return;
		
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
	gnome_vfs_mime_application_free (cdata->app);
	path_list_free (cdata->file_list);
	g_free (cdata->temp_dir);
	if (cdata->process != NULL)
		g_object_unref (cdata->process);

	CommandList = g_list_remove (CommandList, cdata);
	g_free (cdata);
}


static void
release_data ()
{
	g_hash_table_destroy (ProgramsCache);
	file_data_release_data ();

	eel_global_client_free ();

	while (ViewerList != NULL) {
		ViewerData *vdata = ViewerList->data;
		viewer_done (vdata);
	}

	while (CommandList != NULL) {
		CommandData *cdata = CommandList->data;
		command_done (cdata);
	}
}

/* Create the windows. */


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
compute_supported_archive_types (void)
{
	int i, j;
	int sf_i = 0, s_i = 0, o_i = 0;

	for (i = 0; i < G_N_ELEMENTS (command_desc); i++) {
		FRCommandDescription com = command_desc[i];

		if (!is_program_in_path (com.command))
			continue;

		if (strcmp (com.command, "tar") == 0)
			for (j = 0; j < G_N_ELEMENTS (tar_command_desc); j++) {
				FRCommandDescription com2 = tar_command_desc[j];

				if (!is_program_in_path (com2.command))
					continue;
				open_type[o_i++] = com2.file_type;
				save_type[s_i++] = com2.file_type;
				single_file_save_type[sf_i++] = com2.file_type;
			}

		if (com.can_open)
			open_type[o_i++] = com.file_type;
		if (com.can_save && com.support_many_files)
			save_type[s_i++] = com.file_type;
		if (com.can_save)
			single_file_save_type[sf_i++] = com.file_type;
	}

	open_type[o_i++] = FR_FILE_TYPE_NULL;
	save_type[s_i++] = FR_FILE_TYPE_NULL;
	single_file_save_type[sf_i++] = FR_FILE_TYPE_NULL;
}


static char *
get_uri_from_command_line (const char *path)
{
	char *full_path;
	char *uri;

	if (uri_has_scheme (path))
		return g_strdup (path);

	if (g_path_is_absolute (path))
		full_path = g_strdup (path);
	else {
		char *current_dir;
		
		current_dir = g_get_current_dir ();
		full_path = g_build_filename (current_dir,
					      path,
					      NULL);
		g_free (current_dir);
	}

	uri = get_uri_from_local_path (full_path);
	g_free (full_path);

	return uri;
}


static void
prepare_app (void)
{
	char *path;
	char *extract_to_path = NULL;
	char *add_to_path = NULL;

	/* create the config dir if necessary. */

	path = get_home_relative_dir (RC_DIR);

	/* before the gconf port this was a file, now it's folder. */
	if (path_is_file (path))
		unlink (path);

	ensure_dir_exists (path, 0700);
	g_free (path);

	if (eel_gconf_get_boolean (PREF_MIGRATE_DIRECTORIES, TRUE))
		migrate_to_new_directories ();

	compute_supported_archive_types ();

	if (session_is_restored ()) {
		load_session ();
		return;
	}

	/**/

	if (remaining_args == NULL) { /* No archive specified. */
		gtk_widget_show (fr_window_new ());
		return;
	}

	if (extract_to != NULL)
		extract_to_path = get_uri_from_command_line (extract_to);

	if (add_to != NULL)
		add_to_path = get_uri_from_command_line (add_to);

	if ((add_to != NULL) || (add == 1)) { /* Add files to an archive */
		GtkWidget   *window;
		GList       *file_list = NULL;
		const char  *filename;
		int          i = 0;

		window = fr_window_new ();
		if (default_url != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), default_url, TRUE);

		while ((filename = remaining_args[i++]) != NULL)
			file_list = g_list_prepend (file_list, get_uri_from_command_line (filename));
		file_list = g_list_reverse (file_list);

		fr_window_new_batch (FR_WINDOW (window));
		fr_window_set_batch__add (FR_WINDOW (window), add_to_path, file_list);
		fr_window_append_batch_action (FR_WINDOW (window),
					       FR_BATCH_ACTION_QUIT,
					       NULL,
					       NULL);
		fr_window_start_batch (FR_WINDOW (window));
	}
	else if ((extract_to != NULL)
		  || (extract == 1)
		  || (extract_here == 1)) { /* Extract all archives. */
		GtkWidget  *window;
		const char *archive;
		int         i = 0;

		window = fr_window_new ();
		if (default_url != NULL)
			fr_window_set_default_dir (FR_WINDOW (window), default_url, TRUE);

		fr_window_new_batch (FR_WINDOW (window));
		while ((archive = remaining_args[i++]) != NULL) {
			if (extract_here == 1)
				fr_window_set_batch__extract_here (FR_WINDOW (window),
								   archive,
								   extract_to_path);
			else
				fr_window_set_batch__extract (FR_WINDOW (window),
							      archive,
							      extract_to_path);
		}
		fr_window_append_batch_action (FR_WINDOW (window),
					       FR_BATCH_ACTION_QUIT,
					       NULL,
					       NULL);

		if (eel_gconf_get_boolean (PREF_EXTRACT_VIEW_FOLDER, FALSE))
			/* open the destination folder only if we are 
			 * extracting a single archive. */					       
			fr_window_view_folder_after_extract (FR_WINDOW (window), (i - 1 == 1));
					       
		fr_window_start_batch (FR_WINDOW (window));
	}
	else { /* Open each archive in a window */
		const char *utf8_archive = NULL;
		char       *locale_archive = NULL;

		int i = 0;
		while ((utf8_archive = remaining_args[i++]) != NULL) {
			GtkWidget *window;
			char      *uri;
			
			window = fr_window_new ();
			gtk_widget_show (window);
			
			locale_archive = g_filename_from_utf8 (utf8_archive, -1, NULL, NULL, NULL);
			if (locale_archive == NULL)
				locale_archive = g_strdup (utf8_archive);
			uri = gnome_vfs_make_uri_from_shell_arg (locale_archive);
			g_free (locale_archive);

			fr_window_archive_open (FR_WINDOW (window), uri, GTK_WINDOW (window));

			g_free (uri);
		}
	}

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

	for (scan = WindowList; scan; scan = scan->next) {
		FrWindow *window = scan->data;
		char     *key;

		key = g_strdup_printf ("Session/archive%d", i);

		if ((window->archive == NULL) || (window->archive->uri == NULL))
			gnome_config_set_string (key, "");
		else
			gnome_config_set_string (key, window->archive->uri);
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
		GtkWidget *window;
		char      *key;
		char      *filename;

		key = g_strdup_printf ("Session/archive%d", i);
		filename = gnome_config_get_string (key);
		g_free (key);

		window = fr_window_new ();
		gtk_widget_show (window);
		if (strlen (filename) != 0)
			fr_window_archive_open (FR_WINDOW (window), filename, GTK_WINDOW (window));

		g_free (filename);
	}

	gnome_config_pop_prefix ();

	return TRUE;
}
