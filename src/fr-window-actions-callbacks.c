/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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
#include "dlg-add.h"
#include "dlg-extract.h"
#include "dlg-password.h"
#include "dlg-prop.h"
#include "fr-window.h"
#include "fr-window-actions-callbacks.h"


void
toggle_action_activated (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (! g_variant_get_boolean (state)));

	g_variant_unref (state);
}


static GtkWidget *
_gtk_application_get_current_window (GApplication *application)
{
	GList *windows;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	if (windows == NULL)
		return NULL;

	return GTK_WIDGET (windows->data);
}


void
fr_window_activate_add_files (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	dlg_add (FR_WINDOW (user_data));
}


void
fr_window_activate_close (GSimpleAction *action,
			  GVariant      *parameter,
			  gpointer       user_data)
{
	fr_window_close (FR_WINDOW (user_data));
}


void
fr_window_activate_edit_find (GSimpleAction *action,
			      GVariant      *state,
			      gpointer       user_data)
{
	FrWindow *window = FR_WINDOW (user_data);

	g_simple_action_set_state (action, state);
	fr_window_find (window, g_variant_get_boolean (state));
}


void
fr_window_activate_edit_password (GSimpleAction *action,
				  GVariant      *parameter,
				  gpointer       user_data)
{
	dlg_password (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_extract_files (GSimpleAction *action,
				  GVariant      *parameter,
				  gpointer       user_data)
{
	dlg_extract (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_go_back (GSimpleAction *action,
			    GVariant      *parameter,
			    gpointer       user_data)
{
	fr_window_go_back (FR_WINDOW (user_data));
}


void
fr_window_activate_go_forward (GSimpleAction *action,
			       GVariant      *parameter,
			       gpointer       user_data)
{
	fr_window_go_forward (FR_WINDOW (user_data));
}


void
fr_window_activate_go_home (GSimpleAction *action,
		            GVariant      *parameter,
		            gpointer       user_data)
{
	fr_window_go_to_location (FR_WINDOW (user_data), "/", FALSE);
}


void
fr_window_activate_save_as (GSimpleAction *action,
			    GVariant      *parameter,
			    gpointer       user_data)
{
	fr_window_action_save_as (FR_WINDOW (user_data));
}


void
fr_window_activate_test_archive (GSimpleAction *action,
				 GVariant      *parameter,
				 gpointer       user_data)
{
	fr_window_archive_test (FR_WINDOW (user_data));
}


void
fr_window_activate_view_properties (GSimpleAction *action,
				    GVariant      *parameter,
				    gpointer       user_data)
{
	dlg_prop (FR_WINDOW (user_data));
}
