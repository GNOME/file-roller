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

#include <gio/gio.h>
#include <glade/glade.h>
#include <libgnomeui/libgnomeui.h>
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-ace.h"
#include "fr-command-ar.h"
#include "fr-command-arj.h"
#include "fr-command-cfile.h"
#include "fr-command-cpio.h"
#include "fr-command-iso.h"
#include "fr-command-jar.h"
#include "fr-command-lha.h"
#include "fr-command-rar.h"
#include "fr-command-rpm.h"
#include "fr-command-tar.h"
#include "fr-command-unstuff.h"
#include "fr-command-zip.h"
#include "fr-command-zoo.h"
#include "fr-command-7z.h"
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
GList        *CommandList = NULL;
gint          ForceDirectoryCreation;
GHashTable   *ProgramsCache = NULL;
GPtrArray    *Registered_Commands = NULL;

static char **remaining_args;

static char  *add_to = NULL;
static int    add;
static char  *extract_to = NULL;
static int    extract;
static int    extract_here;
static char  *default_url = NULL;

FrMimeTypeDescription mime_type_desc[] = {
	{ "application/x-ace",                 ".ace",      N_("Ace (.ace)"), FALSE, FALSE },
	{ "application/x-ar",                  ".ar",       N_("Ar (.ar)"), FALSE, TRUE },
	{ "application/x-arj",                 ".arj",      N_("Arj (.arj)"), TRUE, TRUE },
	{ "application/x-bzip",                ".bz2",      NULL, FALSE, FALSE },
	{ "application/x-bzip1",               ".bz",       NULL, FALSE, FALSE },
	{ "application/x-compress",            ".Z",        NULL, FALSE, FALSE },
	{ "application/x-cpio",                ".cpio",     NULL, FALSE, TRUE },
	{ "application/x-deb",                 ".deb",      NULL, FALSE, TRUE },
	{ "application/x-cd-image",            ".iso",      NULL, FALSE, TRUE },
	{ "application/x-ear",                 ".ear",      N_("Ear (.ear)"), TRUE, TRUE },
	{ "application/x-ms-dos-executable",   ".exe",      N_("Self-extracting zip (.exe)"), FALSE, TRUE },
	{ "application/x-gzip",                ".gz",       NULL, FALSE, FALSE },
	{ "application/x-jar",                 ".jar",      N_("Jar (.jar)"), TRUE, TRUE },
	{ "application/x-lha",                 ".lzh",      N_("Lha (.lzh)"), FALSE, TRUE },
	{ "application/x-lzma",                ".lzma",     NULL, FALSE, FALSE },
	{ "application/x-lzop",                ".lzo",      NULL, FALSE, FALSE },
	{ "application/x-rar",                 ".rar",      N_("Rar (.rar)"), TRUE, TRUE },
	{ "application/x-rpm",                 ".rpm",      NULL, FALSE, TRUE },
	{ "application/x-tar",                 ".tar",      N_("Tar uncompressed (.tar)"), FALSE, TRUE },
	{ "application/x-bzip1-compressed-tar", ".tar.bz",   N_("Tar compressed with bzip (.tar.bz)"), FALSE, TRUE },
	{ "application/x-bzip-compressed-tar", ".tar.bz2",  N_("Tar compressed with bzip2 (.tar.bz2)"), FALSE, TRUE },
	{ "application/x-compressed-tar",      ".tar.gz",   N_("Tar compressed with gzip (.tar.gz)"), FALSE, TRUE },
	{ "application/x-lzma-compressed-tar", ".tar.lzma", N_("Tar compressed with lzma (.tar.lzma)"), FALSE, TRUE },
	{ "application/x-lzop-compressed-tar", ".tar.lzo",  N_("Tar compressed with lzop (.tar.lzo)"), FALSE, TRUE },
	{ "application/x-compressed-tar",      ".tar.Z",    N_("Tar compressed with compress (.tar.Z)"), FALSE, TRUE },
	{ "application/x-stuffit",             ".sit",      NULL, FALSE, TRUE },
	{ "application/x-war",                 ".war",      N_("War (.war)"), TRUE, TRUE },
	{ "application/zip",                   ".zip",      N_("Zip (.zip)"), TRUE, TRUE },
	{ "application/x-zoo",                 ".zoo",      N_("Zoo (.zoo)"), FALSE, TRUE },
	{ "application/x-7z-compressed",       ".7z",       N_("7-Zip (.7z)"), TRUE, TRUE },
	{ NULL, NULL, NULL, FALSE, FALSE }
};

FrExtensionType file_ext_type[] = {
	{ ".7z", "application/x-7z-compressed" },
	{ ".ace", "application/x-ace" },
	{ ".ar", "application/x-ar" },
	{ ".arj", "application/x-arj" },
	{ ".bin", "application/x-stuffit" },
	{ ".bz", "application/x-bzip" },
	{ ".bz2", "application/x-bzip" },
	{ ".cpio", "application/x-cpio" },
	{ ".deb", "application/x-deb" },
	{ ".ear", "application/x-ear" },
	{ ".exe", "application/x-ms-dos-executable" },
	{ ".gz", "application/x-gzip" },
	{ ".iso", "application/x-cd-image" },
	{ ".jar", "application/x-jar" },
	{ ".lha", "application/x-lha" },
	{ ".lzh", "application/x-lha" },
	{ ".lzma", "application/x-lzma" },
	{ ".lzo", "application/x-lzop" },
	{ ".rar", "application/x-rar" },
	{ ".rpm", "application/x-rpm" },
	{ ".sit", "application/x-stuffit" },
	{ ".tar", "application/x-tar" },
	{ ".tar.bz", "application/x-bzip-compressed-tar" },
	{ ".tar.bz2", "application/x-bzip-compressed-tar" },
	{ ".tar.gz", "application/x-compressed-tar" },
	{ ".tar.lzma", "application/x-lzma-compressed-tar" },
	{ ".tar.lzo", "application/x-lzop-compressed-tar" },
	{ ".tar.Z", "application/x-compressed-tar" },
	{ ".taz", "application/x-compressed-tar" },
	{ ".tbz", "application/x-bzip-compressed-tar" },
	{ ".tbz2", "application/x-bzip-compressed-tar" },
	{ ".tgz", "application/x-compressed-tar" },
	{ ".tzma", "application/x-lzma-compressed-tar" },
	{ ".tzo", "application/x-lzop-compressed-tar" },
	{ ".war", "application/x-war" },
	{ ".z", "application/x-gzip" },
	{ ".Z", "application/x-gzip" },
	{ ".zip", "application/zip" },
	{ ".zoo", "application/x-zoo" }
};

int single_file_save_type[32];
int save_type[32];
int open_type[32];

FrCommandDescription command_desc[] = {
	{ "tar",        "application/x-tar", TRUE, TRUE },
	{ "zip",        "application/zip",  TRUE, TRUE },
	{ "unzip",      "application/zip", TRUE, FALSE },
	{ "rar",        "application/x-rar", TRUE, TRUE },
	{ "unrar",      "application/x-rar", TRUE, FALSE },
	{ "gzip",       "application/x-gzip", TRUE, TRUE },
	{ "bzip2",      "application/x-bzip", TRUE, TRUE },
	{ "unace",      "application/x-ace", TRUE, FALSE },
	{ "ar",         "application/x-ar", TRUE, TRUE },
	{ "ar",         "application/x-deb", TRUE, FALSE },
	{ "arj",        "application/x-arj", TRUE, TRUE },
	{ "bzip2",      "application/x-bzip1", TRUE, FALSE },
	{ "compress",   "application/x-gzip", TRUE, TRUE },
	{ "cpio",       "application/x-cpio", TRUE, FALSE },
	{ "isoinfo",    "application/x-cd-image", TRUE, FALSE },
	{ "zip",        "application/x-ear", TRUE, TRUE },
	{ "zip",        "application/x-jar", TRUE, TRUE },
	{ "zip",        "application/x-war", TRUE, TRUE },
	{ "zip",        "application/x-ms-dos-executable", TRUE, FALSE },
	{ "lha",        "application/x-lha", TRUE, TRUE },
	{ "lzma",       "application/x-lzma", TRUE, TRUE },
	{ "lzop",       "application/x-lzop", TRUE, TRUE },
	{ "rpm2cpio",   "application/x-rpm", TRUE, FALSE },
	{ "uncompress", "application/x-gzip", TRUE, FALSE },
	{ "unstuff",    "application/x-stuffit", TRUE, FALSE },
	{ "zoo",        "application/x-zoo", TRUE, TRUE },
	{ "7za",        "application/x-7z-compressed", TRUE, TRUE },
	{ "7zr",        "application/x-7z-compressed", TRUE, TRUE }
};

FrCommandDescription tar_command_desc[] = {
	{ "gzip",      "application/x-compressed-tar", TRUE, TRUE },
	{ "bzip2",     "application/x-bzip-compressed-tar", TRUE, TRUE },
	/*{ "bzip",     "application/x-bzip1-compressed-tar", FALSE, TRUE },*/
	{ "lzma",      "application/x-lzma-compressed-tar", TRUE, TRUE },
	{ "lzop",      "application/x-lzop-compressed-tar", TRUE, TRUE },
	{ "compress",  "application/x-compressed-tar", TRUE, TRUE }
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
command_done (CommandData *cdata)
{
	if (cdata == NULL)
		return;
		
	if ((cdata->temp_dir != NULL) && path_is_dir (cdata->temp_dir)) {
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
	if (cdata->app != NULL)
		g_object_unref (cdata->app);
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

	eel_global_client_free ();

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

	from_path = get_home_relative_path (from_dir);
	to_path = get_home_relative_path (to_dir);

	if (uri_is_dir (from_path) && ! uri_exists (to_path)) {
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

	from_path = get_home_relative_path (from_file);
	to_path = get_home_relative_path (to_file);

	if (uri_is_file (from_path) && ! uri_exists (to_path)) {
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


/* -- FrRegisteredCommand -- */


FrRegisteredCommand *
fr_registered_command_new (GType command_type)
{
	FrRegisteredCommand *reg_com;
	
	reg_com = g_new0 (FrRegisteredCommand, 1);
	reg_com->ref = 1;
	reg_com->type = command_type;
	reg_com->caps = g_ptr_array_new ();
	
	return reg_com;
}


void
fr_registered_command_ref (FrRegisteredCommand *reg_com)
{
	reg_com->ref++;
}


void
fr_registered_command_unref (FrRegisteredCommand *reg_com)
{
	if (--(reg_com->ref) != 0)
		return;
	
	g_ptr_array_foreach (reg_com->caps, (GFunc) g_free, NULL);
	g_ptr_array_free (reg_com->caps, TRUE);
	g_free (reg_com); 
}


void
fr_registered_command_add_mime_type (FrRegisteredCommand *reg_com,
				     const char          *mime_type,
				     FrCommandCaps        capabilities)
{
	FrMimeTypeCap *cap;
	
	cap = g_new0 (FrMimeTypeCap, 1);
	cap->mime_type = mime_type;
	cap->capabilities = capabilities;
	
	g_ptr_array_add (reg_com->caps, cap);
}


FrCommandCaps  
fr_registered_command_get_capabilities (FrRegisteredCommand *reg_com,
				        const char          *mime_type)
{
	int i;
		
	for (i = 0; i < reg_com->caps->len; i++) {
		FrMimeTypeCap *cap;
		
		cap = g_ptr_array_index (reg_com->caps, i);
		if (strcmp (mime_type, cap->mime_type) == 0) 
			return cap->capabilities;
	}
	
	return FR_COMMAND_CAP_NONE;
}


void
register_command (GType command_type, ...)
{
	va_list              args;
	FrRegisteredCommand *command;
	const char          *mime_type;
	FrCommandCap         capabilities;
		
	if (Registered_Commands == NULL)
		Registered_Commands = g_ptr_array_sized_new (5);
	
	command = fr_registered_command_new (command_type);
	
	va_start (args, command_type);
	while ((mime_type = va_arg (args, const char *)) != NULL) {
		capabilities = va_arg (args, FrCommandCap);
		fr_registered_command_add_mime_type (command, mime_type, capabilities); 
	}
	va_end (args);
	
	g_ptr_array_add (Registered_Commands, command);
}


gboolean
unregister_command (GType command_type)
{
	int i;
	
	for (i = 0; i < Registered_Commands->len; i++) {
		FrRegisteredCommand *command;
		
		command = g_ptr_array_index (Registered_Commands, i);
		if (command->type == command_type) {
			g_ptr_array_remove_index (Registered_Commands, i);
			fr_registered_command_unref (command);
			return TRUE;
		}
	}
	
	return FALSE;
}


static void
register_commands (void)
{
	register_command (FR_TYPE_COMMAND_7Z, 
			  "application/x-7z-compressed", FR_COMMAND_CAP_ALL,
			  NULL);	
	register_command (FR_TYPE_COMMAND_ACE, 
			  "application/x-ace", FR_COMMAND_CAP_READ_WRITE | FR_COMMAND_CAP_ARCHIVE_MANY_FILES,
			  NULL);
	register_command (FR_TYPE_COMMAND_AR, 
			  "application/x-ar", FR_COMMAND_CAP_ALL,
			  "application/x-deb", FR_COMMAND_CAP_READ | FR_COMMAND_CAP_ARCHIVE_MANY_FILES,
			  NULL);
	register_command (FR_TYPE_COMMAND_ARJ, 
			  "application/x-arj", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_CFILE, 
			  "application/x-gzip", FR_COMMAND_CAP_READ_WRITE,
			  "application/x-bzip", FR_COMMAND_CAP_READ_WRITE,
			  "application/x-compress", FR_COMMAND_CAP_READ_WRITE,
			  "application/x-lzma", FR_COMMAND_CAP_READ_WRITE,
			  "application/x-lzop", FR_COMMAND_CAP_READ_WRITE,
			  NULL);
	register_command (FR_TYPE_COMMAND_CPIO, 
			  "application/x-cpio", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_ISO, 
			  "application/x-cd-image", FR_COMMAND_CAP_READ | FR_COMMAND_CAP_ARCHIVE_MANY_FILES,
			  NULL);
	register_command (FR_TYPE_COMMAND_JAR,
			  "application/x-jar", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_LHA,
			  "application/x-lha", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_RAR,
			  "application/x-rar", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_RPM,
			  "application/x-rpm", FR_COMMAND_CAP_READ | FR_COMMAND_CAP_ARCHIVE_MANY_FILES,
			  NULL);
	register_command (FR_TYPE_COMMAND_TAR, 
			  "application/x-tar", FR_COMMAND_CAP_ALL,
			  "application/x-compressed-tar", FR_COMMAND_CAP_ALL,
			  "application/x-bzip-compressed-tar", FR_COMMAND_CAP_ALL,
			  "application/x-lzma-compressed-tar", FR_COMMAND_CAP_ALL,
			  "application/x-lzop-compressed-tar", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_UNSTUFF,
			  "application/x-stuffit", FR_COMMAND_CAP_READ | FR_COMMAND_CAP_ARCHIVE_MANY_FILES,
			  NULL);
	register_command (FR_TYPE_COMMAND_ZIP,
			  "application/zip", FR_COMMAND_CAP_ALL,
			  "application/x-ear", FR_COMMAND_CAP_ALL,
			  "application/x-war", FR_COMMAND_CAP_ALL,
			  "application/x-ms-dos-executable", FR_COMMAND_CAP_ALL,
			  NULL);
	register_command (FR_TYPE_COMMAND_ZOO,
			  "application/x-zoo", FR_COMMAND_CAP_ALL,
			  NULL);
}


GType
get_command_type_from_mime_type (const char    *mime_type,
				 FrCommandCaps  requested_capabilities)
{
	int i;
	
	if (mime_type == NULL)
		return 0;
	
	for (i = 0; i < Registered_Commands->len; i++) {
		FrRegisteredCommand *command;
		FrCommandCaps        capabilities;
		
		command = g_ptr_array_index (Registered_Commands, i);
		capabilities = fr_registered_command_get_capabilities (command, mime_type);
			
		/* the command must support all the requested capabilities */
		if (((capabilities ^ requested_capabilities) & requested_capabilities) == 0)
			return command->type;
	}
	
	return 0;
}


const char *
get_mime_type_from_extension (const char *ext)
{
	int i;
	
	for (i = G_N_ELEMENTS (file_ext_type) - 1; i >= 0; i--) 
		if (strcmp (ext, file_ext_type[i].ext) == 0) 
			return get_static_string (file_ext_type[i].mime_type);
	return NULL;
}


const char *
get_archive_filename_extension (const char *filename)
{
	const char *ext;
	int         i;
		
	if (filename == NULL)
		return NULL;

	ext = get_file_extension (filename);
	if (ext == NULL)
		return NULL;
		
	for (i = G_N_ELEMENTS (file_ext_type) - 1; i >= 0; i--) 
		if (strcmp (ext, file_ext_type[i].ext) == 0)
			return ext;
	return NULL;
}


static int
get_mime_type_index (const char *mime_type)
{
	int i;
	
	for (i = 0; i < G_N_ELEMENTS (mime_type_desc); i++) 
		if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0)
			return i;
	return -1;
}


static void
compute_supported_archive_types (void)
{
	int i, j;
	int sf_i = 0, s_i = 0, o_i = 0;
	int idx;
	
	for (i = 0; i < G_N_ELEMENTS (command_desc); i++) {
		FrCommandDescription comm_desc = command_desc[i];

		if (! is_program_in_path (comm_desc.command))
			continue;

		if (strcmp (comm_desc.command, "tar") == 0) {
			for (j = 0; j < G_N_ELEMENTS (tar_command_desc); j++) {
				FrCommandDescription comm_desc_2 = tar_command_desc[j];

				if (!is_program_in_path (comm_desc_2.command))
					continue;
					
				idx = get_mime_type_index (comm_desc_2.mime_type);
				if (idx >= 0) {
					open_type[o_i++] = idx;
					save_type[s_i++] = idx;
					single_file_save_type[sf_i++] = idx;
				}
			}
		}
		
		idx = get_mime_type_index (comm_desc.mime_type);
		if (idx >= 0) {
			if (comm_desc.can_open)
				open_type[o_i++] = idx;
			if (comm_desc.can_save && mime_type_desc[idx].supports_many_files)
				save_type[s_i++] = idx;
			if (comm_desc.can_save)
				single_file_save_type[sf_i++] = idx;
		}
	}

	open_type[o_i++] = -1;
	save_type[s_i++] = -1;
	single_file_save_type[sf_i++] = -1;
}


static char *
get_uri_from_command_line (const char *path)
{
	char *full_path;
	char *uri;

	if (strstr (path, "://") != NULL)
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

	uri = g_filename_to_uri (full_path, NULL, NULL);
	g_free (full_path);

	return uri;
}


static void
prepare_app (void)
{
	char *uri;
	char *extract_to_path = NULL;
	char *add_to_path = NULL;

	/* create the config dir if necessary. */

	uri = get_home_relative_uri (RC_DIR);
	
	if (uri_is_file (uri)) { /* before the gconf port this was a file, now it's folder. */
		GFile *file;
		
		file = g_file_new_for_uri (uri);
		g_file_delete (file, NULL, NULL);
		g_object_unref (file);
	}

	ensure_dir_exists (uri, 0700, NULL);
	g_free (uri);

	if (eel_gconf_get_boolean (PREF_MIGRATE_DIRECTORIES, TRUE))
		migrate_to_new_directories ();

	register_commands ();
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
			char *archive_uri;

			archive_uri = get_uri_from_command_line (archive);
			if (extract_here == 1)
				fr_window_set_batch__extract_here (FR_WINDOW (window),
								   archive_uri);
			else
				fr_window_set_batch__extract (FR_WINDOW (window),
							      archive_uri,
							      extract_to_path);
			g_free (archive_uri); 
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

	g_free (add_to_path);
	g_free (extract_to_path);
}


/* SM support */


/* The master client we use for SM */
static GnomeClient *master_client = NULL;

/* argv[0] from main(); used as the command to restart the program */
static const char *program_argv0 = NULL;


static char *
get_real_path_for_prefix (const char *prefix)
{
	return g_strconcat (g_get_home_dir (), "/.gnome2/", prefix, NULL);
}


static void
save_session (GnomeClient *client)
{
	GKeyFile *key_file;
	GList    *scan;
	int       i;
	char     *path;
	GFile    *file;
	
	key_file = g_key_file_new ();
	i = 0;
	for (scan = WindowList; scan; scan = scan->next) {
		FrWindow *window = scan->data;
		char     *key;

		key = g_strdup_printf ("archive%d", i);
		if ((window->archive == NULL) || (window->archive->file == NULL)) {
			g_key_file_set_string (key_file, "Sessione", key, "");
		}
		else {
			char *uri;
			
			uri = g_file_get_uri (window->archive->file);
			g_key_file_set_string (key_file, "Sessione", key, uri);
			g_free (uri);
		}
		g_free (key);

		i++;
	}
	g_key_file_set_integer (key_file, "Sessione", "archives", i); 
	
	path = get_real_path_for_prefix (gnome_client_get_config_prefix (client));
	file = g_file_new_for_path (path);
	g_key_file_save (key_file, file);
	
	g_object_unref (file);
	g_free (path);
	g_key_file_free (key_file);
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
	argv[2] = get_real_path_for_prefix (prefix);
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
	char     *path;
	GKeyFile *key_file;
	int       i, n;

	path = get_real_path_for_prefix (gnome_client_get_config_prefix (master_client));
	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, path, 0, NULL);

	n = g_key_file_get_integer (key_file, "Session", "archives", NULL);
	for (i = 0; i < n; i++) {
		GtkWidget *window;
		char      *key;
		char      *filename;

		key = g_strdup_printf ("archive%d", i);
		filename = g_key_file_get_string (key_file, "Session", key, NULL);
		g_free (key);

		window = fr_window_new ();
		gtk_widget_show (window);
		if (strlen (filename) != 0)
			fr_window_archive_open (FR_WINDOW (window), filename, GTK_WINDOW (window));

		g_free (filename);
	}

	g_key_file_free (key_file);
	g_free (path);

	return TRUE;
}
