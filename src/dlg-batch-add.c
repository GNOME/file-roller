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

#include "file-utils.h"
#include "window.h"
#include "typedefs.h"
#include "gtk-utils.h"


#define GLADE_FILE "file_roller.glade"


typedef struct {
	FRWindow  *window;
	GladeXML  *gui;

	GtkWidget *dialog;
	GtkWidget *a_add_label;
	GtkWidget *a_add_to_fileentry;
	GtkWidget *a_add_to_entry;
	GtkWidget *a_only_newer_checkbutton;

	GList     *file_list;
	gboolean   add_clicked;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	if (! data->add_clicked) {
		window_pop_message (data->window);
		window_batch_mode_stop (data->window);
	}

	g_object_unref (data->gui);
        g_free (data);
}


/* called when the "add" button is pressed. */
static void
add_clicked_cb (GtkWidget  *widget, 
		DialogData *data)
{
	FRWindow *window = data->window; 
	char     *archive_name_utf8;
	char     *archive_name;
	char     *archive_dir;
	gboolean  do_not_add = FALSE;

	data->add_clicked = TRUE;

	/* Collect data */

	archive_name_utf8 = gnome_file_entry_get_full_path (GNOME_FILE_ENTRY (data->a_add_to_fileentry), FALSE);
	archive_name = g_locale_from_utf8 (archive_name_utf8, -1, NULL, NULL, NULL);
	g_free (archive_name_utf8);

	window->update_dropped_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->a_only_newer_checkbutton));

	/* check whether the user entered an archive name. */

	if (! fr_archive_utils__file_is_archive (archive_name)) {
		GtkWidget  *d;

		d = gtk_message_dialog_new (GTK_WINDOW (window->app),
					    GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_CLOSE,
					    _("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		return;
	}

	/* check directory existence. */

	archive_dir = remove_level_from_path (archive_name);
	if (! path_is_dir (archive_dir)) {
		GtkWidget *d;
		int        r;
		
		d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("Destination folder does not exist.  Do you want to create it?"),
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Create _Folder"), GTK_RESPONSE_YES,
					     NULL);
			
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);

	}

	if (! do_not_add && ! ensure_dir_exists (archive_dir, 0755)) {
		GtkWidget  *d;
		const char *error;

		error = gnome_vfs_result_to_string (gnome_vfs_result_from_errno ());
		
		d = gtk_message_dialog_new (GTK_WINDOW (window->app),
					    GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_CLOSE,
					    _("Could not create the destination folder: %s.\nAddition not performed."), error);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_dir);
		g_free (archive_name);

		return;
	}

	g_free (archive_dir);

	if (do_not_add) {
		GtkWidget *d;

		d = gtk_message_dialog_new (GTK_WINDOW (window->app),
					    GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_CLOSE,
					    _("Addition not performed."));
		gtk_dialog_run (GTK_DIALOG (d));
		g_free (archive_name);
		gtk_widget_destroy (GTK_WIDGET (d));

		return;
	}

	if (! path_is_file (archive_name)) {
		if (window->dropped_file_list != NULL)
			path_list_free (window->dropped_file_list);
		window->dropped_file_list = path_list_dup (data->file_list);
		window->add_after_creation = TRUE;
		window_archive_new (window, archive_name);

	} else {
		window->add_after_opening = TRUE;

		window_batch_mode_add_next_action (window,
						   FR_BATCH_ACTION_ADD,
						   path_list_dup (data->file_list),
						   (GFreeFunc) path_list_free);
		window_archive_open (window, archive_name);
	}

	g_free (archive_name);
	gtk_widget_destroy (data->dialog);
}


void
dlg_batch_add_files (FRWindow *window,
		     GList    *file_list)
{
        DialogData *data;
	GtkWidget  *cancel_button;
	GtkWidget  *add_button;
	GList      *scan;
	int         n_files = 0, n_folders = 0;
	char       *label_files = NULL, *label_folders = NULL, *label;
	char       *path;
	char       *markup;

        data = g_new (DialogData, 1);

        data->window = window;
	data->file_list = file_list;
	data->add_clicked = FALSE;

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "batch_add_files_dialog");
	data->a_add_label = glade_xml_get_widget (data->gui, "a_add_label");
	data->a_add_to_fileentry = glade_xml_get_widget (data->gui, "a_add_to_fileentry");
	data->a_add_to_entry = glade_xml_get_widget (data->gui, "a_add_to_entry");
	data->a_only_newer_checkbutton = glade_xml_get_widget (data->gui, "a_only_newer_checkbutton");

	add_button = glade_xml_get_widget (data->gui, "a_add_button");
	cancel_button = glade_xml_get_widget (data->gui, "a_cancel_button");

	/* Set widgets data. */

	for (scan = file_list; scan; scan = scan->next) {
		char *path = scan->data;
		if (path_is_dir (path))
			n_folders++;
		else if (path_is_file (path))
			n_files++;
	}

	if (n_files > 0) 
		label_files = g_strdup_printf (_("Files to add: %d"), n_files);
	if (n_folders > 0)
		label_folders = g_strdup_printf (_("Folders to add: %d"), n_folders);
	
	label = g_strconcat ((label_folders ? label_folders : label_files),
			     ((label_folders && label_files) ? "\n" : ""),
			     ((label_folders && label_files) ? label_files : NULL),
			     NULL);
	g_free (label_files);
	g_free (label_folders);

	markup = g_strdup_printf ("<b>%s</b>", label);
	g_free (label);
	gtk_label_set_markup (GTK_LABEL (data->a_add_label), markup);
	g_free (markup);

	path = g_strconcat (window->add_default_dir, "/", NULL);
	_gtk_entry_set_locale_text (GTK_ENTRY (data->a_add_to_entry), path);
	g_free (path);

	/* Set the signals handlers. */

        g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (cancel_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (add_button), 
			  "clicked",
			  G_CALLBACK (add_clicked_cb),
			  data);

	/* Run dialog. */

	gtk_widget_grab_focus (data->a_add_to_entry);
	gtk_editable_set_position  (GTK_EDITABLE (data->a_add_to_entry), 
				    strlen (gtk_entry_get_text (GTK_ENTRY (data->a_add_to_entry))));

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
	gtk_window_set_modal (GTK_WINDOW (data->dialog), FALSE); 

	gtk_widget_show (data->dialog);
}
