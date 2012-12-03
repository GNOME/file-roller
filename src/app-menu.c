/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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
#include "actions.h"
#include "app-menu.h"
#include "fr-application.h"
#include "fr-enum-types.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_ACTION(action_name) (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (application), (action_name))))


static void
update_app_menu_sensitivity (GApplication *application)
{
	GVariant         *state;
	FrWindowListMode  list_mode;

	state = g_action_get_state (G_ACTION (GET_ACTION (PREF_LISTING_LIST_MODE)));
	list_mode = _g_enum_type_get_value_by_nick (FR_TYPE_WINDOW_LIST_MODE, g_variant_get_string (state, NULL))->value;
	g_variant_unref (state);

	g_simple_action_set_enabled (GET_ACTION (PREF_UI_VIEW_FOLDERS), list_mode == FR_WINDOW_LIST_MODE_AS_DIR);
}


static void
toggle_action_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (! g_variant_get_boolean (state)));

	g_variant_unref (state);
}


static void
activate_new (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_new (NULL, windows->data);
}


static void
activate_view_folders (GSimpleAction *action,
		       GVariant      *parameter,
		       gpointer       user_data)
{
	FrApplication *application = user_data;
	GSettings     *settings;

	settings = fr_application_get_settings (application, FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_FOLDERS, g_variant_get_boolean (parameter));
}


static void
activate_list_mode (GSimpleAction *action,
		    GVariant      *parameter,
		    gpointer       user_data)
{
	FrApplication    *application = user_data;
	GSettings        *settings;
	FrWindowListMode  list_mode;

	g_simple_action_set_state (action, g_variant_new_string (g_variant_get_string (parameter, NULL)));
	list_mode = _g_enum_type_get_value_by_nick (FR_TYPE_WINDOW_LIST_MODE, g_variant_get_string (parameter, NULL))->value;

	settings = fr_application_get_settings (application, FILE_ROLLER_SCHEMA_LISTING);
	g_settings_set_enum (settings, PREF_LISTING_LIST_MODE, list_mode);
	g_settings_set_boolean (settings, PREF_LISTING_SHOW_PATH, list_mode == FR_WINDOW_LIST_MODE_FLAT);
	update_app_menu_sensitivity (G_APPLICATION (application));
}


static void
activate_help (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_manual (NULL, windows->data);
}


static void
activate_about (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       user_data)
{
	GApplication *application = user_data;
	GList        *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows != NULL)
		activate_action_about (NULL, windows->data);
}


static void
activate_quit (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	activate_action_quit (NULL, NULL);
}


static const GActionEntry app_menu_entries[] = {
	{ "new",  activate_new },
	{ PREF_UI_VIEW_FOLDERS, toggle_action_activated, NULL, "true", activate_view_folders },
	{ PREF_LISTING_LIST_MODE, activate_list_mode, "s", "'as-dir'", NULL },
	{ "help",  activate_help },
	{ "about", activate_about },
	{ "quit",  activate_quit }
};


static void
pref_view_folders_changed (GSettings  *settings,
		  	   const char *key,
		  	   gpointer    user_data)
{
	GApplication *application = user_data;

	g_simple_action_set_state (GET_ACTION (PREF_UI_VIEW_FOLDERS),
				   g_variant_new_boolean (g_settings_get_boolean (settings, PREF_UI_VIEW_FOLDERS)));
	update_app_menu_sensitivity (application);
}


static void
pref_list_mode_changed (GSettings  *settings,
			const char *key,
			gpointer    user_data)
{
	GApplication *application = user_data;

	g_simple_action_set_state (GET_ACTION (PREF_LISTING_LIST_MODE),
				   g_variant_new_string (_g_enum_type_get_value (FR_TYPE_WINDOW_LIST_MODE,
						         g_settings_get_enum (settings, PREF_LISTING_LIST_MODE))->value_nick));
	update_app_menu_sensitivity (application);
}


void
initialize_app_menu (GApplication *application)
{
	GtkBuilder *builder;
	GSettings  *settings;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);

	builder = _gtk_builder_new_from_resource ("app-menu.ui");
	gtk_application_set_app_menu (GTK_APPLICATION (application),
				      G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu")));
	g_object_unref (builder);

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI);
	g_simple_action_set_state (GET_ACTION (PREF_UI_VIEW_FOLDERS),
				   g_variant_new_boolean (g_settings_get_boolean (settings, PREF_UI_VIEW_FOLDERS)));

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING);
	g_simple_action_set_state (GET_ACTION (PREF_LISTING_LIST_MODE),
				   g_variant_new_string (_g_enum_type_get_value (FR_TYPE_WINDOW_LIST_MODE,
						   	 g_settings_get_enum (settings, PREF_LISTING_LIST_MODE))->value_nick));

	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI),
			  "changed::" PREF_UI_VIEW_FOLDERS,
			  G_CALLBACK (pref_view_folders_changed),
			  application);
	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING),
			  "changed::" PREF_LISTING_LIST_MODE,
			  G_CALLBACK (pref_list_mode_changed),
			  application);
}
