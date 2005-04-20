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
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
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
	char             *uri, *path, *default_dir;
	GString          *cmd;

	files = g_object_get_data (G_OBJECT (item), "files");
	file = files->data;

	uri = nautilus_file_info_get_uri (file);
	path = gnome_vfs_get_local_path_from_uri (uri);
	default_dir = g_strconcat ("file://", 
				   g_get_home_dir(), 
				   "/",
				   "Desktop", 
				   NULL);
	cmd = g_string_new ("file-roller");
	g_string_append_printf (cmd, 
				" --default-dir=\"%s\" --extract \"%s\"", 
				default_dir,
				path);

#ifdef DEBUG
	g_print ("EXEC: %s\n", cmd->str);
#endif

	g_spawn_command_line_async (cmd->str, NULL);

	g_string_free (cmd, TRUE);
	g_free (default_dir);
	g_free (path);
	g_free (uri);
}


static void
extract_here_callback (NautilusMenuItem *item,
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
	g_string_append_printf (cmd," --default-dir=\"%s\" --extract-here --force", dir);

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
	"application/x-ar",
	"application/x-arj",
	"application/x-bzip",
	"application/x-bzip-compressed-tar",
	"application/x-compress",
	"application/x-compressed-tar",
	"application/x-deb",
	"application/x-gtar",
	"application/x-gzip",
	"application/x-lha",
	"application/x-lhz",
	"application/x-rar",
	"application/x-rar-compressed",
	"application/x-tar",
	"application/x-zip",
	"application/x-zip-compressed",
	"application/zip",
	"multipart/x-zip",
	"application/x-rpm",
	"application/x-jar",
	"application/x-java-archive",
	"application/x-lzop",
	"application/x-zoo",
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
	gboolean  all_archives = TRUE;


	if (files == NULL)
		return NULL;
	
	for (scan = files; scan; scan = scan->next) {
		NautilusFileInfo *file = scan->data;
		char             *scheme;
		gboolean          local;

		scheme = nautilus_file_info_get_uri_scheme (file);
		local = strncmp (scheme, "file", 4) == 0;
		g_free (scheme);

		if (!local)
			return NULL;

		if (all_archives && ! is_archive (file))
			all_archives = FALSE;


		if (can_write) {
			char             *parent_uri;
			GnomeVFSFileInfo *info;
			GnomeVFSResult    result;

			parent_uri = nautilus_file_info_get_parent_uri (file);
			info = gnome_vfs_file_info_new ();
			result = gnome_vfs_get_file_info (parent_uri,
							  info,
							  GNOME_VFS_FILE_INFO_GET_ACCESS_RIGHTS);
			if (result == GNOME_VFS_OK)
				can_write = (info->permissions & GNOME_VFS_PERM_ACCESS_WRITABLE) != 0;
			gnome_vfs_file_info_unref (info);
			g_free (parent_uri);
		}
	}

	/**/

	one_item = (files != NULL) && (files->next == NULL);
	one_archive = one_item && all_archives;

	if (all_archives && can_write) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract_here",
					       _("Extract Here"),
					       _("Extract the selected archive in the current position"),
					       NULL);
		g_signal_connect (item, 
				  "activate",
				  G_CALLBACK (extract_here_callback),
				  provider);
		g_object_set_data_full (G_OBJECT (item), 
					"files",
					nautilus_file_info_list_copy (files),
					(GDestroyNotify) nautilus_file_info_list_free);

		items = g_list_append (items, item);

	} else if (all_archives && ! can_write) {
		NautilusMenuItem *item;

		item = nautilus_menu_item_new ("NautilusFr::extract_to",
					       _("Extract To..."),
					       _("Extract the selected archive"),
					       NULL);
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

	if (! one_archive) {
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
