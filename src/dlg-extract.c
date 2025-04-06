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
#include "fr-file-selector-dialog.h"
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
	gboolean      do_not_extract;
	GFile        *destination;
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
	g_clear_object (&data->destination);
	g_free (data);
}

static void extract_cb_possibly_try_to_create_destination_directory (DialogData *data);
static void extract_cb_check_whether_preparing_destination_failed (DialogData *data);
static void extract_cb_check_permissions (DialogData *data);
static void extract_cb_start_extracting (DialogData *data);
static void create_destination_response_cb (GtkDialog *dialog, int response, DialogData *data);

static void
extract_cb (GtkDialog   *dialog,
	    DialogData  *data)
{
	data->extract_clicked = TRUE;

	/* collect extraction options. */

	data->destination = fr_file_selector_dialog_get_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog));

	/* check directory existence. */

	if (! _g_file_query_is_dir (data->destination)) {
		if (! ForceDirectoryCreation) {
			GtkWidget *d;
			g_autofree char *folder_name;
			g_autofree char *msg;

			folder_name = _g_file_get_display_basename (data->destination);
			msg = g_strdup_printf (_("Destination folder “%s” does not exist.\n\nDo you want to create it?"), folder_name);

			d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						     GTK_DIALOG_MODAL,
						     msg,
						     NULL,
						     _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
			g_signal_connect (GTK_MESSAGE_DIALOG (d), "response", G_CALLBACK (create_destination_response_cb), data);
			gtk_widget_show (d);
		} else {
			extract_cb_possibly_try_to_create_destination_directory (data);
		}
	} else {
		extract_cb_check_whether_preparing_destination_failed (data);
	}
}


static void
create_destination_response_cb (GtkDialog    *dialog,
		      int           response,
		      DialogData   *data)
{
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (response != GTK_RESPONSE_YES) {
		data->do_not_extract = TRUE;
	}

	extract_cb_possibly_try_to_create_destination_directory (data);
}


static void
extract_cb_possibly_try_to_create_destination_directory (DialogData  *data)
{
	g_autofree GError *error = NULL;
	if (! data->do_not_extract && ! _g_file_make_directory_tree (data->destination, 0755, &error)) {
		GtkWidget  *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (data->dialog),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Extraction not performed"),
					   _("Could not create the destination folder: %s."),
					   error->message);
		g_signal_connect (GTK_MESSAGE_DIALOG (d), "response", G_CALLBACK (gtk_window_destroy), NULL);
		gtk_widget_show (d);
	} else {
		extract_cb_check_whether_preparing_destination_failed (data);
	}
}


static void
extraction_not_performed_cb (GtkDialog    *dialog,
		      int           response,
		      DialogData   *data)
{
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (fr_window_is_batch_mode (data->window)) {
		gtk_window_destroy (GTK_WINDOW (data->dialog));
	}
}


static void
extract_cb_check_whether_preparing_destination_failed (DialogData  *data)
{
	if (data->do_not_extract) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (data->window),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     _("Extraction not performed"),
					     NULL,
					     _GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		g_signal_connect (GTK_MESSAGE_DIALOG (d), "response", G_CALLBACK (extraction_not_performed_cb), data);
		gtk_widget_show (d);
	} else {
		extract_cb_check_permissions (data);
	}
}


static void
extract_cb_check_permissions (DialogData  *data)
{
	/* check extraction directory permissions. */

	if (_g_file_query_is_dir (data->destination)
	    && ! _g_file_check_permissions (data->destination, R_OK | W_OK))
	{
		GtkWidget *d;
		g_autofree char *utf8_path;

		utf8_path = _g_file_get_display_basename (data->destination);

		d = _gtk_error_dialog_new (GTK_WINDOW (data->dialog),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Extraction not performed"),
					   _("You don’t have the right permissions to extract archives in the folder “%s”"),
					   utf8_path);
		g_signal_connect (GTK_MESSAGE_DIALOG (d), "response", G_CALLBACK (gtk_window_destroy), NULL);
		gtk_widget_show (d);
	} else {
		extract_cb_start_extracting (data);
	}
}


static void
extract_cb_start_extracting (DialogData *data)
{
	FrWindow   *window = data->window;
	GFile      *destination = data->destination;
	gboolean    skip_newer;
	gboolean    selected_files;
	gboolean    pattern_files;
	gboolean    junk_paths;
	GList      *file_list;
	char       *base_dir;

	fr_window_set_extract_default_dir (window, destination);

	skip_newer = ! gtk_check_button_get_inconsistent (GTK_CHECK_BUTTON (GET_WIDGET ("keep_newer_checkbutton"))) && gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("keep_newer_checkbutton")));
	junk_paths = ! gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("keep_structure_checkbutton")));

	if (! gtk_check_button_get_inconsistent (GTK_CHECK_BUTTON (GET_WIDGET ("keep_newer_checkbutton"))))
		g_settings_set_boolean (data->settings, PREF_EXTRACT_SKIP_NEWER, skip_newer);
	g_settings_set_boolean (data->settings, PREF_EXTRACT_RECREATE_FOLDERS, ! junk_paths);

	selected_files = gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("selected_files_radiobutton")));
	pattern_files = gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("file_pattern_radiobutton")));

	/* create the file list. */

	file_list = NULL;

	if (selected_files) {
		file_list = data->selected_files;
		data->selected_files = NULL;       /* do not the list when destroying the dialog. */
	}
	else if (pattern_files) {
		const char *pattern;

		pattern = gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("file_pattern_entry")));
		file_list = fr_window_get_file_list_pattern (window, pattern);
		if (file_list == NULL) {
			gtk_window_destroy (GTK_WINDOW (data->dialog));
			return;
		}
	}

	if (selected_files) {
		base_dir = data->base_dir_for_selection;
		data->base_dir_for_selection = NULL;
	}
	else
		base_dir = NULL;

	/* close the dialog. */

	data->destination = NULL; /* do not free when destroying the dialog. */
	gtk_window_destroy (GTK_WINDOW (data->dialog));

	/* extract ! */

	fr_window_extract_archive_and_continue (window,
				       	        file_list,
						destination,
						base_dir,
						skip_newer,
						FR_OVERWRITE_ASK,
						junk_paths);

	_g_string_list_free (file_list);
	g_free (base_dir);
	g_object_unref (destination);
}


static void
file_selector_response_cb (GtkDialog    *dialog,
		      int           response,
		      DialogData   *data)
{
	pref_util_save_window_geometry (GTK_WINDOW (data->dialog), "Extract");

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (data->dialog));
	} else if (response == GTK_RESPONSE_OK) {
		extract_cb (dialog, data);
	}
}


static void
files_entry_changed_cb (GtkEditable  *widget,
			DialogData *data)
{
	if (! gtk_check_button_get_active (GTK_CHECK_BUTTON (GET_WIDGET ("file_pattern_radiobutton"))))
		gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("file_pattern_radiobutton")), TRUE);
}


static void
dlg_extract__common (FrWindow *window,
	             GList    *selected_files,
	             char     *base_dir_for_selection,
		     gboolean  all_by_default)
{
	DialogData *data;

	data = g_new0 (DialogData, 1);
	data->settings = g_settings_new (FILE_ROLLER_SCHEMA_EXTRACT);
	data->window = window;
	data->selected_files = selected_files;
	data->base_dir_for_selection = base_dir_for_selection;
	data->extract_clicked = FALSE;
	data->do_not_extract = FALSE;
	data->destination = NULL;

	data->dialog = fr_file_selector_dialog_new (FR_FILE_SELECTOR_MODE_FOLDER,
						    C_("Window title", "Extract"),
						    GTK_WINDOW (data->window));

	gtk_dialog_add_button (GTK_DIALOG (data->dialog), _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL);
	GtkWidget *button = gtk_dialog_add_button (GTK_DIALOG (data->dialog), _GTK_LABEL_EXTRACT, GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "suggested-action");

	gtk_window_set_default_size (GTK_WINDOW (data->dialog), 530, 510);
	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

	data->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "extract-dialog-options.ui");
	fr_file_selector_dialog_set_extra_widget (FR_FILE_SELECTOR_DIALOG (data->dialog), GET_WIDGET ("extra_widget"));

	/* Set widgets data. */

	fr_file_selector_dialog_set_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog), fr_window_get_extract_default_dir (window));

	gtk_widget_set_sensitive (GET_WIDGET ("selected_files_radiobutton"), (data->selected_files != NULL));
	if ((data->selected_files != NULL) && !all_by_default)
		gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("selected_files_radiobutton")), TRUE);
	else
		gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("all_files_radiobutton")), TRUE);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("keep_newer_checkbutton")), g_settings_get_boolean (data->settings, PREF_EXTRACT_SKIP_NEWER));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (GET_WIDGET ("keep_structure_checkbutton")), g_settings_get_boolean (data->settings, PREF_EXTRACT_RECREATE_FOLDERS));

	/* Set the signals handlers. */

	g_signal_connect (GTK_DIALOG (data->dialog),
			  "destroy",
			  G_CALLBACK (file_selector_destroy_cb),
			  data);
	g_signal_connect (GTK_DIALOG (data->dialog),
			  "response",
			  G_CALLBACK (file_selector_response_cb),
			  data);
	g_signal_connect (GTK_ENTRY (GET_WIDGET("file_pattern_entry")),
			  "changed",
			  G_CALLBACK (files_entry_changed_cb),
			  data);

	/* Run dialog. */

	gtk_window_set_modal (GTK_WINDOW (data->dialog),TRUE);
	pref_util_restore_window_geometry (GTK_WINDOW (data->dialog), "Extract");
}


void
dlg_extract (GtkWidget *widget,
	     gpointer   callback_data)
{
	FrWindow *window = callback_data;
	GList    *files;
	char     *base_dir;

	files = fr_window_get_selection (window, FALSE, &base_dir);
	dlg_extract__common (window, files, base_dir, FALSE);
}


void
dlg_extract_folder_from_sidebar (GtkWidget *widget,
	     			 gpointer   callback_data)
{
	FrWindow *window = callback_data;
	GList    *files;
	char     *base_dir;

	files = fr_window_get_selection (window, TRUE, &base_dir);
	dlg_extract__common (window, files, base_dir, FALSE);
}


void
dlg_extract_all_by_default (GtkWidget *widget,
			    gpointer   callback_data)
{
	FrWindow *window = callback_data;
	GList    *files;
	char     *base_dir;

	files = fr_window_get_selection (window, FALSE, &base_dir);
	dlg_extract__common (window, files, base_dir, TRUE);
}
