/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2010 Free Software Foundation, Inc.
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
#include <stdlib.h>
#include <glib/gi18n.h>
#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#if ENABLE_LIBARCHIVE
# include "fr-archive-libarchive.h"
#endif
#include "fr-command.h"
#include "fr-command-ace.h"
#include "fr-command-alz.h"
#include "fr-command-ar.h"
#include "fr-command-arj.h"
#include "fr-command-cfile.h"
#include "fr-command-cpio.h"
#include "fr-command-dpkg.h"
#include "fr-command-iso.h"
#include "fr-command-jar.h"
#include "fr-command-lha.h"
#include "fr-command-rar.h"
#include "fr-command-rpm.h"
#include "fr-command-tar.h"
#if HAVE_JSON_GLIB
  #include "fr-command-unarchiver.h"
#endif
#include "fr-command-unstuff.h"
#include "fr-command-zip.h"
#include "fr-command-zoo.h"
#include "fr-command-7z.h"
#include "fr-command-lrzip.h"
#include "fr-init.h"
#include "fr-process.h"
#include "fr-stock.h"
#include "fr-window.h"
#include "typedefs.h"
#include "preferences.h"


/* The capabilities are computed automatically in
 * compute_supported_archive_types() so it's correct to initialize to 0 here. */
FrMimeTypeDescription mime_type_desc[] = {
	{ "application/x-7z-compressed",        ".7z",       N_("7-Zip"), 0 },
	{ "application/x-7z-compressed-tar",    ".tar.7z",   N_("Tar compressed with 7z"), 0 },
	{ "application/x-ace",                  ".ace",      N_("Ace"), 0 },
	{ "application/x-alz",                  ".alz",      NULL, 0 },
	{ "application/x-ar",                   ".ar",       N_("Ar"), 0 },
	{ "application/x-arj",                  ".arj",      N_("Arj"), 0 },
	{ "application/x-bzip",                 ".bz2",      NULL, 0 },
	{ "application/x-bzip-compressed-tar",  ".tar.bz2",  N_("Tar compressed with bzip2"), 0 },
	{ "application/x-bzip1",                ".bz",       NULL, 0 },
	{ "application/x-bzip1-compressed-tar", ".tar.bz",   N_("Tar compressed with bzip"), 0 },
	{ "application/vnd.ms-cab-compressed",  ".cab",      N_("Cabinet"), 0 },
	{ "application/x-cbr",                  ".cbr",      N_("Rar Archived Comic Book"), 0 },
	{ "application/x-cbz",                  ".cbz",      N_("Zip Archived Comic Book"), 0 },
	{ "application/x-cd-image",             ".iso",      NULL, 0 },
	{ "application/x-compress",             ".Z",        NULL, 0 },
	{ "application/x-compressed-tar",       ".tar.gz",   N_("Tar compressed with gzip"), 0 },
	{ "application/x-cpio",                 ".cpio",     NULL, 0 },
	{ "application/x-deb",                  ".deb",      NULL, 0 },
	{ "application/x-ear",                  ".ear",      N_("Ear"), 0 },
	{ "application/x-ms-dos-executable",    ".exe",      N_("Self-extracting zip"), 0 },
	{ "application/x-gzip",                 ".gz",       NULL, 0 },
	{ "application/x-java-archive",         ".jar",      N_("Jar"), 0 },
	{ "application/x-lha",                  ".lzh",      N_("Lha"), 0 },
	{ "application/x-lrzip",                ".lrz",      N_("Lrzip"), 0},
	{ "application/x-lrzip-compressed-tar", ".tar.lrz",  N_("Tar compressed with lrzip"), 0 },
	{ "application/x-lzip",                 ".lz",       NULL, 0 },
	{ "application/x-lzip-compressed-tar",  ".tar.lz",   N_("Tar compressed with lzip"), 0 },
	{ "application/x-lzma",                 ".lzma",     NULL, 0 },
	{ "application/x-lzma-compressed-tar",  ".tar.lzma", N_("Tar compressed with lzma"), 0 },
	{ "application/x-lzop",                 ".lzo",      NULL, 0 },
	{ "application/x-lzop-compressed-tar",  ".tar.lzo",  N_("Tar compressed with lzop"), 0 },
	{ "application/x-ms-wim",               ".wim",      N_("Windows Imaging Format"), 0 },
	{ "application/x-rar",                  ".rar",      N_("Rar"), 0 },
	{ "application/x-rpm",                  ".rpm",      NULL, 0 },
	{ "application/x-rzip",                 ".rz",       NULL, 0 },
	{ "application/x-tar",                  ".tar",      N_("Tar uncompressed"), 0 },
	{ "application/x-tarz",                 ".tar.Z",    N_("Tar compressed with compress"), 0 },
	{ "application/x-stuffit",              ".sit",      NULL, 0 },
	{ "application/x-war",                  ".war",      N_("War"), 0 },
	{ "application/x-xz",                   ".xz",       N_("Xz"), 0 },
	{ "application/x-xz-compressed-tar",    ".tar.xz",   N_("Tar compressed with xz"), 0 },
	{ "application/x-zoo",                  ".zoo",      N_("Zoo"), 0 },
	{ "application/zip",                    ".zip",      N_("Zip"), 0 },
	{ NULL, NULL, NULL, 0 }
};

FrExtensionType file_ext_type[] = {
	{ ".7z", "application/x-7z-compressed" },
	{ ".ace", "application/x-ace" },
	{ ".alz", "application/x-alz" },
	{ ".ar", "application/x-ar" },
	{ ".arj", "application/x-arj" },
	{ ".bin", "application/x-stuffit" },
	{ ".bz", "application/x-bzip" },
	{ ".bz2", "application/x-bzip" },
	{ ".cab", "application/vnd.ms-cab-compressed" },
	{ ".cbr", "application/x-cbr" },
	{ ".cbz", "application/x-cbz" },
	{ ".cpio", "application/x-cpio" },
	{ ".deb", "application/x-deb" },
	{ ".ear", "application/x-ear" },
	{ ".exe", "application/x-ms-dos-executable" },
	{ ".gz", "application/x-gzip" },
	{ ".iso", "application/x-cd-image" },
	{ ".jar", "application/x-java-archive" },
	{ ".lha", "application/x-lha" },
	{ ".lrz", "application/x-lrzip" },
	{ ".lzh", "application/x-lha" },
	{ ".lz", "application/x-lzip" },
	{ ".lzma", "application/x-lzma" },
	{ ".lzo", "application/x-lzop" },
	{ ".rar", "application/x-rar" },
	{ ".rpm", "application/x-rpm" },
	{ ".rz", "application/x-rzip" },
	{ ".sit", "application/x-stuffit" },
	{ ".swm", "application/x-ms-wim" },
	{ ".tar", "application/x-tar" },
	{ ".tar.bz", "application/x-bzip-compressed-tar" },
	{ ".tar.bz2", "application/x-bzip-compressed-tar" },
	{ ".tar.gz", "application/x-compressed-tar" },
	{ ".tar.lrz", "application/x-lrzip-compressed-tar" },
	{ ".tar.lz", "application/x-lzip-compressed-tar" },
	{ ".tar.lzma", "application/x-lzma-compressed-tar" },
	{ ".tar.lzo", "application/x-lzop-compressed-tar" },
	{ ".tar.7z", "application/x-7z-compressed-tar" },
	{ ".tar.xz", "application/x-xz-compressed-tar" },
	{ ".tar.Z", "application/x-tarz" },
	{ ".taz", "application/x-tarz" },
	{ ".tbz", "application/x-bzip-compressed-tar" },
	{ ".tbz2", "application/x-bzip-compressed-tar" },
	{ ".tgz", "application/x-compressed-tar" },
	{ ".txz", "application/x-xz-compressed-tar" },
	{ ".tlz", "application/x-lzip-compressed-tar" },
	{ ".tzma", "application/x-lzma-compressed-tar" },
	{ ".tzo", "application/x-lzop-compressed-tar" },
	{ ".war", "application/x-war" },
	{ ".wim", "application/x-ms-wim" },
	{ ".xz", "application/x-xz" },
	{ ".z", "application/x-gzip" },
	{ ".Z", "application/x-compress" },
	{ ".zip", "application/zip" },
	{ ".zoo", "application/x-zoo" },
	{ NULL, NULL }
};


GList        *CommandList;
gint          ForceDirectoryCreation;
GHashTable   *ProgramsCache;
GPtrArray    *Registered_Archives;
int           single_file_save_type[64];
int           save_type[64];
int           open_type[64];
int           create_type[64];


static void
migrate_options_directory (void)
{
	char *old_directory_path;
	GFile *old_directory;
	GFile *new_directory;

	old_directory_path = get_home_relative_path (".gnome2/file-roller/options");
	old_directory = g_file_new_for_path (old_directory_path);
	new_directory = _g_file_new_user_config_subdir (ADD_FOLDER_OPTIONS_DIR, FALSE);
	if (g_file_query_exists (old_directory, NULL) && ! g_file_query_exists (new_directory, NULL)) {
		GFile *parent;

		parent = g_file_get_parent (new_directory);
		if (_g_file_make_directory_tree (parent, 0700, NULL))
			g_file_move (old_directory, new_directory, 0, NULL, NULL, NULL, NULL);

		g_object_unref (parent);
	}

	g_object_unref (new_directory);
	g_object_unref (old_directory);
	g_free (old_directory_path);
}


/* -- FrRegisteredArchive -- */


static FrRegisteredArchive *
fr_registered_archive_new (GType command_type)
{
	FrRegisteredArchive  *reg_com;
	FrArchive            *archive;
	const char          **mime_types;
	int                   i;

	reg_com = g_new0 (FrRegisteredArchive, 1);
	reg_com->ref = 1;
	reg_com->type = command_type;
	reg_com->caps = g_ptr_array_new ();
	reg_com->packages = g_ptr_array_new ();

	archive = (FrArchive*) g_object_new (reg_com->type, NULL);
	mime_types = fr_archive_get_mime_types (archive);
	for (i = 0; mime_types[i] != NULL; i++) {
		const char         *mime_type;
		FrMimeTypeCap      *cap;
		FrMimeTypePackages *packages;

		mime_type = _g_str_get_static (mime_types[i]);

		cap = g_new0 (FrMimeTypeCap, 1);
		cap->mime_type = mime_type;
		cap->current_capabilities = fr_archive_get_capabilities (archive, mime_type, TRUE);
		cap->potential_capabilities = fr_archive_get_capabilities (archive, mime_type, FALSE);
		g_ptr_array_add (reg_com->caps, cap);

		packages = g_new0 (FrMimeTypePackages, 1);
		packages->mime_type = mime_type;
		packages->packages = fr_archive_get_packages (archive, mime_type);
		g_ptr_array_add (reg_com->packages, packages);
	}

	g_object_unref (archive);

	return reg_com;
}


G_GNUC_UNUSED static void
fr_registered_command_ref (FrRegisteredArchive *reg_com)
{
	reg_com->ref++;
}


static void
fr_registered_archive_unref (FrRegisteredArchive *reg_com)
{
	if (--(reg_com->ref) != 0)
		return;

	g_ptr_array_foreach (reg_com->caps, (GFunc) g_free, NULL);
	g_ptr_array_free (reg_com->caps, TRUE);
	g_free (reg_com);
}


static FrArchiveCaps
fr_registered_archive_get_capabilities (FrRegisteredArchive *reg_com,
				        const char          *mime_type)
{
	int i;

	for (i = 0; i < reg_com->caps->len; i++) {
		FrMimeTypeCap *cap;

		cap = g_ptr_array_index (reg_com->caps, i);
		if (strcmp (mime_type, cap->mime_type) == 0)
			return cap->current_capabilities;
	}

	return FR_ARCHIVE_CAN_DO_NOTHING;
}


static FrArchiveCaps
fr_registered_archive_get_potential_capabilities (FrRegisteredArchive *reg_com,
						  const char          *mime_type)
{
	int i;

	if (mime_type == NULL)
		return FR_ARCHIVE_CAN_DO_NOTHING;

	for (i = 0; i < reg_com->caps->len; i++) {
		FrMimeTypeCap *cap;

		cap = g_ptr_array_index (reg_com->caps, i);
		if ((cap->mime_type != NULL) && (strcmp (mime_type, cap->mime_type) == 0))
			return cap->potential_capabilities;
	}

	return FR_ARCHIVE_CAN_DO_NOTHING;
}


static void
register_archive (GType command_type)
{
	if (Registered_Archives == NULL)
		Registered_Archives = g_ptr_array_sized_new (5);
	g_ptr_array_add (Registered_Archives, fr_registered_archive_new (command_type));
}


G_GNUC_UNUSED static gboolean
unregister_archive (GType command_type)
{
	int i;

	for (i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *archive;

		archive = g_ptr_array_index (Registered_Archives, i);
		if (archive->type == command_type) {
			g_ptr_array_remove_index (Registered_Archives, i);
			fr_registered_archive_unref (archive);
			return TRUE;
		}
	}

	return FALSE;
}


static void
register_archives (void)
{
	/* The order here is important. Commands registered earlier have higher
	 * priority.  However commands that can read and write a file format
	 * have higher priority over commands that can only read the same
	 * format, regardless of the registration order. */

#if ENABLE_LIBARCHIVE
	register_archive (FR_TYPE_ARCHIVE_LIBARCHIVE);
#endif

	register_archive (FR_TYPE_COMMAND_TAR);
	register_archive (FR_TYPE_COMMAND_CFILE);
	register_archive (FR_TYPE_COMMAND_7Z);
	register_archive (FR_TYPE_COMMAND_DPKG);

	register_archive (FR_TYPE_COMMAND_ACE);
	register_archive (FR_TYPE_COMMAND_ALZ);
	register_archive (FR_TYPE_COMMAND_AR);
	register_archive (FR_TYPE_COMMAND_ARJ);
	register_archive (FR_TYPE_COMMAND_CPIO);
	register_archive (FR_TYPE_COMMAND_ISO);
	register_archive (FR_TYPE_COMMAND_JAR);
	register_archive (FR_TYPE_COMMAND_LHA);
	register_archive (FR_TYPE_COMMAND_RAR);
	register_archive (FR_TYPE_COMMAND_RPM);
	register_archive (FR_TYPE_COMMAND_UNSTUFF);
	register_archive (FR_TYPE_COMMAND_ZIP);
	register_archive (FR_TYPE_COMMAND_LRZIP);
	register_archive (FR_TYPE_COMMAND_ZOO);
#if HAVE_JSON_GLIB
	register_archive (FR_TYPE_COMMAND_UNARCHIVER);
#endif
}


GType
get_archive_type_from_mime_type (const char    *mime_type,
				 FrArchiveCaps  requested_capabilities)
{
	int i;

	if (mime_type == NULL)
		return 0;

	for (i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *command;
		FrArchiveCaps        capabilities;

		command = g_ptr_array_index (Registered_Archives, i);
		capabilities = fr_registered_archive_get_capabilities (command, mime_type);

		/* the command must support all the requested capabilities */
		if (((capabilities ^ requested_capabilities) & requested_capabilities) == 0)
			return command->type;
	}

	return 0;
}


GType
get_preferred_archive_for_mime_type (const char    *mime_type,
				     FrArchiveCaps  requested_capabilities)
{
	int i;

	for (i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *archive;
		FrArchiveCaps        capabilities;

		archive = g_ptr_array_index (Registered_Archives, i);
		capabilities = fr_registered_archive_get_potential_capabilities (archive, mime_type);

		/* the archive must support all the requested capabilities */
		if (((capabilities ^ requested_capabilities) & requested_capabilities) == 0)
			return archive->type;
	}

	return 0;
}


void
update_registered_archives_capabilities (void)
{
	int i;

	g_hash_table_remove_all (ProgramsCache);

	for (i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *reg_com;
		FrArchive           *archive;
		int                  j;

		reg_com = g_ptr_array_index (Registered_Archives, i);
		archive = g_object_new (reg_com->type, NULL);
		for (j = 0; j < reg_com->caps->len; j++) {
			FrMimeTypeCap *cap = g_ptr_array_index (reg_com->caps, j);

			cap->current_capabilities = fr_archive_get_capabilities (archive, cap->mime_type, TRUE);
			cap->potential_capabilities = fr_archive_get_capabilities (archive, cap->mime_type, FALSE);
		}

		g_object_unref (archive);
	}
}


const char *
get_mime_type_from_extension (const char *ext)
{
	int i;

	if (ext == NULL)
		return NULL;

	for (i = G_N_ELEMENTS (file_ext_type) - 1; i >= 0; i--) {
		if (file_ext_type[i].ext == NULL)
			continue;
		if (strcasecmp (ext, file_ext_type[i].ext) == 0)
			return _g_str_get_static (file_ext_type[i].mime_type);
	}

	return NULL;
}


const char *
get_archive_filename_extension (const char *filename)
{
	const char *ext;
	int         i;

	if (filename == NULL)
		return NULL;

	ext = _g_filename_get_extension (filename);
	if (ext == NULL)
		return NULL;

	for (i = G_N_ELEMENTS (file_ext_type) - 1; i >= 0; i--) {
		if (file_ext_type[i].ext == NULL)
			continue;
		if (strcasecmp (ext, file_ext_type[i].ext) == 0)
			return ext;
	}

	return NULL;
}


int
get_mime_type_index (const char *mime_type)
{
	int i;

	for (i = 0; mime_type_desc[i].mime_type != NULL; i++)
		if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0)
			return i;
	return -1;
}


static void
add_if_non_present (int *a,
	            int *n,
	            int  o)
{
	int i;

	for (i = 0; i < *n; i++) {
		if (a[i] == o)
			return;
	}
	a[*n] = o;
	*n = *n + 1;
}


static int
cmp_mime_type_by_extension (const void *p1,
			    const void *p2)
{
	int i1 = * (int*) p1;
	int i2 = * (int*) p2;

	return strcmp (mime_type_desc[i1].default_ext, mime_type_desc[i2].default_ext);
}


static int
cmp_mime_type_by_description (const void *p1,
			      const void *p2)
{
	int i1 = * (int*) p1;
	int i2 = * (int*) p2;

	return g_utf8_collate (_(mime_type_desc[i1].name), _(mime_type_desc[i2].name));
}


static void
sort_mime_types (int *a,
                 int(*compar)(const void *, const void *))
{
	int n = 0;

	while (a[n] != -1)
		n++;
	qsort (a, n, sizeof (int), compar);
}


void
sort_mime_types_by_extension (int *a)
{
	sort_mime_types (a, cmp_mime_type_by_extension);
}


void
sort_mime_types_by_description (int *a)
{
	sort_mime_types (a, cmp_mime_type_by_description);
}


static void
compute_supported_archive_types (void)
{
	int sf_i = 0, s_i = 0, o_i = 0, c_i = 0;
	int i;

	for (i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *reg_com;
		int                  j;

		reg_com = g_ptr_array_index (Registered_Archives, i);
		for (j = 0; j < reg_com->caps->len; j++) {
			FrMimeTypeCap *cap;
			int            idx;

			cap = g_ptr_array_index (reg_com->caps, j);
			idx = get_mime_type_index (cap->mime_type);
			if (idx < 0) {
				g_warning ("mime type not recognized: %s", cap->mime_type);
				continue;
			}
			mime_type_desc[idx].capabilities |= cap->current_capabilities;
			if (cap->current_capabilities & FR_ARCHIVE_CAN_READ)
				add_if_non_present (open_type, &o_i, idx);
			if (cap->current_capabilities & FR_ARCHIVE_CAN_WRITE) {
				if (cap->current_capabilities & FR_ARCHIVE_CAN_STORE_MANY_FILES) {
					add_if_non_present (save_type, &s_i, idx);
					if (cap->current_capabilities & FR_ARCHIVE_CAN_WRITE)
						add_if_non_present (create_type, &c_i, idx);
				}
				add_if_non_present (single_file_save_type, &sf_i, idx);
			}
		}
	}

	open_type[o_i] = -1;
	save_type[s_i] = -1;
	single_file_save_type[sf_i] = -1;
	create_type[c_i] = -1;
}


static gboolean initialized = FALSE;


void
initialize_data (void)
{
	if (initialized)
		return;
	initialized = TRUE;

	ProgramsCache = g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       g_free,
					       NULL);

	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PKG_DATA_DIR G_DIR_SEPARATOR_S "icons");

	migrate_options_directory ();
	register_archives ();
	compute_supported_archive_types ();
	fr_stock_init ();
}


static void
command_done (CommandData *cdata)
{
	if (cdata == NULL)
		return;

	if ((cdata->temp_dir != NULL) && _g_path_query_is_dir (cdata->temp_dir)) {
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
	_g_string_list_free (cdata->file_list);
	g_free (cdata->temp_dir);
	if (cdata->process != NULL)
		g_object_unref (cdata->process);

	CommandList = g_list_remove (CommandList, cdata);
	g_free (cdata);
}


void
release_data (void)
{
	if (! initialized)
		return;

	while (CommandList != NULL) {
		CommandData *cdata = CommandList->data;
		command_done (cdata);
	}
}
