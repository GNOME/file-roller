/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2006 The Free Software Foundation, Inc.
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
#include <glib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "file-utils.h"
#include "fr-command.h"
#include "fr-command-zip.h"
#include "fr-command-apk.h"
#include "glib-utils.h"
#include "java-utils.h"


G_DEFINE_TYPE (FrCommandApk, fr_command_apk, FR_TYPE_COMMAND_ZIP)


typedef struct {
	char *filename;
	char *rel_path;
	char *package_minus_one_level;
	char *link_name;		/* package dir = package_minus_one_level + '/' + link_name */
} ApkData;


static void
fr_command_apk_add (FrCommand  *comm,
		    const char *from_file,
		    GList      *file_list,
		    const char *base_dir,
		    gboolean    update,
		    gboolean    follow_links)
{
	FrProcess *proc = comm->process;
	GList     *zip_list = NULL, *apkdata_list = NULL, *apk_list = NULL;
	GList     *scan;
	char      *tmp_dir;

	for (scan = file_list; scan; scan = scan->next) {
		char *filename = scan->data;
		char *path = g_build_filename (base_dir, filename, NULL);
		char *package = NULL;

		if (_g_filename_has_extension (filename, ".java"))
			package = get_package_name_from_java_file (path);
		else if (_g_filename_has_extension (filename, ".class"))
			package = get_package_name_from_class_file (path);

		if ((package == NULL) || (strlen (package) == 0))
			zip_list = g_list_append (zip_list, g_strdup (filename));
		else {
			ApkData *newdata = g_new0 (ApkData, 1);

			newdata->package_minus_one_level = _g_path_remove_level (package);
			newdata->link_name = g_strdup (_g_path_get_basename (package));
			newdata->rel_path = _g_path_remove_level (filename);
			newdata->filename = g_strdup (_g_path_get_basename (filename));
			apkdata_list = g_list_append (apkdata_list, newdata);
		}

		g_free (package);
		g_free (path);
	}

	tmp_dir = _g_path_get_temp_work_dir (NULL);
	for (scan = apkdata_list; scan ; scan = scan->next) {
		ApkData *jdata = scan->data;
		char    *pack_path;
		GFile   *directory;
		char    *old_link;
		char    *link_name;
		int      retval;

		pack_path = g_build_filename (tmp_dir, jdata->package_minus_one_level, NULL);
		directory = g_file_new_for_path (pack_path);
		if (! _g_file_make_directory_tree (directory, 0755, NULL)) {
			g_object_unref (directory);
			g_free (pack_path);
			continue;
		}

		old_link = g_build_filename (base_dir, jdata->rel_path, NULL);
		link_name = g_build_filename (pack_path, jdata->link_name, NULL);

		retval = symlink (old_link, link_name);
		if ((retval != -1) || (errno == EEXIST))
			apk_list = g_list_append (apk_list,
						  g_build_filename (jdata->package_minus_one_level,
							            jdata->link_name,
						      	            jdata->filename,
						      	            NULL));

		g_free (link_name);
		g_free (old_link);
		g_object_unref (directory);
		g_free (pack_path);
	}

	if (zip_list != NULL)
		FR_COMMAND_CLASS (fr_command_apk_parent_class)->add (comm, NULL, zip_list, base_dir, update, follow_links);

	if (apk_list != NULL)
		FR_COMMAND_CLASS (fr_command_apk_parent_class)->add (comm, NULL, apk_list, tmp_dir, update, follow_links);

	fr_process_begin_command (proc, "rm");
	fr_process_set_working_dir (proc, "/");
	fr_process_add_arg (proc, "-r");
	fr_process_add_arg (proc, "-f");
	fr_process_add_arg (proc, tmp_dir);
	fr_process_end_command (proc);
	fr_process_set_sticky (proc, TRUE);

	for (scan = apkdata_list; scan ; scan = scan->next) {
		ApkData *jdata = scan->data;
		g_free (jdata->filename);
		g_free (jdata->package_minus_one_level);
		g_free (jdata->link_name);
		g_free (jdata->rel_path);
	}

	_g_string_list_free (apkdata_list);
	_g_string_list_free (apk_list);
	_g_string_list_free (zip_list);
	g_free (tmp_dir);
}


const char *apk_mime_type[] = { "application/vnd.android.package-archive",
				NULL };


static const char **
fr_command_apk_get_mime_types (FrArchive *archive)
{
	return apk_mime_type;
}


static FrArchiveCap
fr_command_apk_get_capabilities (FrArchive  *archive,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrArchiveCap capabilities;

	capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
	if (_g_program_is_available ("zip", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE;

	return capabilities;
}


static const char *
fr_command_apk_get_packages (FrArchive  *archive,
			     const char *mime_type)
{
	return PACKAGES ("zip,unzip");
}


static void
fr_command_apk_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_APK (object));

        if (G_OBJECT_CLASS (fr_command_apk_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_apk_parent_class)->finalize (object);
}


static void
fr_command_apk_class_init (FrCommandApkClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_apk_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = fr_command_apk_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_apk_get_mime_types;
	archive_class->get_capabilities = fr_command_apk_get_capabilities;
	archive_class->get_packages     = fr_command_apk_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->add              = fr_command_apk_add;
}


static void
fr_command_apk_init (FrCommandApk *self)
{
	/* void */
}
