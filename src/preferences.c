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

#include <libgnome/libgnome.h>
#include <gconf/gconf-client.h>
#include "typedefs.h"
#include "preferences.h"
#include "main.h"
#include "file-utils.h"


static gint
get_int_with_default (gchar *config_path, gint def)
{
	gchar *path_with_def;
	gint result;

	path_with_def = g_strdup_printf ("%s=%d", config_path, def);
	result = gnome_config_get_int (path_with_def);
	g_free (path_with_def);
					 
	return result;
}


static gint
get_bool_with_default (gchar *config_path, gboolean def)
{
	gchar *path_with_def;
	gboolean result;

	path_with_def = g_strdup_printf ("%s=%s", config_path, 
					 def ? "true" : "false");
	result = gnome_config_get_bool (path_with_def);
	g_free (path_with_def);
					 
	return result;
}


void 
preferences_load () 
{
	GConfClient *client;
	int i;

	preferences.sort_method = get_int_with_default (
		"file-roller/Main Window/Sort Method", WINDOW_SORT_BY_NAME);
	preferences.sort_type = get_int_with_default (
		"file-roller/Main Window/Sort Type", GTK_SORT_ASCENDING);
	preferences.list_mode = get_int_with_default (
		"file-roller/Main Window/List Mode", WINDOW_LIST_MODE_AS_DIR);

	preferences.show_name = get_bool_with_default (
		"file-roller/File List/Name", TRUE);
	preferences.show_type = get_bool_with_default (
		"file-roller/File List/Type", TRUE);
	preferences.show_size = get_bool_with_default (
		"file-roller/File List/Size", TRUE);
	preferences.show_time = get_bool_with_default (
		"file-roller/File List/Time", TRUE);
	preferences.show_path = get_bool_with_default (
		"file-roller/File List/Path", TRUE);

	preferences.use_mime_icons = get_bool_with_default (
		"file-roller/File List/Use Mime Icons", TRUE);

	preferences.max_history_len = get_int_with_default (
		"file-roller/Main Window/Max History List", 5);

	preferences.view_folder = get_bool_with_default (
		"file-roller/Extract/View Folder", FALSE);

	preferences.editors_n = gnome_config_get_int (
		"file-roller/Editors/editors=0");
	preferences.editors = NULL;
	for (i = 0; i < preferences.editors_n; i++) {
		gchar *prop_name;
		gchar *prop_val;

		prop_name = g_strdup_printf ("file-roller/Editors/editor%d", i);
		prop_val = gnome_config_get_string (prop_name);
		if (prop_val != NULL)
			preferences.editors = g_list_prepend (preferences.editors, prop_val);
		g_free (prop_name);
	}

	client = gconf_client_get_default ();
	preferences.menus_have_tearoff = gconf_client_get_bool (client, "/desktop/gnome/interface/menus_have_tearoff", NULL);
	preferences.toolbar_detachable = gconf_client_get_bool (client, "/desktop/gnome/interface/toolbar_detachable", NULL);
	preferences.nautilus_theme = gconf_client_get_string (client, "/desktop/gnome/file_views/icon_theme", NULL);
	g_object_unref (G_OBJECT (client));

	preferences.install_scripts = get_bool_with_default (
		"file-roller/Main/Install Scripts", TRUE);
	preferences.scripts_installed = get_bool_with_default (
		"file-roller/Main/Scripts Installed", FALSE);
}


void 
preferences_save ()
{
	GList *scan;
	int i;

	gnome_config_set_int  ("file-roller/Main Window/Sort Method",
			       preferences.sort_method);
	gnome_config_set_int  ("file-roller/Main Window/Sort Type",
			       preferences.sort_type);
	gnome_config_set_int  ("file-roller/Main Window/List Mode",
			       preferences.list_mode);

	gnome_config_set_bool ("file-roller/File List/Name",
			       preferences.show_name);
	gnome_config_set_bool ("file-roller/File List/Type",
			       preferences.show_type);
	gnome_config_set_bool ("file-roller/File List/Size",
			       preferences.show_size);
	gnome_config_set_bool ("file-roller/File List/Time",
			       preferences.show_time);
	gnome_config_set_bool ("file-roller/File List/Path",
			       preferences.show_path);
	gnome_config_set_bool ("file-roller/File List/Use Mime Icons",
			       preferences.use_mime_icons);

	gnome_config_set_int  ("file-roller/Main Window/Max History List",
			       preferences.max_history_len);

	gnome_config_set_bool ("file-roller/Extract/View Folder",
			       preferences.view_folder);
	
	gnome_config_set_int ("file-roller/Editors/editors", preferences.editors_n);
	for (i = 0, scan = preferences.editors; scan; scan = scan->next) {
		gchar *prop_name;
		prop_name = g_strdup_printf ("file-roller/Editors/editor%d", i++);
		gnome_config_set_string (prop_name, (gchar*) scan->data);
		g_free (prop_name);
	}

	gnome_config_set_bool ("file-roller/Main/Install Scripts",
			       preferences.install_scripts);
	gnome_config_set_bool ("file-roller/Main/Scripts Installed",
			       preferences.scripts_installed);

	gnome_config_sync ();
}


void
preferences_release ()
{
	path_list_free (preferences.editors);
	if (preferences.nautilus_theme != NULL)
		g_free (preferences.nautilus_theme);
}
