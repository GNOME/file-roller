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
#include "dlg-delete.h"
#include "dlg-extract.h"
#include "dlg-open-with.h"
#include "dlg-password.h"
#include "dlg-prop.h"
#include "fr-init.h"
#include "fr-window.h"
#include "fr-window-actions-callbacks.h"
#include "gtk-utils.h"


void
fr_toggle_action_activated (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (! g_variant_get_boolean (state)));

	g_variant_unref (state);
}


GtkWidget *
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
fr_window_activate_delete (GSimpleAction *action,
			   GVariant      *parameter,
			   gpointer       user_data)
{
	dlg_delete (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_deselect_all (GSimpleAction *action,
				 GVariant      *parameter,
				 gpointer       user_data)
{
	fr_window_unselect_all (FR_WINDOW (user_data));
}


void
fr_window_activate_edit_copy (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	fr_window_copy_selection (FR_WINDOW (user_data), FALSE);
}


void
fr_window_activate_edit_cut (GSimpleAction *action,
			     GVariant      *parameter,
			     gpointer       user_data)
{
	fr_window_cut_selection (FR_WINDOW (user_data), FALSE);
}


void
fr_window_activate_edit_password (GSimpleAction *action,
				  GVariant      *parameter,
				  gpointer       user_data)
{
	dlg_password (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_edit_paste (GSimpleAction *action,
			       GVariant      *parameter,
			       gpointer       user_data)
{
	fr_window_paste_selection (FR_WINDOW (user_data), FALSE);
}


void
fr_window_activate_extract_files (GSimpleAction *action,
				  GVariant      *parameter,
				  gpointer       user_data)
{
	dlg_extract (NULL, FR_WINDOW (user_data));
}

void
fr_window_activate_extract_all_by_default (GSimpleAction *action,
					   GVariant      *parameter,
					   gpointer       user_data)
{
	dlg_extract_all_by_default (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_find (GSimpleAction *action,
			 GVariant      *state,
			 gpointer       user_data)
{
	FrWindow *window = FR_WINDOW (user_data);

	g_simple_action_set_state (action, state);
	fr_window_find (window, g_variant_get_boolean (state));
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
fr_window_activate_go_up_one_level (GSimpleAction *action,
				    GVariant      *parameter,
				    gpointer       user_data)
{
	fr_window_go_up_one_level (FR_WINDOW (user_data));
}

void
fr_window_activate_open_folder (GSimpleAction *action,
				GVariant      *parameter,
				gpointer       user_data)
{
	fr_window_current_folder_activated (FR_WINDOW (user_data), FALSE);
}


void
fr_window_activate_open_with (GSimpleAction *action,
			      GVariant      *parameter,
			      gpointer       user_data)
{
	open_with_cb (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_reload (GSimpleAction *action,
			   GVariant      *parameter,
			   gpointer       user_data)
{
	fr_window_archive_reload (FR_WINDOW (user_data));
}


void
fr_window_activate_rename (GSimpleAction *action,
			   GVariant      *parameter,
			   gpointer       user_data)
{
	fr_window_rename_selection (FR_WINDOW (user_data), FALSE);
}


void
fr_window_activate_navigate_to (GSimpleAction *action,
			GVariant      *parameter,
			gpointer       user_data)
{
	FrWindow *window = FR_WINDOW (user_data);
	GList *file_list = fr_window_get_file_list_selection (window, FALSE, TRUE, NULL);

	if (file_list == NULL) {
		return;
	}

	// g_path_get_dirname will return the directory itself if the path ends with /
	// so we need to trim it first to be able to get the parent.
	g_autofree char *selected_path = g_str_has_suffix (file_list->data, G_DIR_SEPARATOR_S) ? g_path_get_dirname (file_list->data) : g_strdup (file_list->data);
	g_autofree char *selected_location = g_path_get_dirname (selected_path);
	g_autofree char *selected_location_abs = g_strdup_printf ("/%s", selected_location);

	fr_window_go_to_location (window, selected_location_abs, TRUE);
	fr_window_find (window, FALSE);

	_g_string_list_free (file_list);
}


void
fr_window_activate_new (GSimpleAction *action,
			GVariant      *parameter,
			gpointer       user_data)
{
	fr_window_action_new_archive (FR_WINDOW (user_data));
}


/* -- fr_window_activate_open -- */


static void
window_archive_loaded_cb (FrWindow  *window,
			  gboolean   success,
			  GtkWidget *file_sel)
{
	if (success) {
		g_signal_handlers_disconnect_by_data (window, file_sel);
		gtk_window_destroy (GTK_WINDOW (file_sel));
	}
	else {
		FrWindow *original_window =  g_object_get_data (G_OBJECT (file_sel), "fr_window");
		if (window != original_window)
			fr_window_destroy_with_error_dialog (window);
	}
}


static void
open_file_response_cb (GtkDialog *dialog,
		       int        response,
		       GtkWidget *file_sel)
{
	FrWindow *window = NULL;
	GFile    *file;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (file_sel));
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");
	file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (file_sel));

	if ((window == NULL) || (file == NULL))
		return;

	if (fr_window_archive_is_present (window))
		window = (FrWindow *) fr_window_new ();
	g_signal_connect (FR_WINDOW (window),
			  "archive_loaded",
			  G_CALLBACK (window_archive_loaded_cb),
			  file_sel);
	fr_window_archive_open (window, file, GTK_WINDOW (file_sel));

	g_object_unref (file);
}


void
fr_window_activate_open (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       user_data)
{
	FrWindow      *window = user_data;
	GtkWidget     *file_sel;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (C_("Window title", "Open"),
						GTK_WINDOW (window),
						GTK_FILE_CHOOSER_ACTION_OPEN,
						_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						_GTK_LABEL_OPEN, GTK_RESPONSE_OK,
						NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel), fr_window_get_open_default_dir (window), NULL);
	_gtk_dialog_add_to_window_group (GTK_DIALOG (file_sel));
	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; open_type[i] != -1; i++)
		gtk_file_filter_add_mime_type (filter, mime_type_desc[open_type[i]].mime_type);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);

	g_signal_connect (GTK_FILE_CHOOSER_DIALOG (file_sel),
			  "response",
			  G_CALLBACK (open_file_response_cb),
			  file_sel);

	gtk_window_present (GTK_WINDOW (file_sel));
}


void
fr_window_activate_save_as (GSimpleAction *action,
			    GVariant      *parameter,
			    gpointer       user_data)
{
	fr_window_action_save_as (FR_WINDOW (user_data));
}


void
fr_window_activate_select_all (GSimpleAction *action,
			       GVariant      *parameter,
			       gpointer       user_data)
{
	fr_window_select_all (FR_WINDOW (user_data));
}


void
fr_window_activate_sidebar_delete (GSimpleAction *action,
				   GVariant      *parameter,
				   gpointer       user_data)
{
	dlg_delete_from_sidebar (NULL, FR_WINDOW (user_data));
}


void
fr_window_activate_sidebar_edit_copy (GSimpleAction *action,
				      GVariant      *parameter,
				      gpointer       user_data)
{
	fr_window_copy_selection (FR_WINDOW (user_data), TRUE);
}


void
fr_window_activate_sidebar_edit_cut (GSimpleAction *action,
				     GVariant      *parameter,
				     gpointer       user_data)
{
	fr_window_cut_selection (FR_WINDOW (user_data), TRUE);
}


void
fr_window_activate_sidebar_edit_paste (GSimpleAction *action,
				       GVariant      *parameter,
				       gpointer       user_data)
{
	fr_window_paste_selection (FR_WINDOW (user_data), TRUE);
}


void
fr_window_activate_sidebar_extract_files (GSimpleAction *action,
					  GVariant      *parameter,
					  gpointer       user_data)
{
	dlg_extract_folder_from_sidebar (NULL, user_data);
}


void
fr_window_activate_sidebar_open_folder (GSimpleAction *action,
					GVariant      *parameter,
					gpointer       user_data)
{
	fr_window_current_folder_activated (FR_WINDOW (user_data), TRUE);
}


void
fr_window_activate_sidebar_rename (GSimpleAction *action,
				   GVariant      *parameter,
				   gpointer       user_data)
{
	fr_window_rename_selection (FR_WINDOW (user_data), TRUE);
}


void
fr_window_activate_stop (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       user_data)
{
	fr_window_stop (FR_WINDOW (user_data));
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


void
fr_window_activate_view_selection (GSimpleAction *action,
				   GVariant      *parameter,
				   gpointer       user_data)
{
	FrWindow *window = FR_WINDOW (user_data);
	GList    *file_list;

	file_list = fr_window_get_file_list_selection (window, FALSE, FALSE, NULL);
	if (file_list != NULL)
		fr_window_open_files (window, file_list, FALSE);

	_g_string_list_free (file_list);
}


void
fr_window_activate_view_sidebar (GSimpleAction *action,
				 GVariant      *state,
				 gpointer       user_data)
{
	GSettings *settings;

	g_simple_action_set_state (action, state);
	settings = g_settings_new (FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_SIDEBAR, g_variant_get_boolean (state));

	g_object_unref (settings);
}


void
fr_window_activate_focus_location (GSimpleAction *action,
				   GVariant      *state,
				   gpointer       user_data)
{
	fr_window_focus_location (FR_WINDOW (user_data));
}
