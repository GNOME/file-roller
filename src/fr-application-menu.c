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
#include "fr-application.h"
#include "fr-application-menu.h"
#include "fr-enum-types.h"
#include "fr-window-actions-callbacks.h"
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

	g_simple_action_set_enabled (GET_ACTION (PREF_UI_VIEW_SIDEBAR), list_mode == FR_WINDOW_LIST_MODE_AS_DIR);
}


static void
fr_application_activate_new (GSimpleAction *action,
			     GVariant      *parameter,
			     gpointer       user_data)
{
	GtkWidget *window;

	window = _gtk_application_get_current_window (G_APPLICATION (user_data));
	if (window != NULL)
		fr_window_activate_new (action, parameter, window);
}


static void
fr_application_activate_open (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	GtkWidget *window;

	window = _gtk_application_get_current_window (G_APPLICATION (user_data));
	if (window != NULL)
		fr_window_activate_open (action, parameter, window);
}


static void
fr_application_activate_view_sidebar (GSimpleAction *action,
		       	       	      GVariant      *parameter,
		       	       	      gpointer       user_data)
{
	FrApplication *application = user_data;
	GSettings     *settings;

	settings = fr_application_get_settings (application, FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_SIDEBAR, g_variant_get_boolean (parameter));
}


static void
fr_application_activate_list_mode (GSimpleAction *action,
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
fr_application_activate_help (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	GtkWidget *window;

	window = _gtk_application_get_current_window (G_APPLICATION (user_data));
	_gtk_show_help_dialog (GTK_WINDOW (window) , NULL);
}


static void
fr_application_activate_about (GSimpleAction *action,
			       GVariant      *parameter,
			       gpointer       user_data)
{
	const char *authors[] = { "Paolo Bacchilega <paolo.bacchilega@libero.it>", NULL	};
	const char *documenters [] = { "Alexander Kirillov", "Breda McColgan", NULL };

	gtk_show_about_dialog (GTK_WINDOW (_gtk_application_get_current_window (G_APPLICATION (user_data))),
			       "version", PACKAGE_VERSION,
			       "copyright", _("Copyright \xc2\xa9 2001–2014 Free Software Foundation, Inc."),
			       "comments", _("An archive manager for GNOME."),
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", _("translator-credits"),
			       "logo-icon-name", "org.gnome.ArchiveManager",
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "website", "https://wiki.gnome.org/Apps/FileRoller",
			       "wrap-license", TRUE,
			       NULL);
}


static void
fr_application_activate_quit (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	g_application_quit (G_APPLICATION (user_data));
}


static const GActionEntry app_menu_entries[] = {
	{ "new",  fr_application_activate_new },
	{ "open",  fr_application_activate_open },
	{ PREF_UI_VIEW_SIDEBAR, toggle_action_activated, NULL, "true", fr_application_activate_view_sidebar },
	{ PREF_LISTING_LIST_MODE, fr_application_activate_list_mode, "s", "'as-dir'", NULL },
	{ "help",  fr_application_activate_help },
	{ "about", fr_application_activate_about },
	{ "quit",  fr_application_activate_quit }
};


static const FrAccelerator fr_app_accelerators[] = {
	{ "app.new", "<Control>n" },
	{ "app.open", "<Control>o" },
	{ "app.help", "F1" },
	{ "app.quit", "<Control>q" },
	{ "app.list-mode::flat", "<Control>1" },
	{ "app.list-mode::as-dir", "<Control>2" },
};


static void
pref_view_sidebar_changed (GSettings  *settings,
		  	   const char *key,
		  	   gpointer    user_data)
{
	GApplication *application = user_data;

	g_simple_action_set_state (GET_ACTION (PREF_UI_VIEW_SIDEBAR),
				   g_variant_new_boolean (g_settings_get_boolean (settings, PREF_UI_VIEW_SIDEBAR)));
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
	GSettings *settings;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);
	_gtk_application_add_accelerators (GTK_APPLICATION (application), fr_app_accelerators, G_N_ELEMENTS (fr_app_accelerators));

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI);
	g_simple_action_set_state (GET_ACTION (PREF_UI_VIEW_SIDEBAR),
				   g_variant_new_boolean (g_settings_get_boolean (settings, PREF_UI_VIEW_SIDEBAR)));

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING);
	g_simple_action_set_state (GET_ACTION (PREF_LISTING_LIST_MODE),
				   g_variant_new_string (_g_enum_type_get_value (FR_TYPE_WINDOW_LIST_MODE,
						   	 g_settings_get_enum (settings, PREF_LISTING_LIST_MODE))->value_nick));

	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI),
			  "changed::" PREF_UI_VIEW_SIDEBAR,
			  G_CALLBACK (pref_view_sidebar_changed),
			  application);
	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING),
			  "changed::" PREF_LISTING_LIST_MODE,
			  G_CALLBACK (pref_list_mode_changed),
			  application);
}


void
initialize_app_menubar (GApplication *application)
{
	GtkBuilder *builder;
	GSettings  *settings;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);

	builder = _gtk_builder_new_from_resource ("app-menubar.ui");
	gtk_application_set_menubar (GTK_APPLICATION (application),
				     G_MENU_MODEL (gtk_builder_get_object (builder, "app-menubar")));
	g_object_unref (builder);

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI);
	g_simple_action_set_state (GET_ACTION (PREF_UI_VIEW_SIDEBAR),
				   g_variant_new_boolean (g_settings_get_boolean (settings, PREF_UI_VIEW_SIDEBAR)));

	settings = fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING);
	g_simple_action_set_state (GET_ACTION (PREF_LISTING_LIST_MODE),
				   g_variant_new_string (_g_enum_type_get_value (FR_TYPE_WINDOW_LIST_MODE,
						   	 g_settings_get_enum (settings, PREF_LISTING_LIST_MODE))->value_nick));

	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_UI),
			  "changed::" PREF_UI_VIEW_SIDEBAR,
			  G_CALLBACK (pref_view_sidebar_changed),
			  application);
	g_signal_connect (fr_application_get_settings (FR_APPLICATION (application), FILE_ROLLER_SCHEMA_LISTING),
			  "changed::" PREF_LISTING_LIST_MODE,
			  G_CALLBACK (pref_list_mode_changed),
			  application);
}
