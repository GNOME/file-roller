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
#include <glib/gi18n-lib.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libnautilus-extension/nautilus-extension-types.h>
#include <libnautilus-extension/nautilus-file-info.h>
#include <libnautilus-extension/nautilus-menu-provider.h>
#include "nautilus-fileroller.h"


static GObjectClass *parent_class;


static void
extract_callback (NautilusMenuItem *item,
		  gpointer          user_data)
{
	GList            *files;
	NautilusFileInfo *file;
	char             *uri, *path, *dir_files;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	uri = nautilus_file_info_get_uri (file);
	path = gnome_vfs_get_local_path_from_uri (uri);
	dir_files = g_strconcat (path, "_FILES", NULL);

	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd, 
				" --force --extract-to=\"%s\" \"%s\"", 
				dir_files,
				path);

	g_spawn_command_line_async (cmd->str, NULL);

	g_string_free (cmd, TRUE);
	g_free (dir_files);
	g_free (path);
	g_free (uri);
}


static void
add_callback (NautilusMenuItem *item,
	      gpointer          user_data)
{
	GList            *files, *scan;
	NautilusFileInfo *file;
	char             *uri, *path, *dir;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	uri = nautilus_file_info_get_uri (file);
	path = gnome_vfs_get_local_path_from_uri (uri);
	dir = g_path_get_dirname (path);

	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd," --default-dir=\"%s\" --add", dir);

	g_free (dir);
	g_free (path);
	g_free (uri);

	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;
		
		uri = nautilus_file_info_get_uri (file);
		path = gnome_vfs_get_local_path_from_uri (uri);

		g_string_append_printf (cmd, " \"%s\"", path);

		g_free (path);
		g_free (uri);
	}

	g_spawn_command_line_async (cmd->str, NULL);

	g_string_free (cmd, TRUE);
}	 


static char *mime_types[] = {
	"application/x-tar",
	"application/x-compressed-tar",
	"application/x-bzip-compressed-tar",
	"application/x-lzop-compressed-tar",
	"application/zip",
	"application/x-arj",
	"application/x-zip",
	"application/x-lha",
	"application/x-rar",
	"application/x-rar-compressed",
	"application/x-gzip",
	"application/x-bzip",
	"application/x-compress",
	"application/x-lzop",
	"application/x-zoo",
	"application/x-jar",
	"application/x-cd-image",
	NULL
};


static gboolean
is_archive (NautilusFileInfo *file)
{
	int i;

	for (i = 0; mime_types[i] != NULL; i++)
		if (nautilus_file_info_is_mime_type (file, mime_types[i]))
			return TRUE;

	return FALSE;
}


/* nautilus_menu_provider_get_file_items
 *  
 * This function is called by Nautilus when it wants context menu
 * items from the extension.
 *
 * This function is called in the main thread before a context menu
 * is shown, so it should return quickly.
 * 
 * The function should return a GList of allocated NautilusMenuItem
 * items.
 */
static GList *
nautilus_fr_get_file_items (NautilusMenuProvider *provider,
			    GtkWidget            *window,
			    GList                *files)
{
	gboolean  one_item;
	gboolean  one_archive = FALSE;
	GList    *items = NULL;


	one_item = (files != NULL) && (files->next == NULL);

	if (one_item) {
		NautilusFileInfo *file = files->data;
		one_archive = is_archive (file);
	}

	if (one_archive) {		
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract",
					       _("Extract Here"),
					       _("Extract the selected archive in the current position"),
					       NULL);
		g_signal_connect (item, 
				  "activate",
				  G_CALLBACK (extract_callback),
				  provider);
		g_object_set_data_full (G_OBJECT (item), 
					"files",
					nautilus_file_info_list_copy (files),
					(GDestroyNotify) nautilus_file_info_list_free);

		items = g_list_append (items, item);

	} else {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::add",
					       _("Create Archive..."),
					       _("Create an archive with the selected objects"),
					       NULL);
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


/* Type registration.  Because this type is implemented in a module
 * that can be unloaded, we separate type registration from get_type().
 * the type_register() function will be called by the module's
 * initialization function. */

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
