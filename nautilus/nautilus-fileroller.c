/*
 *  File-Roller
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Paolo Bacchilega <paobac@cvs.gnome.org>
 *
 */

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libnautilus-extension/nautilus-extension-types.h>
#include <libnautilus-extension/nautilus-file-info.h>
#include <libnautilus-extension/nautilus-menu-provider.h>
#include "nautilus-fileroller.h"


static GObjectClass *parent_class;


static void
extract_to_callback (NautilusMenuItem *item,
		     gpointer          user_data)
{
	GList            *files;
	NautilusFileInfo *file;
	char             *uri, *default_dir;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	uri = nautilus_file_info_get_uri (file);
	default_dir = nautilus_file_info_get_parent_uri (file);

	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd,
				" --default-dir=%s --extract %s",
				g_shell_quote (default_dir),
				g_shell_quote (uri));

#ifdef DEBUG
	g_print ("EXEC: %s\n", cmd->str);
#endif

	g_spawn_command_line_async (cmd->str, NULL);

	g_string_free (cmd, TRUE);
	g_free (default_dir);
	g_free (uri);
}


static void
extract_here_callback (NautilusMenuItem *item,
		       gpointer          user_data)
{
	GList            *files, *scan;
	NautilusFileInfo *file;
	char             *dir;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	dir = nautilus_file_info_get_parent_uri (file);

	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd," --extract-here");

	g_free (dir);

	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;
		char             *uri;

		uri = nautilus_file_info_get_uri (file);
		g_string_append_printf (cmd, " %s", g_shell_quote (uri));
		g_free (uri);
	}

	g_spawn_command_line_async (cmd->str, NULL);

#ifdef DEBUG
	g_print ("EXEC: %s\n", cmd->str);
#endif

	g_string_free (cmd, TRUE);
}


static void
add_callback (NautilusMenuItem *item,
	      gpointer          user_data)
{
	GList            *files, *scan;
	NautilusFileInfo *file;
	char             *uri, *dir;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	uri = nautilus_file_info_get_uri (file);
	dir = g_path_get_dirname (uri);

	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd," --default-dir=%s --add", g_shell_quote (dir));

	g_free (dir);
	g_free (uri);

	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;

		uri = nautilus_file_info_get_uri (file);
		g_string_append_printf (cmd, " %s", g_shell_quote (uri));
		g_free (uri);
	}

	g_spawn_command_line_async (cmd->str, NULL);

	g_string_free (cmd, TRUE);
}


static char *mime_types[] = {
	"application/x-7z-compressed",
 	"application/x-7z-compressed-tar",
 	"application/x-ace",
 	"application/x-alz",
	"application/x-ar",
	"application/x-arj",
	"application/x-bzip",
	"application/x-bzip-compressed-tar",
	"application/x-bzip1",
	"application/x-bzip1-compressed-tar",
	"application/vnd.ms-cab-compressed",
	"application/x-cbr",
	"application/x-cbz",
	"application/x-cd-image",
	"application/x-compress",
	"application/x-compressed-tar",
	"application/x-cpio",
	"application/x-deb",
	"application/x-ear",
	"application/x-ms-dos-executable",
	"application/x-gtar",
	"application/x-gzip",
	"application/x-gzpostscript",
	"application/x-java-archive",
	"application/x-lha",
	"application/x-lhz",
	"application/x-lzip",
	"application/x-lzip-compressed-tar",
	"application/x-lzma",
	"application/x-lzma-compressed-tar",
	"application/x-lzop",
	"application/x-lzop-compressed-tar",
	"application/x-rar",
	"application/x-rar-compressed",
	"application/x-rpm",
	"application/x-rzip",
	"application/x-tar",
	"application/x-tarz",
	"application/x-stuffit",
	"application/x-war",
	"application/x-xz",
	"application/x-xz-compressed-tar",
	"application/x-zip",
	"application/x-zip-compressed",
	"application/x-zoo",
	"application/zip",
	"multipart/x-zip",
	NULL
};


typedef struct
{
      gboolean is_archive;
      gboolean is_derived_archive;
} FileMimeInfo;


static FileMimeInfo
get_file_mime_info (NautilusFileInfo *file)
{
	FileMimeInfo file_mime_info;
	int          i;

	file_mime_info.is_archive = FALSE;
	file_mime_info.is_derived_archive = FALSE;

	for (i = 0; mime_types[i] != NULL; i++)
		if (nautilus_file_info_is_mime_type (file, mime_types[i])) {
			char *mime_type;
			char *content_type_mime_file;
			char *content_type_mime_compare;

			mime_type = nautilus_file_info_get_mime_type (file);

			content_type_mime_file = g_content_type_from_mime_type (mime_type);
			content_type_mime_compare = g_content_type_from_mime_type (mime_types[i]);

			file_mime_info.is_archive = TRUE;
			if ((content_type_mime_file != NULL) && (content_type_mime_compare != NULL))
				file_mime_info.is_derived_archive = ! g_content_type_equals (content_type_mime_file, content_type_mime_compare);

			g_free (mime_type);
			g_free (content_type_mime_file);
			g_free (content_type_mime_compare);

			return file_mime_info;
		}

	return file_mime_info;
}


static gboolean
unsupported_scheme (NautilusFileInfo *file)
{
	gboolean  result = FALSE;
	GFile    *location;
	char     *scheme;

	location = nautilus_file_info_get_location (file);
	scheme = g_file_get_uri_scheme (location);

	if (scheme != NULL) {
		const char *unsupported[] = { "trash", "computer", NULL };
		int         i;

		for (i = 0; unsupported[i] != NULL; i++)
			if (strcmp (scheme, unsupported[i]) == 0)
				result = TRUE;
	}

	g_free (scheme);
	g_object_unref (location);

	return result;
}


static GList *
nautilus_fr_get_file_items (NautilusMenuProvider *provider,
			    GtkWidget            *window,
			    GList                *files)
{
	GList    *items = NULL;
	GList    *scan;
	gboolean  can_write = TRUE;
	gboolean  one_item;
	gboolean  one_archive = FALSE;
	gboolean  one_derived_archive = FALSE;
	gboolean  all_archives = TRUE;
	gboolean  all_archives_derived = TRUE;

	if (files == NULL)
		return NULL;

	if (unsupported_scheme ((NautilusFileInfo *) files->data))
			return NULL;

	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;
		FileMimeInfo      file_mime_info;

		file_mime_info = get_file_mime_info (file);

		if (all_archives && ! file_mime_info.is_archive)
			all_archives = FALSE;

		if (all_archives_derived && file_mime_info.is_archive && ! file_mime_info.is_derived_archive)
			all_archives_derived = FALSE;

		if (can_write) {
			NautilusFileInfo *parent;

			parent = nautilus_file_info_get_parent_info (file);
 			can_write = nautilus_file_info_can_write (parent);
		}
	}

	/**/

	one_item = (files != NULL) && (files->next == NULL);
	one_archive = one_item && all_archives;
	one_derived_archive = one_archive && all_archives_derived;

	if (all_archives && can_write) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract_here",
					       _("Extract Here"),
					       _("Extract the selected archive in the current position"),
					       "drive-harddisk");
		g_signal_connect (item,
				  "activate",
				  G_CALLBACK (extract_here_callback),
				  provider);
		g_object_set_data_full (G_OBJECT (item),
					"files",
					nautilus_file_info_list_copy (files),
					(GDestroyNotify) nautilus_file_info_list_free);

		items = g_list_append (items, item);
	}
	else if (all_archives && ! can_write) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract_to",
					       _("Extract To..."),
					       _("Extract the selected archive"),
					       "drive-harddisk");
		g_signal_connect (item,
				  "activate",
				  G_CALLBACK (extract_to_callback),
				  provider);
		g_object_set_data_full (G_OBJECT (item),
					"files",
					nautilus_file_info_list_copy (files),
					(GDestroyNotify) nautilus_file_info_list_free);

		items = g_list_append (items, item);

	}

	if (! one_archive || one_derived_archive) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::add",
					       _("Compress..."),
					       _("Create a compressed archive with the selected objects"),
					       "gnome-mime-application-x-archive");
		g_signal_connect (item,
				  "activate",
				  G_CALLBACK (add_callback),
				  provider);
		g_object_set_data_full (G_OBJECT (item),
					"files",
					nautilus_file_info_list_copy (files),
					(GDestroyNotify) nautilus_file_info_list_free);

		items = g_list_append (items, item);
	}

	return items;
}


static void
nautilus_fr_menu_provider_iface_init (NautilusMenuProviderIface *iface)
{
	iface->get_file_items = nautilus_fr_get_file_items;
}


static void
nautilus_fr_instance_init (NautilusFr *fr)
{
}


static void
nautilus_fr_class_init (NautilusFrClass *class)
{
	parent_class = g_type_class_peek_parent (class);
}


static GType fr_type = 0;


GType
nautilus_fr_get_type (void)
{
	return fr_type;
}


void
nautilus_fr_register_type (GTypeModule *module)
{
	static const GTypeInfo info = {
		sizeof (NautilusFrClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) nautilus_fr_class_init,
		NULL,
		NULL,
		sizeof (NautilusFr),
		0,
		(GInstanceInitFunc) nautilus_fr_instance_init,
	};

	static const GInterfaceInfo menu_provider_iface_info = {
		(GInterfaceInitFunc) nautilus_fr_menu_provider_iface_init,
		NULL,
		NULL
	};

	fr_type = g_type_module_register_type (module,
					       G_TYPE_OBJECT,
					       "NautilusFileRoller",
					       &info, 0);

	g_type_module_add_interface (module,
				     fr_type,
				     NAUTILUS_TYPE_MENU_PROVIDER,
				     &menu_provider_iface_info);
}
