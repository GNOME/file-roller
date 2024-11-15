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
#include "fr-file-data.h"
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
#include "fr-command-lrzip.h"
#include "fr-command-rar.h"
#include "fr-command-rpm.h"
#include "fr-command-tar.h"
#if HAVE_JSON_GLIB
  #include "fr-command-unarchiver.h"
#endif
#include "fr-command-unsquashfs.h"
#include "fr-command-unstuff.h"
#include "fr-command-zip.h"
#include "fr-command-zoo.h"
#include "fr-command-7z.h"
#include "fr-init.h"
#include "fr-process.h"
#include "fr-window.h"
#include "typedefs.h"
#include "preferences.h"


/* The capabilities are computed automatically in
 * compute_supported_archive_types() so it's correct to initialize to 0 here. */
FrMimeTypeDescription mime_type_desc[] = {
	{ "application/epub+zip",                  ".epub",     0 },
	{ "application/x-7z-compressed",           ".7z",       0 },
	{ "application/x-7z-compressed-tar",       ".tar.7z",   0 },
	{ "application/x-ace",                     ".ace",      0 },
	{ "application/x-alz",                     ".alz",      0 },
	{ "application/vnd.android.package-archive", ".apk", 0 },
	{ "application/x-ar",                      ".ar",       0 },
	{ "application/x-arj",                     ".arj",      0 },
	{ "application/x-brotli",                  ".br",       0 },
	{ "application/x-brotli-compressed-tar",   ".tar.br",   0 },
	{ "application/x-bzip3",                   ".bz3",      0 },
	{ "application/x-bzip3-compressed-tar",    ".tar.bz3",  0 },
	{ "application/x-bzip",                    ".bz2",      0 },
	{ "application/x-bzip-compressed-tar",     ".tar.bz2",  0 },
	{ "application/x-bzip1",                   ".bz",       0 },
	{ "application/x-bzip1-compressed-tar",    ".tar.bz",   0 },
	{ "application/vnd.ms-cab-compressed",     ".cab",      0 },
	{ "application/x-cbr",                     ".cbr",      0 },
	{ "application/x-cbz",                     ".cbz",      0 },
	{ "application/x-cd-image",                ".iso",      0 },
	{ "application/x-chrome-extension",        ".crx",      0 },
	{ "application/x-compress",                ".Z",        0 },
	{ "application/x-compressed-tar",          ".tar.gz",   0 },
	{ "application/x-cpio",                    ".cpio",     0 },
	{ "application/x-deb",                     ".deb",      0 },
	{ "application/x-debian-package",          ".deb",      0 },
	{ "application/vnd.debian.binary-package", ".deb",      0 },
	{ "application/x-apple-diskimage",         ".dmg",      0 },
	{ "application/vnd.rar",                   ".rar",      0 },
	{ "application/vnd.snap",                  ".snap",     0 },
	{ "application/vnd.squashfs",              ".sqsh",     0 },
	{ "application/x-ear",                     ".ear",      0 },
	{ "application/x-ms-dos-executable",       ".exe",      0 },
	{ "application/x-gzip",                    ".gz",       0 },
	{ "application/x-java-archive",            ".jar",      0 },
	{ "application/x-lha",                     ".lzh",      0 },
	{ "application/x-lrzip",                   ".lrz",      0 },
	{ "application/x-lrzip-compressed-tar",    ".tar.lrz",  0 },
	{ "application/x-lz4",                     ".lz4",      0 },
	{ "application/x-lz4-compressed-tar",      ".tar.lz4",  0 },
	{ "application/x-lzip",                    ".lz",       0 },
	{ "application/x-lzip-compressed-tar",     ".tar.lz",   0 },
	{ "application/x-lzma",                    ".lzma",     0 },
	{ "application/x-lzma-compressed-tar",     ".tar.lzma", 0 },
	{ "application/x-lzop",                    ".lzo",      0 },
	{ "application/x-ms-wim",                  ".wim",      0 },
	{ "application/x-rar",                     ".rar",      0 },
	{ "application/x-rpm",                     ".rpm",      0 },
	{ "application/x-rzip",                    ".rz",       0 },
	{ "application/x-rzip-compressed-tar",     ".tar.rz",   0 },
	{ "application/x-tar",                     ".tar",      0 },
	{ "application/x-tarz",                    ".tar.Z",    0 },
	{ "application/x-tzo",                     ".tar.lzo",  0 },
	{ "application/x-stuffit",                 ".sit",      0 },
	{ "application/x-war",                     ".war",      0 },
	{ "application/x-xar",                     ".xar",      0 },
	{ "application/x-xz",                      ".xz",       0 },
	{ "application/x-xz-compressed-tar",       ".tar.xz",   0 },
	{ "application/x-zoo",                     ".zoo",      0 },
	{ "application/x-zstd-compressed-tar",     ".tar.zst",  0 },
	{ "application/zip",                       ".zip",      0 },
	{ "application/zstd",                      ".zst",      0 },
	{ NULL, NULL, 0 }
};

FrExtensionType file_ext_type[] = {
	{ ".7z", "application/x-7z-compressed" },
	{ ".ace", "application/x-ace" },
	{ ".alz", "application/x-alz" },
	{ ".apk", "application/vnd.android.package-archive" },
	{ ".ar", "application/x-ar" },
	{ ".arj", "application/x-arj" },
	{ ".bin", "application/x-stuffit" },
	{ ".br", "application/x-brotli" },
	{ ".bz", "application/x-bzip" },
	{ ".bz2", "application/x-bzip" },
	{ ".bz3", "application/x-bzip3" },
	{ ".cab", "application/vnd.ms-cab-compressed" },
	{ ".cbr", "application/x-cbr" },
	{ ".cbz", "application/x-cbz" },
	{ ".click", "application/x-deb" },
	{ ".cpio", "application/x-cpio" },
	{ ".crx", "application/x-chrome-extension" },
	{ ".deb", "application/x-deb" },
	{ ".dmg", "application/x-apple-diskimage" },
	{ ".ear", "application/x-ear" },
	{ ".epub", "application/epub+zip" },
	{ ".exe", "application/x-ms-dos-executable" },
	{ ".gz", "application/x-gzip" },
	{ ".iso", "application/x-cd-image" },
	{ ".jar", "application/x-java-archive" },
	{ ".lha", "application/x-lha" },
	{ ".lrz", "application/x-lrzip" },
	{ ".lzh", "application/x-lha" },
	{ ".lz", "application/x-lzip" },
	{ ".lz4", "application/x-lz4" },
	{ ".lzma", "application/x-lzma" },
	{ ".lzo", "application/x-lzop" },
	{ ".rar", "application/vnd.rar" },
	{ ".rpm", "application/x-rpm" },
	{ ".rz", "application/x-rzip" },
	{ ".sit", "application/x-stuffit" },
	{ ".snap", "application/vnd.snap" },
	{ ".sqsh", "application/vnd.squashfs" },
	{ ".swm", "application/x-ms-wim" },
	{ ".tar", "application/x-tar" },
	{ ".tar.br", "application/x-brotli-compressed-tar" },
	{ ".tar.bz", "application/x-bzip-compressed-tar" },
	{ ".tar.bz2", "application/x-bzip-compressed-tar" },
	{ ".tar.bz3", "application/x-bzip3-compressed-tar" },
	{ ".tar.gz", "application/x-compressed-tar" },
	{ ".tar.lrz", "application/x-lrzip-compressed-tar" },
	{ ".tar.lz", "application/x-lzip-compressed-tar" },
	{ ".tar.lz4", "application/x-lz4-compressed-tar" },
	{ ".tar.lzma", "application/x-lzma-compressed-tar" },
	{ ".tar.lzo", "application/x-tzo" },
	{ ".tar.7z", "application/x-7z-compressed-tar" },
	{ ".tar.rz", "application/x-rzip-compressed-tar" },
	{ ".tar.xz", "application/x-xz-compressed-tar" },
	{ ".tar.Z", "application/x-tarz" },
	{ ".tar.zst", "application/x-zstd-compressed-tar" },
	{ ".taz", "application/x-tarz" },
	{ ".tbz", "application/x-bzip-compressed-tar" },
	{ ".tbz2", "application/x-bzip-compressed-tar" },
	{ ".tbz3", "application/x-bzip3-compressed-tar" },
	{ ".tgz", "application/x-compressed-tar" },
	{ ".txz", "application/x-xz-compressed-tar" },
	{ ".tlz", "application/x-lzip-compressed-tar" },
	{ ".tlz4", "application/x-lz4-compressed-tar" },
	{ ".tzma", "application/x-lzma-compressed-tar" },
	{ ".tzo", "application/x-tzo" },
	{ ".tzst", "application/x-zstd-compressed-tar" },
	{ ".war", "application/x-war" },
	{ ".wim", "application/x-ms-wim" },
	{ ".xar", "application/x-xar" },
	{ ".xz", "application/x-xz" },
	{ ".z", "application/x-gzip" },
	{ ".Z", "application/x-compress" },
	{ ".zip", "application/zip" },
	{ ".zoo", "application/x-zoo" },
	{ ".zst", "application/zstd" },
	{ NULL, NULL }
};


GList        *CommandList;
gint          ForceDirectoryCreation;
GHashTable   *ProgramsCache;
GPtrArray    *Registered_Archives;
int           single_file_save_type[G_N_ELEMENTS (mime_type_desc)];
int           save_type[G_N_ELEMENTS (mime_type_desc)];
int           open_type[G_N_ELEMENTS (mime_type_desc)];
int           create_type[G_N_ELEMENTS (mime_type_desc)];


/* -- FrRegisteredArchive -- */


static FrRegisteredArchive *
fr_registered_archive_new (GType command_type)
{
	FrRegisteredArchive  *reg_com;
	FrArchive            *archive;
	const char          **mime_types;

	reg_com = g_new0 (FrRegisteredArchive, 1);
	reg_com->ref = 1;
	reg_com->type = command_type;
	reg_com->caps = g_ptr_array_new_with_free_func (g_free);
	reg_com->packages = g_ptr_array_new_with_free_func (g_free);

	archive = (FrArchive*) g_object_new (reg_com->type, NULL);
	mime_types = fr_archive_get_supported_types (archive);
	for (size_t i = 0; mime_types[i] != NULL; i++) {
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

	g_ptr_array_free (reg_com->caps, TRUE);
	g_free (reg_com);
}


static FrArchiveCaps
fr_registered_archive_get_capabilities (FrRegisteredArchive *reg_com,
				        const char          *mime_type)
{
	for (guint i = 0; i < reg_com->caps->len; i++) {
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
	if (mime_type == NULL)
		return FR_ARCHIVE_CAN_DO_NOTHING;

	for (guint i = 0; i < reg_com->caps->len; i++) {
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
	for (guint i = 0; i < Registered_Archives->len; i++) {
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
	register_archive (fr_archive_libarchive_get_type ());
#endif

	register_archive (fr_command_tar_get_type ());
	register_archive (fr_command_cfile_get_type ());
	register_archive (fr_command_7z_get_type ());
	register_archive (fr_command_dpkg_get_type ());

	register_archive (fr_command_ace_get_type ());
	register_archive (fr_command_alz_get_type ());
	register_archive (fr_command_ar_get_type ());
	register_archive (fr_command_arj_get_type ());
	register_archive (fr_command_cpio_get_type ());
	register_archive (fr_command_iso_get_type ());
	register_archive (fr_command_jar_get_type ());
	register_archive (fr_command_lha_get_type ());
	register_archive (fr_command_rar_get_type ());
	register_archive (fr_command_rpm_get_type ());
	register_archive (fr_command_unsquashfs_get_type ());
	register_archive (fr_command_unstuff_get_type ());
	register_archive (fr_command_zip_get_type ());
	register_archive (fr_command_lrzip_get_type ());
	register_archive (fr_command_zoo_get_type ());
#if HAVE_JSON_GLIB
	register_archive (fr_command_unarchiver_get_type ());
#endif
}


GType
fr_get_archive_type_from_mime_type (const char    *mime_type,
				 FrArchiveCaps  requested_capabilities)
{
	if (mime_type == NULL)
		return 0;

	for (guint i = 0; i < Registered_Archives->len; i++) {
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
fr_get_preferred_archive_for_mime_type (const char    *mime_type,
				     FrArchiveCaps  requested_capabilities)
{
	for (guint i = 0; i < Registered_Archives->len; i++) {
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
fr_update_registered_archives_capabilities (void)
{
	g_hash_table_remove_all (ProgramsCache);

	for (guint i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *reg_com;
		FrArchive           *archive;

		reg_com = g_ptr_array_index (Registered_Archives, i);
		archive = g_object_new (reg_com->type, NULL);
		for (guint j = 0; j < reg_com->caps->len; j++) {
			FrMimeTypeCap *cap = g_ptr_array_index (reg_com->caps, j);

			cap->current_capabilities = fr_archive_get_capabilities (archive, cap->mime_type, TRUE);
			cap->potential_capabilities = fr_archive_get_capabilities (archive, cap->mime_type, FALSE);
		}

		g_object_unref (archive);
	}
}


const char *
_g_mime_type_get_from_extension (const char *ext)
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
_g_mime_type_get_from_filename (GFile *file)
{
	const char *mime_type = NULL;
	char       *uri;

	if (file == NULL)
		return NULL;

	uri = g_file_get_uri (file);
	mime_type = _g_mime_type_get_from_extension (_g_filename_get_extension (uri));

	g_free (uri);

	return mime_type;
}


const char *
fr_get_archive_filename_extension (const char *filename)
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
fr_get_mime_type_index (const char *mime_type)
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
fr_sort_mime_types_by_extension (int *a)
{
	sort_mime_types (a, cmp_mime_type_by_extension);
}


static void
compute_supported_archive_types (void)
{
	int sf_i = 0, s_i = 0, o_i = 0, c_i = 0;

	for (guint i = 0; i < Registered_Archives->len; i++) {
		FrRegisteredArchive *reg_com;

		reg_com = g_ptr_array_index (Registered_Archives, i);
		for (guint j = 0; j < reg_com->caps->len; j++) {
			FrMimeTypeCap *cap;
			int            idx;

			cap = g_ptr_array_index (reg_com->caps, j);
			idx = fr_get_mime_type_index (cap->mime_type);
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
fr_initialize_data (void)
{
	if (initialized)
		return;
	initialized = TRUE;
	ProgramsCache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	register_archives ();
	compute_supported_archive_types ();
}


static void
command_done (FrCommandData *cdata)
{
	if (cdata == NULL)
		return;

	_g_file_remove_directory (cdata->temp_dir, NULL, NULL);

	g_free (cdata->command);
	_g_object_unref (cdata->app);
	_g_object_list_unref (cdata->file_list);
	_g_object_unref (cdata->temp_dir);
	_g_object_unref (cdata->process);

	CommandList = g_list_remove (CommandList, cdata);
	g_free (cdata);
}


void
fr_release_data (void)
{
	if (! initialized)
		return;

	while (CommandList != NULL) {
		FrCommandData *cdata = CommandList->data;
		command_done (cdata);
	}
}
