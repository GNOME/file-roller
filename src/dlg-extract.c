/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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

#include <config.h>
#include <string.h>

#include <gnome.h>
#include <glade/glade.h>

#include "bookmarks.h"
#include "file-utils.h"
#include "main.h"
#include "gtk-utils.h"
#include "window.h"
#include "typedefs.h"
#include "gconf-utils.h"


#define GLADE_FILE "file_roller.glade"


typedef struct {
	FRWindow     *window;
	GladeXML     *gui;

	GtkWidget    *dialog;
	GtkWidget    *e_extract_to_fileentry;
	GtkWidget    *e_extract_to_entry;

	GtkWidget    *e_fav_tree_view;
	GtkWidget    *e_add_fav_button;
	GtkWidget    *e_remove_fav_button;

	GtkWidget    *e_all_radiobutton;
	GtkWidget    *e_selected_radiobutton;
	GtkWidget    *e_files_radiobutton;
	GtkWidget    *e_files_entry;
	GtkWidget    *e_recreate_dir_checkbutton;
	GtkWidget    *e_overwrite_checkbutton;
	GtkWidget    *e_not_newer_checkbutton;
	GtkWidget    *e_password_entry;
	GtkWidget    *e_password_hbox;

	GtkWidget    *e_view_folder_checkbutton;

	gboolean      extract_clicked;
	GtkTreeModel *fav_model;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	if (! data->extract_clicked) {
		window_pop_message (data->window);
		window_batch_mode_stop (data->window);
	}

        g_object_unref (data->gui);
        g_free (data);
}


/* called when the "ok" button is pressed. */
static void
ok_clicked_cb (GtkWidget  *widget, 
	       DialogData *data)
{
	char       *extract_to_dir_utf8;
	char       *extract_to_dir;
	gboolean    do_not_extract = FALSE;
	gboolean    overwrite;
	gboolean    skip_newer;
	gboolean    selected_files;
	gboolean    pattern_files;
	gboolean    junk_paths;
	GList      *file_list;
	FRWindow   *window = data->window;
	char       *password;

	data->extract_clicked = TRUE;

	/* collect extraction options. */

	extract_to_dir_utf8 = gnome_file_entry_get_full_path (GNOME_FILE_ENTRY (data->e_extract_to_fileentry), FALSE);
	extract_to_dir = g_locale_from_utf8 (extract_to_dir_utf8, -1, 0, 0, 0);
	g_free (extract_to_dir_utf8);

	/* check directory existence. */

	if (! path_is_dir (extract_to_dir)) {
		if (! force_directory_creation) {
			GtkWidget *d;
			int        r;
		
			d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     _("Destination folder does not exist.  Do you want to create it?"),
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);
			
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			if (r != GTK_RESPONSE_YES) 
				do_not_extract = TRUE;
		}

		if (! do_not_extract && ! ensure_dir_exists (extract_to_dir, 0755)) {
			GtkWidget  *d;
			const char *error;
			char       *message;

			error = gnome_vfs_result_to_string (gnome_vfs_result_from_errno ());
			message = g_strdup_printf (_("Could not create the destination folder: %s."), error);
			d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						     GTK_DIALOG_DESTROY_WITH_PARENT,
						     GTK_STOCK_DIALOG_ERROR,
						     _("Extraction not performed"),
						     message,
						     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						     NULL);
			g_free (message);

			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			return;
		}
	} 
	
	if (do_not_extract) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Extraction not performed"),
					     NULL,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		return;
	}

	/* check extraction directory permissions. */

	if (path_is_dir (extract_to_dir) 
	    && access (extract_to_dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *d;
		char      *utf8_path;
		char      *message;
		
		utf8_path = g_locale_to_utf8 (extract_to_dir, -1, NULL, NULL, NULL);
		message = g_strdup_printf (_("You don't have the right permissions to extract archives in the folder \"%s\""), utf8_path);
		g_free (utf8_path);
		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Extraction not performed"),
					     message,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		g_free (message);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (extract_to_dir);

		return;
	}

	window_set_extract_default_dir (window, extract_to_dir);
	if (window->batch_mode)
		window->extract_interact_use_default_dir = TRUE;

	overwrite = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_overwrite_checkbutton));
	skip_newer = !gtk_toggle_button_get_inconsistent (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton)) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton));
	junk_paths = ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_recreate_dir_checkbutton));

	selected_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_selected_radiobutton));
	pattern_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_files_radiobutton));

	if (GTK_WIDGET_SENSITIVE (data->e_password_entry)) {
		password = _gtk_entry_get_locale_text (GTK_ENTRY (data->e_password_entry));
		if ((password != NULL) && (password[0] == 0)) {
			g_free (password);
			password = NULL;
		}
	} else
		password = NULL;

	eel_gconf_set_boolean (PREF_EXTRACT_VIEW_FOLDER, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_view_folder_checkbutton)));

	/* create the file list. */

	file_list = NULL;
	if (selected_files) 
		file_list = window_get_file_list_selection (window, TRUE, NULL);
	else if (pattern_files) {
		const char *pattern;
		pattern = gtk_entry_get_text (GTK_ENTRY (data->e_files_entry));
		file_list = window_get_file_list_pattern (window, pattern);
		if (file_list == NULL) {
			gtk_widget_destroy (data->dialog);
			g_free (extract_to_dir);
			g_free (password);
			return;
		}
	}

	/* close the dialog. */

	gtk_widget_destroy (data->dialog);

	/* extract ! */

	if (eel_gconf_get_boolean (PREF_EXTRACT_VIEW_FOLDER, FALSE)) {
		window->view_folder_after_extraction = TRUE;
		g_free (window->folder_to_view);
		window->folder_to_view = g_strdup (extract_to_dir);
	}

	window_archive_extract (window,
				file_list,
				extract_to_dir,
				skip_newer,
				overwrite,
				junk_paths,
				password);
	
	path_list_free (file_list);
	g_free (extract_to_dir);
	g_free (password);
}


static void
files_entry_changed_cb (GtkWidget  *widget, 
			DialogData *data)
{
	if (! GTK_TOGGLE_BUTTON (data->e_files_radiobutton)->active)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_files_radiobutton), TRUE);
}


static void
path_entry_changed (GtkWidget  *widget, 
		    DialogData *data)
{
	const char *path;
	gboolean    can_add;

	path = gtk_entry_get_text (GTK_ENTRY (data->e_extract_to_entry));
	can_add = g_utf8_strlen (path, -1) > 0;
	gtk_widget_set_sensitive (data->e_add_fav_button, can_add);
}


static void
overwrite_toggled_cb (GtkToggleButton *button,
		      DialogData      *data)
{
	gboolean active = gtk_toggle_button_get_active (button);
	gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton), !active);
	gtk_widget_set_sensitive (data->e_not_newer_checkbutton, active);
}


static void
update_bookmark_list (DialogData *data)
{
	Bookmarks   *bookmarks;
	GtkTreeIter  iter;
	GList       *scan;

	if (GTK_WIDGET_REALIZED (data->e_fav_tree_view)) 
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (data->e_fav_tree_view), 0, 0);

	gtk_list_store_clear (GTK_LIST_STORE (data->fav_model));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->fav_model), 0, GTK_SORT_ASCENDING);

	bookmarks = bookmarks_new (RC_BOOKMARKS_FILE);
	bookmarks_load_from_disk (bookmarks);

	for (scan = bookmarks->list; scan; scan = scan->next) {
		char *utf8_name;

		gtk_list_store_append (GTK_LIST_STORE (data->fav_model),
				       &iter);

		utf8_name = g_locale_to_utf8 (scan->data, -1, 0, 0, 0);
		gtk_list_store_set (GTK_LIST_STORE (data->fav_model), &iter,
				    0, utf8_name,
				    -1);
		g_free (utf8_name);
	}

	bookmarks_free (bookmarks);
}


/* called when the "add to favorites" button is pressed. */
static void
add_fav_cb (GtkWidget  *widget, 
	    DialogData *data)
{
	Bookmarks  *bookmarks;
	char       *path;

	path = _gtk_entry_get_locale_text (GTK_ENTRY (data->e_extract_to_entry));
	if (path == NULL) 
		return;

	if ((path[strlen (path) - 1] == '/') && (strcmp (path, "/") != 0))
		path[strlen (path) - 1] = 0;

	bookmarks = bookmarks_new (RC_BOOKMARKS_FILE);
	bookmarks_load_from_disk (bookmarks);
	bookmarks_add (bookmarks, path);
	bookmarks_write_to_disk (bookmarks);
	bookmarks_free (bookmarks);

	g_free (path);

	update_bookmark_list (data);
}


/* called when the "remove favorite" button is pressed. */
static void
remove_fav_cb (GtkWidget  *widget, 
	       DialogData *data)
{
	GtkTreeIter  iter;
	Bookmarks   *bookmarks;
	gchar       *path;

	bookmarks = bookmarks_new (RC_BOOKMARKS_FILE);
	bookmarks_load_from_disk (bookmarks);

	if (! gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->e_fav_tree_view)), NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (data->fav_model), &iter,
                            0, &path,
                            -1);
	bookmarks_remove (bookmarks, path);
	g_free (path);

	bookmarks_write_to_disk (bookmarks);
	bookmarks_free (bookmarks);
	update_bookmark_list (data);
}


static gboolean
fav_item_activated_cb (GtkTreeView       *tree_view, 
		       GtkTreePath       *path, 
		       GtkTreeViewColumn *column, 
		       gpointer           callback_data)

{
        DialogData  *data = callback_data;
	GtkTreeIter  iter;
	gchar       *dir;

	if (! gtk_tree_model_get_iter (data->fav_model, &iter, path)) 
		return FALSE;

	gtk_tree_model_get (data->fav_model, &iter,
			    0, &dir,
			    -1);
	gtk_entry_set_text (GTK_ENTRY (data->e_extract_to_entry), dir);
	g_free (dir);

	return FALSE;
}


/* called when the "help" button is clicked. */
static void
help_clicked_cb (GtkWidget  *widget, 
		 DialogData *data)
{
	GError *err;

	err = NULL;  
	gnome_help_display ("file-roller", "fr-extracting", &err);
	
	if (err != NULL) {
		GtkWidget *dialog;
		
		dialog = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						  GTK_DIALOG_DESTROY_WITH_PARENT, 
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not display help"),
						  err->message,
						  GTK_STOCK_OK, GTK_RESPONSE_OK,
						  NULL);
		
		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);
		
		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
		
		gtk_widget_show (dialog);
		
		g_error_free (err);
	}
}


static void
set_bold_label (GladeXML   *gui,
		const char *widget_name,
		const char *label_txt)
{
	GtkWidget *label;
	char      *bold_label;

	label = glade_xml_get_widget (gui, widget_name);
	bold_label = g_strconcat ("<b>", label_txt, "</b>", NULL);
	gtk_label_set_markup (GTK_LABEL (label), bold_label);
	g_free (bold_label);
}


void
dlg_extract (GtkWidget *widget,
	     gpointer   callback_data)
{
        DialogData        *data;
	FRWindow          *window = callback_data;
	GtkWidget         *cancel_button;
	GtkWidget         *ok_button;
	GtkWidget         *help_button;
	gchar             *path;
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;

        data = g_new (DialogData, 1);

        data->window = window;
	data->extract_clicked = FALSE;

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (! data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "extract_dialog");
        data->e_extract_to_fileentry = glade_xml_get_widget (data->gui, "e_extract_to_fileentry");
        data->e_extract_to_entry = glade_xml_get_widget (data->gui, "e_extract_to_entry");

	data->e_fav_tree_view = glade_xml_get_widget (data->gui, "e_fav_tree_view");
	data->e_add_fav_button = glade_xml_get_widget (data->gui, "e_add_fav_button");
	data->e_remove_fav_button = glade_xml_get_widget (data->gui, "e_remove_fav_button");

        data->e_all_radiobutton = glade_xml_get_widget (data->gui, "e_all_radiobutton");
        data->e_selected_radiobutton = glade_xml_get_widget (data->gui, "e_selected_radiobutton");
        data->e_files_radiobutton = glade_xml_get_widget (data->gui, "e_files_radiobutton");
        data->e_files_entry = glade_xml_get_widget (data->gui, "e_files_entry");
        data->e_overwrite_checkbutton = glade_xml_get_widget (data->gui, "e_overwrite_checkbutton");
        data->e_not_newer_checkbutton = glade_xml_get_widget (data->gui, "e_not_newer_checkbutton");
        data->e_recreate_dir_checkbutton = glade_xml_get_widget (data->gui, "e_recreate_dir_checkbutton");
        data->e_password_entry = glade_xml_get_widget (data->gui, "e_password_entry");
        data->e_password_hbox = glade_xml_get_widget (data->gui, "e_password_hbox");

        data->e_view_folder_checkbutton = glade_xml_get_widget (data->gui, "e_view_folder_checkbutton");

	set_bold_label (data->gui, "e_actions_label", _("Actions"));
	set_bold_label (data->gui, "e_destination_folder_label", _("Destination folder"));
	set_bold_label (data->gui, "e_files_label", _("Files"));

	ok_button = glade_xml_get_widget (data->gui, "e_ok_button");
	cancel_button = glade_xml_get_widget (data->gui, "e_cancel_button");
	help_button = glade_xml_get_widget (data->gui, "e_help_button");

	/* Set widgets data. */

	if (window->extract_default_dir[strlen (window->extract_default_dir) - 1] != '/')
		path = g_strconcat (window->extract_default_dir, "/", NULL);
	else
		path = g_strdup (window->extract_default_dir);
	_gtk_entry_set_locale_text (GTK_ENTRY (data->e_extract_to_entry), path);
	g_free (path);
	
	if (_gtk_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view))) > 0)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_selected_radiobutton), TRUE);
	else {
		gtk_widget_set_sensitive (data->e_selected_radiobutton, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_all_radiobutton), TRUE);
	}

	data->fav_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_STRING));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->fav_model), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model (GTK_TREE_VIEW (data->e_fav_tree_view),
				 data->fav_model);
	g_object_unref (G_OBJECT (data->fav_model));

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Bookmarks"),
							   renderer,
							   "text", 0,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW (data->e_fav_tree_view),
				     column);

	update_bookmark_list (data);

	if (window->archive->command->propPassword) {
		gtk_widget_set_sensitive (data->e_password_hbox, TRUE);
		if (window->password != NULL)
			_gtk_entry_set_locale_text (GTK_ENTRY (data->e_password_entry), window->password);
	} else 
		gtk_widget_set_sensitive (data->e_password_hbox, FALSE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_view_folder_checkbutton), eel_gconf_get_boolean (PREF_EXTRACT_VIEW_FOLDER, FALSE));

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (cancel_button), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (ok_button), 
			  "clicked",
			  G_CALLBACK (ok_clicked_cb),
			  data);
	g_signal_connect (G_OBJECT (help_button), 
			  "clicked",
			  G_CALLBACK (help_clicked_cb),
			  data);
	g_signal_connect (G_OBJECT (data->e_add_fav_button), 
			  "clicked",
			  G_CALLBACK (add_fav_cb),
			  data);
	g_signal_connect (G_OBJECT (data->e_remove_fav_button), 
			  "clicked",
			  G_CALLBACK (remove_fav_cb),
			  data);
	g_signal_connect (G_OBJECT (data->e_extract_to_entry), 
			  "changed",
			  G_CALLBACK (path_entry_changed),
			  data);
	g_signal_connect (G_OBJECT (data->e_overwrite_checkbutton), 
			  "toggled",
			  G_CALLBACK (overwrite_toggled_cb),
			  data);
	g_signal_connect (G_OBJECT (data->e_fav_tree_view), 
			  "row_activated",
			  G_CALLBACK (fav_item_activated_cb), 
			  data);
	g_signal_connect (G_OBJECT (data->e_files_entry), 
			  "changed",
			  G_CALLBACK (files_entry_changed_cb),
			  data);

	

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
        gtk_window_set_modal         (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
