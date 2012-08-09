/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
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
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "actions.h"
#include "dlg-add.h"
#include "dlg-extract.h"
#include "dlg-delete.h"
#include "dlg-open-with.h"
#include "dlg-password.h"
#include "dlg-prop.h"
#include "gtk-utils.h"
#include "fr-window.h"
#include "file-utils.h"
#include "fr-process.h"
#include "glib-utils.h"
#include "fr-init.h"
#include "typedefs.h"


void
activate_action_new (GtkAction *action,
		     gpointer   data)
{
	fr_window_action_new_archive (FR_WINDOW (data));
}


/* -- open archive -- */


static void
window_archive_loaded_cb (FrWindow  *window,
			  gboolean   success,
			  GtkWidget *file_sel)
{
	if (success) {
		g_signal_handlers_disconnect_by_data (window, file_sel);
		gtk_widget_destroy (file_sel);
	}
	else {
		FrWindow *original_window =  g_object_get_data (G_OBJECT (file_sel), "fr_window");
		if (window != original_window)
			fr_window_destroy_with_error_dialog (window);
	}
}


static void
open_file_response_cb (GtkWidget *w,
		       int        response,
		       GtkWidget *file_sel)
{
	FrWindow *window = NULL;
	GFile    *file;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");
	file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (file_sel));

	if ((window == NULL) || (file == NULL))
		return;

	if (fr_window_archive_is_present (window))
		window = (FrWindow *) fr_window_new ();
	g_signal_connect (G_OBJECT (window),
			  "archive_loaded",
			  G_CALLBACK (window_archive_loaded_cb),
			  file_sel);
	fr_window_archive_open (window, file, GTK_WINDOW (file_sel));

	g_object_unref (file);
}


void
activate_action_open (GtkAction *action,
		      gpointer   data)
{
	GtkWidget     *file_sel;
	FrWindow      *window = data;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (_("Open"),
						GTK_WINDOW (window),
						GTK_FILE_CHOOSER_ACTION_OPEN,
						GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						GTK_STOCK_OPEN, GTK_RESPONSE_OK,
						NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_sel), FALSE);
	gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (file_sel), fr_window_get_open_default_dir (window), NULL);
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

	g_signal_connect (G_OBJECT (file_sel),
			  "response",
			  G_CALLBACK (open_file_response_cb),
			  file_sel);

	gtk_widget_show (file_sel);
}


/* -- save archive -- */


void
activate_action_save_as (GtkAction *action,
			 gpointer   user_data)
{
	fr_window_action_save_as (FR_WINDOW (user_data));
}


void
activate_action_test_archive (GtkAction *action,
			      gpointer   data)
{
	FrWindow *window = data;

	fr_window_archive_test (window);
}


void
activate_action_properties (GtkAction *action,
			    gpointer   data)
{
	FrWindow *window = data;

	dlg_prop (window);
}


void
activate_action_close (GtkAction *action,
		       gpointer   data)
{
	FrWindow *window = data;

	fr_window_close (window);
}


void
activate_action_quit (GtkAction *action,
		      gpointer   data)
{
	GList *windows;

	/* Copy the list to avoid possible errors if the original list is
	 * modified after closing a window. */
	windows = g_list_copy (gtk_application_get_windows (GTK_APPLICATION (g_application_get_default ())));
	g_list_foreach (windows, (GFunc) fr_window_close, NULL);

	g_list_free (windows);
}


void
activate_action_add (GtkAction *action,
		     gpointer   data)
{
	dlg_add (FR_WINDOW (data));
}


void
activate_action_extract (GtkAction *action,
			 gpointer   data)
{
	dlg_extract (NULL, data);
}


void
activate_action_extract_folder_from_sidebar (GtkAction *action,
			 		     gpointer   data)
{
	dlg_extract_folder_from_sidebar (NULL, data);
}


void
activate_action_copy (GtkAction *action,
		      gpointer   data)
{
	fr_window_copy_selection ((FrWindow*) data, FALSE);
}


void
activate_action_cut (GtkAction *action,
		     gpointer   data)
{
	fr_window_cut_selection ((FrWindow*) data, FALSE);
}


void
activate_action_paste (GtkAction *action,
		       gpointer   data)
{
	fr_window_paste_selection ((FrWindow*) data, FALSE);
}


void
activate_action_rename (GtkAction *action,
			gpointer   data)
{
	fr_window_rename_selection ((FrWindow*) data, FALSE);
}


void
activate_action_delete (GtkAction *action,
			gpointer   data)
{
	dlg_delete (NULL, data);
}


void
activate_action_copy_folder_from_sidebar (GtkAction *action,
		      			  gpointer   data)
{
	fr_window_copy_selection ((FrWindow*) data, TRUE);
}


void
activate_action_cut_folder_from_sidebar (GtkAction *action,
		     			 gpointer   data)
{
	fr_window_cut_selection ((FrWindow*) data, TRUE);
}


void
activate_action_paste_folder_from_sidebar (GtkAction *action,
		       			   gpointer   data)
{
	fr_window_paste_selection ((FrWindow*) data, TRUE);
}


void
activate_action_rename_folder_from_sidebar (GtkAction *action,
					    gpointer   data)
{
	fr_window_rename_selection ((FrWindow*) data, TRUE);
}


void
activate_action_delete_folder_from_sidebar (GtkAction *action,
					    gpointer   data)
{
	dlg_delete_from_sidebar (NULL, data);
}


void
activate_action_find (GtkAction *action,
		      gpointer   data)
{
	FrWindow *window = data;

	fr_window_find (window);
}


void
activate_action_select_all (GtkAction *action,
			    gpointer   data)
{
	FrWindow *window = data;

	fr_window_select_all (window);
}


void
activate_action_deselect_all (GtkAction *action,
			      gpointer   data)
{
	FrWindow *window = data;

	fr_window_unselect_all (window);
}


void
activate_action_open_with (GtkAction *action,
			   gpointer   data)
{
	open_with_cb (NULL, (FrWindow*) data);
}


void
activate_action_view_or_open (GtkAction *action,
			      gpointer   data)
{
	FrWindow *window = data;
	GList    *file_list;

	file_list = fr_window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL)
		return;
	fr_window_open_files (window, file_list, FALSE);
	_g_string_list_free (file_list);
}


void
activate_action_open_folder (GtkAction *action,
			     gpointer   data)
{
	FrWindow *window = data;
	fr_window_current_folder_activated (window, FALSE);
}


void
activate_action_open_folder_from_sidebar (GtkAction *action,
			                  gpointer   data)
{
	FrWindow *window = data;
	fr_window_current_folder_activated (window, TRUE);
}


void
activate_action_password (GtkAction *action,
			  gpointer   data)
{
	dlg_password (NULL, (FrWindow*) data);
}


void
activate_action_view_toolbar (GtkAction *action,
			      gpointer   data)
{
	GSettings *settings;

	settings = g_settings_new (FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_TOOLBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
	g_object_unref (settings);
}


void
activate_action_view_statusbar (GtkAction *action,
				gpointer   data)
{
	GSettings *settings;

	settings = g_settings_new (FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_STATUSBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
	g_object_unref (settings);
}


void
activate_action_view_folders (GtkAction *action,
			      gpointer   data)
{
	GSettings *settings;

	settings = g_settings_new (FILE_ROLLER_SCHEMA_UI);
	g_settings_set_boolean (settings, PREF_UI_VIEW_FOLDERS, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
	g_object_unref (settings);
}


void
activate_action_stop (GtkAction *action,
		      gpointer   data)
{
	FrWindow *window = data;
	fr_window_stop (window);
}


void
activate_action_reload (GtkAction *action,
			gpointer   data)
{
	FrWindow *window = data;

	fr_window_archive_reload (window);
}


void
activate_action_go_back (GtkAction *action,
			 gpointer   data)
{
	FrWindow *window = data;
	fr_window_go_back (window);
}


void
activate_action_go_forward (GtkAction *action,
			    gpointer   data)
{
	FrWindow *window = data;
	fr_window_go_forward (window);
}


void
activate_action_go_up (GtkAction *action,
		       gpointer   data)
{
	FrWindow *window = data;
	fr_window_go_up_one_level (window);
}


void
activate_action_go_home (GtkAction *action,
			 gpointer   data)
{
	FrWindow *window = data;
	fr_window_go_to_location (window, "/", FALSE);
}


void
activate_action_manual (GtkAction *action,
			gpointer   data)
{
	FrWindow *window = data;

	_gtk_show_help_dialog (GTK_WINDOW (window) , NULL);
}


void
activate_action_about (GtkAction *action,
		       gpointer   data)
{
	FrWindow   *window = data;
	const char *authors[] = { "Paolo Bacchilega <paolo.bacchilega@libero.it>", NULL	};
	const char *documenters [] = { "Alexander Kirillov", "Breda McColgan", NULL };

	gtk_show_about_dialog (GTK_WINDOW (window),
			       "version", VERSION,
			       "copyright", _("Copyright \xc2\xa9 2001–2010 Free Software Foundation, Inc."),
			       "comments", _("An archive manager for GNOME."),
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", _("translator-credits"),
			       "logo-icon-name", "file-roller",
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "wrap-license", TRUE,
			       NULL);
}
