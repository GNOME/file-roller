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
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <libnautilus-extension/nautilus-extension-types.h>
#include <libnautilus-extension/nautilus-file-info.h>
#include <libnautilus-extension/nautilus-menu-provider.h>
#include <locale.h>
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
	g_string_append_printf (cmd," --extract-here --notify");

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

/** mime-types which aren't supported by nautilus itself */
static struct {
	char     *mime_type;
	gboolean  is_compressed;
} archive_mime_types[] = {
		{ "application/x-ace", TRUE },
		{ "application/x-alz", TRUE },
		{ "application/x-ar", TRUE },
		{ "application/x-arj", TRUE },
		{ "application/x-brotli", TRUE },
		{ "application/x-brotli-compressed-tar", TRUE },
		{ "application/vnd.ms-cab-compressed", TRUE },
		{ "application/x-cbr", TRUE },
		{ "application/x-cbz", TRUE },
		{ "application/x-cd-image", FALSE },
		{ "application/x-deb", TRUE },
		{ "application/vnd.debian.binary-package", TRUE },
		{ "application/x-ear", TRUE },
		{ "application/x-ms-dos-executable", FALSE },
		{ "application/x-gtar", FALSE },
		{ "application/x-gzpostscript", TRUE },
		{ "application/x-java-archive", TRUE },
		{ "application/x-lhz", TRUE },
		{ "application/x-lzop", TRUE },
		{ "application/x-lzop-compressed-tar", TRUE },
		{ "application/x-ms-wim", TRUE },
		{ "application/x-rar", TRUE },
		{ "application/x-rar-compressed", TRUE },
		{ "application/x-rpm", TRUE },
		{ "application/x-rzip", TRUE },
		{ "application/x-stuffit", TRUE },
		{ "application/x-war", TRUE },
		{ "application/x-zoo", TRUE },
		{ "multipart/x-zip", TRUE },
		{ NULL, FALSE }
};


typedef struct {
      gboolean is_archive;
      gboolean is_derived_archive;
      gboolean is_compressed_archive;
} FileMimeInfo;


static FileMimeInfo
get_file_mime_info (NautilusFileInfo *file)
{
	FileMimeInfo file_mime_info;
	int          i;

	file_mime_info.is_archive = FALSE;
	file_mime_info.is_derived_archive = FALSE;
	file_mime_info.is_compressed_archive = FALSE;

	for (i = 0; archive_mime_types[i].mime_type != NULL; i++)
		if (nautilus_file_info_is_mime_type (file, archive_mime_types[i].mime_type)) {
			char *mime_type;
			char *content_type_mime_file;
			char *content_type_mime_compare;

			mime_type = nautilus_file_info_get_mime_type (file);

			content_type_mime_file = g_content_type_from_mime_type (mime_type);
			content_type_mime_compare = g_content_type_from_mime_type (archive_mime_types[i].mime_type);

			file_mime_info.is_archive = TRUE;
			file_mime_info.is_compressed_archive = archive_mime_types[i].is_compressed;
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
		const char *unsupported[] = { "trash", "computer", "x-nautilus-desktop", NULL };
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
	gboolean  all_archives = TRUE;
	gboolean  all_archives_derived = TRUE;
	gboolean  all_archives_compressed = TRUE;

	if (files == NULL)
		return NULL;

	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;
		FileMimeInfo      file_mime_info;

		if (unsupported_scheme (file))
			return NULL;

		file_mime_info = get_file_mime_info (file);

		if (all_archives && ! file_mime_info.is_archive)
			all_archives = FALSE;

		if (all_archives_compressed && file_mime_info.is_archive && ! file_mime_info.is_compressed_archive)
			all_archives_compressed = FALSE;

		if (all_archives_derived && file_mime_info.is_archive && ! file_mime_info.is_derived_archive)
			all_archives_derived = FALSE;

		if (can_write) {
			NautilusFileInfo *parent;

			parent = nautilus_file_info_get_parent_info (file);
 			can_write = nautilus_file_info_can_write (parent);
			g_object_unref (parent);
		}
	}

	/**/

	if (all_archives && can_write) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract_here",
					       g_dcgettext ("file-roller", "Extract Here", LC_MESSAGES),
					       /* Translators: the current position is the current folder */
					       g_dcgettext ("file-roller", "Extract the selected archive to the current position", LC_MESSAGES),
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
					       g_dcgettext ("file-roller", "Extract To…", LC_MESSAGES),
					       g_dcgettext ("file-roller", "Extract the selected archive", LC_MESSAGES),
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
