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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include "file-utils.h"
#include "fr-init.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "fr-window.h"
#include "typedefs.h"
#include "dlg-extract.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (data->builder, (x)))


typedef struct {
	FrWindow     *window;
	GSettings    *settings;
	GList        *selected_files;
	char         *base_dir_for_selection;
	GtkWidget    *dialog;
	GtkBuilder   *builder;
	gboolean      extract_clicked;
} DialogData;


/* called when the main dialog is closed. */
static void
file_selector_destroy_cb (GtkWidget  *widget,
			  DialogData *data)
{
	if (! data->extract_clicked)
		fr_window_batch_stop (data->window);

	g_object_unref (data->builder);
	_g_string_list_free (data->selected_files);
	g_free (data->base_dir_for_selection);
	g_object_unref (data->settings);
	g_free (data);
}


static int
extract_cb (GtkWidget   *w,
	    DialogData  *data)
{
	FrWindow   *window = data->window;
	gboolean    do_not_extract = FALSE;
	GFile      *destination;
	gboolean    skip_newer;
	gboolean    selected_files;
	gboolean    pattern_files;
	gboolean    junk_paths;
	GList      *file_list;
	char       *base_dir = NULL;
	GError     *error = NULL;

	data->extract_clicked = TRUE;

	/* collect extraction options. */

	destination = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->dialog));

	/* check directory existence. */

	if (! _g_file_query_is_dir (destination)) {
		if (! ForceDirectoryCreation) {
			GtkWidget *d;
			int        r;
			char      *folder_name;
			char      *msg;

			folder_name = _g_file_get_display_basename (destination);
			msg = g_strdup_printf (_("Destination folder “%s” does not exist.\n\nDo you want to create it?"), folder_name);
			g_free (folder_name);

			d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						     GTK_DIALOG_MODAL,
						     msg,
						     NULL,
						     _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			g_free (msg);

			if (r != GTK_RESPONSE_YES)
				do_not_extract = TRUE;
		}

		if (! do_not_extract && ! _g_file_make_directory_tree (destination, 0755, &error)) {
			GtkWidget  *d;

			d = _gtk_error_dialog_new (GTK_WINDOW (window),
						   GTK_DIALOG_DESTROY_WITH_PARENT,
						   NULL,
						   _("Extraction not performed"),
						   _("Could not create the destination folder: %s."),
						   error->message);
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			g_error_free (error);

			return FALSE;
		}
	}

	if (do_not_extract) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     _("Extraction not performed"),
					     NULL,
					     _GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		if (fr_window_is_batch_mode (data->window))
			gtk_widget_destroy (data->dialog);

		return FALSE;
	}

	/* check extraction directory permissions. */

	if (_g_file_query_is_dir (destination)
	    && ! _g_file_check_permissions (destination, R_OK | W_OK))
	{
		GtkWidget *d;
		char      *utf8_path;

		utf8_path = _g_file_get_display_basename (destination);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Extraction not performed"),
					   _("You don’t have the right permissions to extract archives in the folder “%s”"),
					   utf8_path);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (utf8_path);
		g_object_unref (destination);

		return FALSE;
	}

	fr_window_set_extract_default_dir (window, destination);

	skip_newer = ! gtk_toggle_button_get_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_newer_checkbutton"))) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_newer_checkbutton")));
	junk_paths = ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_structure_checkbutton")));

	if (! gtk_toggle_button_get_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_newer_checkbutton"))))
		g_settings_set_boolean (data->settings, PREF_EXTRACT_SKIP_NEWER, skip_newer);
	g_settings_set_boolean (data->settings, PREF_EXTRACT_RECREATE_FOLDERS, ! junk_paths);

	selected_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("selected_files_radiobutton")));
	pattern_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("file_pattern_radiobutton")));

	/* create the file list. */

	file_list = NULL;

	if (selected_files) {
		file_list = data->selected_files;
		data->selected_files = NULL;       /* do not the list when destroying the dialog. */
	}
	else if (pattern_files) {
		const char *pattern;

		pattern = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("file_pattern_entry")));
		file_list = fr_window_get_file_list_pattern (window, pattern);
		if (file_list == NULL) {
			gtk_widget_destroy (data->dialog);
			g_object_unref (destination);
			return FALSE;
		}
	}

	if (selected_files) {
		base_dir = data->base_dir_for_selection;
		data->base_dir_for_selection = NULL;
	}
	else
		base_dir = NULL;

	/* close the dialog. */

	gtk_widget_destroy (data->dialog);

	/* extract ! */

	fr_window_extract_archive_and_continue (window,
				       	        file_list,
						destination,
						base_dir,
						skip_newer,
						FR_OVERWRITE_ASK,
						junk_paths);

	_g_string_list_free (file_list);
	g_object_unref (destination);
	g_free (base_dir);

	return TRUE;
}


static int
file_selector_response_cb (GtkWidget    *widget,
		      int           response,
		      DialogData   *data)
{
	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (data->dialog);
		return TRUE;
	}

	if (response == GTK_RESPONSE_OK)
		return extract_cb (widget, data);

	return FALSE;
}


static void
files_entry_changed_cb (GtkWidget  *widget,
			DialogData *data)
{
	if (! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("file_pattern_radiobutton"))))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("file_pattern_radiobutton")), TRUE);
}


static void
dlg_extract__common (FrWindow *window,
	             GList    *selected_files,
	             char     *base_dir_for_selection)
{
	DialogData *data;

	data = g_new0 (DialogData, 1);
	data->settings = g_settings_new (FILE_ROLLER_SCHEMA_EXTRACT);
	data->window = window;
	data->selected_files = selected_files;
	data->base_dir_for_selection = base_dir_for_selection;
	data->extract_clicked = FALSE;

	data->dialog = gtk_file_chooser_dialog_new (C_("Window title", "Extract"),
						    GTK_WINDOW (data->window),
						    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
						    _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						    _GTK_LABEL_EXTRACT, GTK_RESPONSE_OK,
						    NULL);

	gtk_window_set_default_size (GTK_WINDOW (data->dialog), 530, 510);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (data->dialog), FALSE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (data->dialog), FALSE);
	gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER (data->dialog), TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

	data->builder = _gtk_builder_new_from_resource ("extract-dialog-options.ui");
	if (data->builder == NULL)
		return;
	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (data->dialog), GET_WIDGET ("extra_widget"));

	/* Set widgets data. */

	gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (data->dialog), fr_window_get_extract_default_dir (window), NULL);

	if (data->selected_files != NULL)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("selected_files_radiobutton")), TRUE);
	else {
		gtk_widget_set_sensitive (GET_WIDGET ("selected_files_radiobutton"), FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("all_files_radiobutton")), TRUE);
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_newer_checkbutton")), g_settings_get_boolean (data->settings, PREF_EXTRACT_SKIP_NEWER));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("keep_structure_checkbutton")), g_settings_get_boolean (data->settings, PREF_EXTRACT_RECREATE_FOLDERS));

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (file_selector_destroy_cb),
			  data);
	g_signal_connect (G_OBJECT (data->dialog),
			  "response",
			  G_CALLBACK (file_selector_response_cb),
			  data);
	g_signal_connect (G_OBJECT (GET_WIDGET ("file_pattern_entry")),
			  "changed",
			  G_CALLBACK (files_entry_changed_cb),
			  data);

	/* Run dialog. */

	gtk_window_set_modal (GTK_WINDOW (data->dialog),TRUE);
	gtk_widget_show (data->dialog);
}


void
dlg_extract (GtkWidget *widget,
	     gpointer   callback_data)
{
	FrWindow *window = callback_data;
	GList    *files;
	char     *base_dir;

	files = fr_window_get_selection (window, FALSE, &base_dir);
	dlg_extract__common (window, files, base_dir);
}


void
dlg_extract_folder_from_sidebar (GtkWidget *widget,
	     			 gpointer   callback_data)
{
	FrWindow *window = callback_data;
	GList    *files;
	char     *base_dir;

	files = fr_window_get_selection (window, TRUE, &base_dir);
	dlg_extract__common (window, files, base_dir);
}
