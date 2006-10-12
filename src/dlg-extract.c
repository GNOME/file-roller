/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
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

#include <glade/glade.h>

#include "bookmarks.h"
#include "file-utils.h"
#include "fr-stock.h"
#include "main.h"
#include "gtk-utils.h"
#include "window.h"
#include "typedefs.h"
#include "gconf-utils.h"


#define GLADE_FILE "file-roller-extract.glade"


typedef struct {
	FRWindow     *window;
	GladeXML     *gui;

	GtkWidget    *dialog;

	GtkWidget    *e_main_vbox;
	GtkWidget    *e_destination_filechooserbutton;
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

	GtkTooltips  *tooltips;

	gboolean      extract_clicked;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	g_object_unref (G_OBJECT (data->gui));

	if (! data->extract_clicked) {
		window_pop_message (data->window);
		window_batch_mode_stop (data->window);
	}

	g_object_unref (data->tooltips);
	g_free (data);
}


static int
help_cb (GtkWidget   *w,
	 DialogData  *data)
{
	show_help_dialog (GTK_WINDOW (data->dialog), "file-roller-extract-options");
	return TRUE;
}


static int
extract_cb (GtkWidget   *w,
	    DialogData  *data)
{
	FRWindow   *window = data->window;
	gboolean    do_not_extract = FALSE;
	char       *extract_to_dir;
	gboolean    overwrite;
	gboolean    skip_newer;
	gboolean    selected_files;
	gboolean    pattern_files;
	gboolean    junk_paths;
	GList      *file_list;
	char       *password;
	const char *base_dir = NULL;

	data->extract_clicked = TRUE;

	/* collect extraction options. */

	extract_to_dir = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (data->e_destination_filechooserbutton));

	/* check directory existence. */

	if (! path_is_dir (extract_to_dir)) {
		if (! force_directory_creation) {
			GtkWidget *d;
			int        r;
			char      *folder_name;
			char      *msg;

			folder_name = g_filename_display_name (extract_to_dir);
			msg = g_strdup_printf (_("Destination folder \"%s\" does not exist.\n\nDo you want to create it?"), folder_name);
			g_free (folder_name);

			d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     msg,
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);
			
			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			g_free (msg);
			
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
			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);
			g_free (message);

			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			return FALSE;
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
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		if (data->window->batch_mode) 
			gtk_widget_destroy (data->dialog);

		return FALSE;
	}

	/* check extraction directory permissions. */

	if (path_is_dir (extract_to_dir) 
	    && access (extract_to_dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *d;
		char      *utf8_path;
		char      *message;
		
		utf8_path = g_filename_to_utf8 (extract_to_dir, -1, NULL, NULL, NULL);
		message = g_strdup_printf (_("You don't have the right permissions to extract archives in the folder \"%s\""), utf8_path);
		g_free (utf8_path);
		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Extraction not performed"),
					     message,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);
		g_free (message);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (extract_to_dir);

		return FALSE;
	}

	window_set_extract_default_dir (window, extract_to_dir);
	if (window->batch_mode)
		window->extract_interact_use_default_dir = TRUE;

	overwrite = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_overwrite_checkbutton));
	skip_newer = !gtk_toggle_button_get_inconsistent (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton)) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton));
	junk_paths = ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_recreate_dir_checkbutton));

	eel_gconf_set_boolean (PREF_EXTRACT_OVERWRITE, overwrite);
	if (!gtk_toggle_button_get_inconsistent (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton)))
		eel_gconf_set_boolean (PREF_EXTRACT_SKIP_NEWER, skip_newer);
	eel_gconf_set_boolean (PREF_EXTRACT_RECREATE_FOLDERS, !junk_paths);

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
			return FALSE;
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

	if (password != NULL) {
		g_free (window->password);
		window->password = g_strdup (password);
	}

	if (selected_files)
		base_dir = window_get_current_location (window);

	window_archive_extract (window,
				file_list,
				extract_to_dir,
				base_dir,
				skip_newer,
				overwrite,
				junk_paths,
				password);
	
	path_list_free (file_list);
	g_free (extract_to_dir);
	g_free (password);

	return TRUE;
}


static void
files_entry_changed_cb (GtkWidget  *widget, 
			DialogData *data)
{
	if (! GTK_TOGGLE_BUTTON (data->e_files_radiobutton)->active)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_files_radiobutton), TRUE);
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
set_bold_label (GtkWidget  *label,
		const char *label_txt)
{
	char *bold_label;

	bold_label = g_strconcat ("<b>", label_txt, "</b>", NULL);
	gtk_label_set_markup (GTK_LABEL (label), bold_label);
	g_free (bold_label);
}


static GtkWidget *
create_extra_widget (DialogData *data)
{
	GtkWidget *vbox1;
	GtkWidget *hbox28;
	GtkWidget *vbox19;
	GtkWidget *e_files_label;
	GtkWidget *hbox29;
	GtkWidget *label47;
	GtkWidget *table1;
	GSList    *e_files_radiobutton_group = NULL;
	GtkWidget *vbox20;
	GtkWidget *e_actions_label;
	GtkWidget *hbox30;
	GtkWidget *label48;
	GtkWidget *vbox15;
	GtkWidget *label31;
	
	vbox1 = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox1), 0);

	hbox28 = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox28, TRUE, TRUE, 0);

	vbox19 = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox28), vbox19, TRUE, TRUE, 0);

	e_files_label = gtk_label_new ("");
	set_bold_label (e_files_label, _("Files"));
	gtk_box_pack_start (GTK_BOX (vbox19), e_files_label, FALSE, FALSE, 0);
	gtk_label_set_justify (GTK_LABEL (e_files_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (e_files_label), 0, 0.5);

	hbox29 = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox19), hbox29, TRUE, TRUE, 0);

	label47 = gtk_label_new ("    ");
	gtk_box_pack_start (GTK_BOX (hbox29), label47, FALSE, FALSE, 0);
	gtk_label_set_justify (GTK_LABEL (label47), GTK_JUSTIFY_LEFT);

	table1 = gtk_table_new (3, 2, FALSE);
	gtk_box_pack_start (GTK_BOX (hbox29), table1, TRUE, TRUE, 0);
	gtk_table_set_row_spacings (GTK_TABLE (table1), 6);
	gtk_table_set_col_spacings (GTK_TABLE (table1), 6);

	data->e_files_radiobutton = gtk_radio_button_new_with_mnemonic (NULL, _("_Files:"));
	gtk_table_attach (GTK_TABLE (table1), data->e_files_radiobutton, 0, 1, 2, 3,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_radio_button_set_group (GTK_RADIO_BUTTON (data->e_files_radiobutton), e_files_radiobutton_group);
	e_files_radiobutton_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (data->e_files_radiobutton));

	data->e_files_entry = gtk_entry_new ();
	gtk_table_attach (GTK_TABLE (table1), data->e_files_entry, 1, 2, 2, 3,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_tooltips_set_tip (data->tooltips, data->e_files_entry, _("example: *.txt; *.doc"), NULL);
	gtk_entry_set_activates_default (GTK_ENTRY (data->e_files_entry), TRUE);
	
	data->e_all_radiobutton = gtk_radio_button_new_with_mnemonic (NULL, _("_All files"));
	gtk_table_attach (GTK_TABLE (table1), data->e_all_radiobutton, 0, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_radio_button_set_group (GTK_RADIO_BUTTON (data->e_all_radiobutton), e_files_radiobutton_group);
	e_files_radiobutton_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (data->e_all_radiobutton));
	
	data->e_selected_radiobutton = gtk_radio_button_new_with_mnemonic (NULL, _("_Selected files"));
	gtk_table_attach (GTK_TABLE (table1), data->e_selected_radiobutton, 0, 2, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_radio_button_set_group (GTK_RADIO_BUTTON (data->e_selected_radiobutton), e_files_radiobutton_group);
	e_files_radiobutton_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (data->e_selected_radiobutton));
	
	vbox20 = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox28), vbox20, TRUE, TRUE, 0);

	e_actions_label = gtk_label_new ("");
	set_bold_label (e_actions_label, _("Actions"));
	gtk_box_pack_start (GTK_BOX (vbox20), e_actions_label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (e_actions_label), TRUE);
	gtk_label_set_justify (GTK_LABEL (e_actions_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (e_actions_label), 0, 0.5);

	hbox30 = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox20), hbox30, TRUE, TRUE, 0);

	label48 = gtk_label_new ("    ");
	gtk_box_pack_start (GTK_BOX (hbox30), label48, FALSE, FALSE, 0);
	gtk_label_set_justify (GTK_LABEL (label48), GTK_JUSTIFY_LEFT);

	vbox15 = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox30), vbox15, TRUE, TRUE, 0);
	
	data->e_recreate_dir_checkbutton = gtk_check_button_new_with_mnemonic (_("R_e-create folders"));
	gtk_box_pack_start (GTK_BOX (vbox15), data->e_recreate_dir_checkbutton, FALSE, FALSE, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_recreate_dir_checkbutton), TRUE);

	data->e_overwrite_checkbutton = gtk_check_button_new_with_mnemonic (_("Over_write existing files"));
	gtk_box_pack_start (GTK_BOX (vbox15), data->e_overwrite_checkbutton, FALSE, FALSE, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_overwrite_checkbutton), TRUE);
			
	data->e_not_newer_checkbutton = gtk_check_button_new_with_mnemonic (_("Do not e_xtract older files"));
	gtk_box_pack_start (GTK_BOX (vbox15), data->e_not_newer_checkbutton, FALSE, FALSE, 0);
	
	data->e_password_hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox15), data->e_password_hbox, TRUE, TRUE, 0);
	
	label31 = gtk_label_new_with_mnemonic (_("_Password:"));
	gtk_box_pack_start (GTK_BOX (data->e_password_hbox), label31, FALSE, FALSE, 0);
	gtk_label_set_justify (GTK_LABEL (label31), GTK_JUSTIFY_LEFT);
	
	data->e_password_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (data->e_password_hbox), data->e_password_entry, TRUE, TRUE, 0);
	gtk_entry_set_activates_default (GTK_ENTRY (data->e_password_entry), TRUE);
	gtk_entry_set_visibility (GTK_ENTRY (data->e_password_entry), FALSE);
	
	data->e_view_folder_checkbutton = gtk_check_button_new_with_mnemonic (_("_Open destination folder after extraction"));
	gtk_box_pack_start (GTK_BOX (vbox1), data->e_view_folder_checkbutton, FALSE, FALSE, 0);
	
	gtk_label_set_mnemonic_widget (GTK_LABEL (label31), data->e_password_entry);

	gtk_widget_show_all (vbox1);

	return vbox1;
}


void
dlg_extract (GtkWidget *widget,
	     gpointer   callback_data)
{
	FRWindow          *window = callback_data;
        DialogData        *data;
	GtkWidget         *e_okbutton, *e_cancelbutton, *e_helpbutton;

        data = g_new (DialogData, 1);

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
        if (! data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
	}

        data->window = window;
	data->extract_clicked = FALSE;

	data->tooltips = gtk_tooltips_new ();
	g_object_ref (G_OBJECT (data->tooltips));
	gtk_object_sink (GTK_OBJECT (data->tooltips));

        /* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "extract_dialog");
	data->e_main_vbox = glade_xml_get_widget (data->gui, "e_main_vbox");
	data->e_destination_filechooserbutton = glade_xml_get_widget (data->gui, "e_destination_filechooserbutton");
	e_okbutton = glade_xml_get_widget (data->gui, "e_okbutton");
	e_cancelbutton = glade_xml_get_widget (data->gui, "e_cancelbutton");
	e_helpbutton = glade_xml_get_widget (data->gui, "e_helpbutton");

	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
	gtk_button_set_use_stock (GTK_BUTTON (e_okbutton), TRUE);
	gtk_button_set_label (GTK_BUTTON (e_okbutton), FR_STOCK_EXTRACT);

	gtk_box_pack_start (GTK_BOX (data->e_main_vbox), create_extra_widget (data), TRUE, TRUE, 0);

	/* Set widgets data. */

	if (window->extract_default_dir != NULL) {
		const char *folder = window->extract_default_dir;
		if (dir_is_temp_dir (folder))
			folder = g_get_home_dir ();
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (data->e_destination_filechooserbutton), folder);
	}
		
	if (_gtk_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view))) > 0)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_selected_radiobutton), TRUE);
	else {
		gtk_widget_set_sensitive (data->e_selected_radiobutton, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_all_radiobutton), TRUE);
	}

	if (window->archive->command->propPassword) {
		gtk_widget_set_sensitive (data->e_password_hbox, TRUE);
		if (window->password != NULL)
			_gtk_entry_set_locale_text (GTK_ENTRY (data->e_password_entry), window->password);
	} else 
		gtk_widget_set_sensitive (data->e_password_hbox, FALSE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_view_folder_checkbutton), eel_gconf_get_boolean (PREF_EXTRACT_VIEW_FOLDER, FALSE));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_overwrite_checkbutton), eel_gconf_get_boolean (PREF_EXTRACT_OVERWRITE, FALSE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton), eel_gconf_get_boolean (PREF_EXTRACT_SKIP_NEWER, FALSE));
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->e_overwrite_checkbutton))) {
		gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (data->e_not_newer_checkbutton), TRUE);
		gtk_widget_set_sensitive (data->e_not_newer_checkbutton, FALSE);
	}
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->e_recreate_dir_checkbutton), eel_gconf_get_boolean (PREF_EXTRACT_RECREATE_FOLDERS, TRUE));

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (e_okbutton), 
			  "clicked",
			  G_CALLBACK (extract_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (e_cancelbutton), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (e_helpbutton), 
			  "clicked",
			  G_CALLBACK (help_cb),
			  data);

	g_signal_connect (G_OBJECT (data->e_overwrite_checkbutton), 
			  "toggled",
			  G_CALLBACK (overwrite_toggled_cb),
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
