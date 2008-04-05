/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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
#include <unistd.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "file-utils.h"
#include "fr-stock.h"
#include "fr-window.h"
#include "gtk-utils.h"


typedef struct {
	FrWindow  *window;
	GtkWidget *dialog;
	GtkWidget *add_if_newer_checkbutton;
} DialogData;


static void
open_file_destroy_cb (GtkWidget  *file_sel,
		      DialogData *data)
{
	g_free (data);
}


static int
file_sel_response_cb (GtkWidget      *widget,
		      int             response,
		      DialogData     *data)
{
	GtkFileChooser *file_sel = GTK_FILE_CHOOSER (widget);
	FrWindow       *window = data->window;
	char           *current_folder;
	gboolean        update;
	GSList         *selections, *iter;
	GList          *item_list = NULL;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (data->dialog);
		return TRUE;
	}

	if (response == GTK_RESPONSE_HELP) {
		show_help_dialog (GTK_WINDOW (data->dialog), "file-roller-add-options");
		return TRUE;
	}

	current_folder = gtk_file_chooser_get_current_folder_uri (file_sel);

	/* check folder permissions. */

	if (uri_is_dir (current_folder)
	    && ! check_permissions (current_folder, R_OK | X_OK)) {
		GtkWidget *d;
		char      *utf8_path;

		utf8_path = g_filename_display_name (current_folder);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not add the files to the archive"),
					   _("You don't have the right permissions to read files from folder \"%s\""), 
					   utf8_path);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (utf8_path);

	g_free (current_folder);

		return FALSE;
	}

	fr_window_set_add_default_dir (window, current_folder);

	update = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->add_if_newer_checkbutton));

	/**/

	selections = gtk_file_chooser_get_uris (file_sel);
	for (iter = selections; iter != NULL; iter = iter->next) {
		char *path = iter->data;
		item_list = g_list_prepend (item_list, path);
	}

	if (item_list != NULL)
		fr_window_archive_add_files (window, item_list, update);

	g_list_free (item_list);
	g_slist_foreach (selections, (GFunc) g_free, NULL);
	g_slist_free (selections);
	g_free (current_folder);

	gtk_widget_destroy (data->dialog);

	return TRUE;
}


static void
selection_changed_cb (GtkWidget  *file_sel,
 		      DialogData *data)
{
	FrWindow *window = data->window;
	char     *current_folder;

	current_folder = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (file_sel));

	/* check folder permissions. */

	if (uri_is_dir (current_folder)
	    && ! check_permissions (current_folder, R_OK | X_OK)) {
		GtkWidget *d;
		char      *utf8_path;

		utf8_path = g_filename_display_name (current_folder);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not add the files to the archive"),
					   _("You don't have the right permissions to read files from folder \"%s\""), 
					   utf8_path);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (utf8_path);
		g_free (current_folder);
	}
}


/* create the "add" dialog. */
void
add_files_cb (GtkWidget *widget,
	      void      *callback_data)
{
	GtkWidget  *file_sel;
	DialogData *data;
	GtkWidget  *main_box;

	data = g_new0 (DialogData, 1);
	data->window = callback_data;
	data->dialog = file_sel =
		gtk_file_chooser_dialog_new (_("Add Files"),
					     GTK_WINDOW (data->window),
					     GTK_FILE_CHOOSER_ACTION_OPEN,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     FR_STOCK_ADD_FILES, GTK_RESPONSE_OK,
					     GTK_STOCK_HELP, GTK_RESPONSE_HELP,
					     NULL);

	gtk_window_set_default_size (GTK_WINDOW (data->dialog), 530, 450);

	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (file_sel), TRUE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_sel), FALSE);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);

	data->add_if_newer_checkbutton = gtk_check_button_new_with_mnemonic (_("_Add only if newer"));

	main_box = gtk_hbox_new (FALSE, 20);
	gtk_container_set_border_width (GTK_CONTAINER (main_box), 0);
	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (file_sel), main_box);

	gtk_box_pack_start (GTK_BOX (main_box), data->add_if_newer_checkbutton,
			    TRUE, TRUE, 0);

	gtk_widget_show_all (main_box);

	/* set data */

	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (file_sel), fr_window_get_add_default_dir (data->window));

	/* signals */

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (open_file_destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (file_sel),
			  "response",
			  G_CALLBACK (file_sel_response_cb),
			  data);

	g_signal_connect (G_OBJECT (file_sel),
			  "selection-changed",
			  G_CALLBACK (selection_changed_cb),
			  data);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show (file_sel);
}
