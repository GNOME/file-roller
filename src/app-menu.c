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
#include "gtk-utils.h"


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
	{ "help",  activate_help },
	{ "about", activate_about },
	{ "quit",  activate_quit }
};


void
initialize_app_menu (GApplication *application)
{
	gboolean    show_app_menu;
	GtkBuilder *builder;

	g_object_get (gtk_settings_get_default (),
		      "gtk-shell-shows-app-menu", &show_app_menu,
		      NULL);
	if (! show_app_menu)
		return;

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 app_menu_entries,
					 G_N_ELEMENTS (app_menu_entries),
					 application);

	builder = _gtk_builder_new_from_resource ("app-menu.ui");
	gtk_application_set_app_menu (GTK_APPLICATION (application),
				      G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu")));

	g_object_unref (builder);
}
