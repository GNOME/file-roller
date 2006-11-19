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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgnome/gnome-help.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "actions.h"
#include "dlg-add-files.h"
#include "dlg-add-folder.h"
#include "dlg-extract.h"
#include "dlg-delete.h"
#include "dlg-open-with.h"
#include "dlg-password.h"
#include "main.h"
#include "gtk-utils.h"
#include "window.h"
#include "file-utils.h"
#include "fr-process.h"
#include "gconf-utils.h"
#include "glib-utils.h"
#include "dlg-prop.h"


/* -- new archive -- */


static void
new_file_destroy_cb (GtkWidget *w,
		     GtkWidget *file_sel)
{
}


static void
new_archive (GtkWidget *file_sel, 
	     FRWindow  *window, 
	     gchar     *path)
{
	FRWindow *archive_window;
	gboolean  new_window;

	new_window = window->archive_present;

	if (new_window) 
		archive_window = window_new ();
	else
		archive_window = window;

	/* Pass the add_after_creation options to the new window. */
	if (archive_window != window) {
		archive_window->add_after_creation = window->add_after_creation;
		if (archive_window->add_after_creation) {
			archive_window->dropped_file_list = window->dropped_file_list;
			window->dropped_file_list = NULL;
		}
		window->add_after_creation = FALSE;
	}

	if (window_archive_new (archive_window, path)) {
		gtk_window_present (GTK_WINDOW (archive_window->app));
		gtk_widget_destroy (file_sel);
	} else if (new_window)
		window_close (archive_window);
}


/* when on Automatic the user provided extension needs to be supported, 
   otherwise an existing unsupported archive can be deleted (if the user
   provided name matches with its name) before we find out that the 
   archive is unsupported
*/
static gboolean 
is_supported_extension (GtkWidget *file_sel, 
			char      *filename)
{
	int i;
	for (i = 0; save_type[i] != FR_FILE_TYPE_NULL; i++) 
		if (file_extension_is (filename, file_type_desc[save_type[i]].ext)) 
			return TRUE;
	return FALSE;
}


static char *
get_full_path (GtkWidget *file_sel)
{
	GtkWidget   *combo_box;
	char        *full_path = NULL;
	char        *path;
	const char  *filename;
	int          idx;

	combo_box = g_object_get_data (G_OBJECT (file_sel), "fr_combo_box");
	path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));

	if ((path == NULL) || (*path == 0))
		return NULL;

	filename = file_name_from_path (path);
	if ((filename == NULL) || (*filename == 0)) {
		g_free (path);
		return NULL;
	}
	
	idx = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
	if (idx > 0) {
		const char *path_ext = fr_archive_utils__get_file_name_ext (path);
		char       *default_ext = file_type_desc[save_type[idx-1]].ext;
		if (strcmp_null_tollerant (path_ext, default_ext) != 0) {
			full_path = g_strconcat (path, default_ext, NULL);
			g_free (path);
		}
	} 
	if (full_path == NULL)
		full_path = path;

	return full_path;
}


static char *
get_archive_filename_from_selector (FRWindow  *window,
				    GtkWidget *file_sel)
{
	char *path = NULL;
	char *dir;

	path = get_full_path (file_sel);
	if ((path == NULL) || (*path == 0)) {
		GtkWidget *dialog;

		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not create the archive"),
						  _("You have to specify an archive name."),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return NULL;
	}
	
	dir = remove_level_from_path (path);
	if (access (dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *dialog;

		g_free (dir);
		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not create the archive"),
						  _("You don't have permission to create an archive in this folder"),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return NULL;
	}
	g_free (dir);

	/* if the user did not specify a valid extension use the filetype combobox current type
	 * or tar.gz if automatic is selected. */
	if (fr_archive_utils__get_file_name_ext (path) == NULL) {
		GtkWidget *combo_box;
		int        idx;
		char      *new_path;
		char      *ext = NULL;

		combo_box = g_object_get_data (G_OBJECT (file_sel), "fr_combo_box");
		idx = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
		if (idx > 0)
			ext = file_type_desc[save_type[idx-1]].ext;
		else
			ext = ".tar.gz";
		new_path = g_strconcat (path, ext, NULL);
		g_free (path);
		path = new_path;
	}

	debug (DEBUG_INFO, "create/save %s\n", path); 

	if (path_is_file (path)) {
		GtkWidget *dialog;
		int        r;

		if (!is_supported_extension (file_sel, path)) {
                	dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_STOCK_DIALOG_ERROR,
                                                  _("Could not create the archive"),
                                                  _("Archive type not supported."),
                                                  GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL,
                                                  NULL);
                	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
                	gtk_dialog_run (GTK_DIALOG (dialog));
                	gtk_widget_destroy (GTK_WIDGET (dialog));
			g_free (path);
			return NULL;
		}

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_QUESTION,
						  _("The archive already exists.  Do you want to overwrite it?"),
						  NULL,
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  _("Overwrite"), GTK_RESPONSE_YES,
						  NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (r != GTK_RESPONSE_YES) {
			g_free (path);
			return NULL;
		}

		if (unlink (path) != 0) {
			GtkWidget *dialog;
			dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not delete the old archive."),
							  NULL,
							  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			g_free (path);
			return NULL;
		}
	}

	return path;
}


static void 
new_file_response_cb (GtkWidget *w,
		      int        response,
		      GtkWidget *file_sel)
{
	FRWindow *window;
	char     *path;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	if (response == GTK_RESPONSE_HELP) {
		show_help_dialog (GTK_WINDOW (file_sel), "file-roller-create");
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");

	path = get_archive_filename_from_selector (window, file_sel);
	if (path != NULL) {
		new_archive (file_sel, window, path);
		g_free (path);
	}
}


static void
filetype_combobox_changed_cb (GtkComboBox *combo_box,
			      GtkWidget   *file_sel)
{
	int         idx;
	const char *filename;
	const char *ext, *newext;
	char       *new_filename, *filename_noext;

	idx = gtk_combo_box_get_active (combo_box) - 1;
	if (idx < 0) 
		return;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));
	if (filename == NULL)
		return;

	filename = file_name_from_path (filename);
	if (filename == NULL)
		return;

	ext = fr_archive_utils__get_file_name_ext (filename);
	if (ext == NULL)
		ext = "";

	filename_noext = g_strndup (filename, strlen (filename) - strlen (ext));

	newext = file_type_desc[save_type[idx]].ext;
	new_filename = g_strconcat (filename_noext, newext, NULL);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_sel), new_filename);

	g_free (new_filename);
	g_free (filename_noext);
}


void
activate_action_new (GtkAction *action, 
		     gpointer   data)
{
	FRWindow      *window = data;
	GtkWidget     *file_sel;
	GtkWidget     *hbox;
	GtkWidget     *combo_box;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (
			_("New"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_NEW, GTK_RESPONSE_OK,
			GTK_STOCK_HELP, GTK_RESPONSE_HELP,
			NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	if (window->add_after_creation && (window->dropped_file_list != NULL)) {
		char       *first_item = (char*) window->dropped_file_list->data;
		char       *folder = remove_level_from_path (first_item);
		const char *name = NULL;

		if (window->dropped_file_list->next != NULL)
			name = file_name_from_path (folder);
		else
			name = file_name_from_path (first_item);

		if (folder != NULL) 
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel), folder);

		if (name != NULL) {
			char *ext, *name_ext;
			ext = eel_gconf_get_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, ".tgz");
			name_ext = g_strconcat (name, ext, NULL);
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_sel), name_ext);
			g_free (name_ext);
			g_free (ext);
		}

		g_free (folder);
	}

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; save_type[i] != FR_FILE_TYPE_NULL; i++) 
		gtk_file_filter_add_mime_type (filter, file_type_desc[save_type[i]].mime_type);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (file_sel), hbox);

	gtk_box_pack_start (GTK_BOX (hbox), 
			    gtk_label_new (_("Archive type:")),
			    FALSE, FALSE, 0);

	combo_box = gtk_combo_box_new_text ();
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Automatic"));
	for (i = 0; save_type[i] != FR_FILE_TYPE_NULL; i++)
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
					   _(file_type_desc[save_type[i]].name));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	gtk_box_pack_start (GTK_BOX (hbox), combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);
	
	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	g_object_set_data (G_OBJECT (file_sel), "fr_combo_box", combo_box);
	g_object_set_data (G_OBJECT (window->app), "fr_file_sel", file_sel);
	
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (new_file_response_cb), 
			  file_sel);
	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (new_file_destroy_cb),
			  file_sel);
	g_signal_connect (G_OBJECT (combo_box),
			  "changed", 
			  G_CALLBACK (filetype_combobox_changed_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel),TRUE);
	gtk_widget_show_all (file_sel);
}


/* -- open archive -- */


static void
open_file_destroy_cb (GtkWidget *w,
		      GtkWidget *file_sel)
{
}


static void 
open_file_response_cb (GtkWidget *w,
		       int        response,
		       GtkWidget *file_sel)
{
	FRWindow *window = NULL;
	char     *path;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");
	path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));

        if (path == NULL)
                return;

	if (window_archive_open (window, path, GTK_WINDOW (file_sel)))
		gtk_widget_destroy (file_sel);

	g_free (path);
}


void
activate_action_open (GtkAction *action, 
		      gpointer   data)
{
	GtkWidget     *file_sel;
	FRWindow      *window = data;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (
			_("Open"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_OK,
			NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; open_type[i] != FR_FILE_TYPE_NULL; i++)
		gtk_file_filter_add_mime_type (filter, file_type_desc[open_type[i]].mime_type);
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
 
	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (open_file_destroy_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show (file_sel);
}


/* -- save archive -- */


static void
save_file_destroy_cb (GtkWidget *w,
		      GtkWidget *file_sel)
{
}


static void 
save_file_response_cb (GtkWidget *w,
		       gint       response,
		       GtkWidget *file_sel)
{
	FRWindow *window;
	char     *path;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	if (response == GTK_RESPONSE_HELP) {
		show_help_dialog (GTK_WINDOW (file_sel), "file-roller-convert-archive");
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");

	path = get_archive_filename_from_selector (window, file_sel);
	if (path != NULL) {
		window_archive_save_as (window, path);
		gtk_widget_destroy (file_sel);
		g_free (path);
	}
}


void
activate_action_save_as (GtkAction *action, 
			 gpointer   data)
{
	FRWindow      *window = data;
	GtkWidget     *file_sel;
	GtkWidget     *hbox;
	GtkWidget     *combo_box;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (
			_("Save"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_SAVE, GTK_RESPONSE_OK,
			GTK_STOCK_HELP, GTK_RESPONSE_HELP,
			NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	if (window->archive_filename != NULL) 
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_sel), file_name_from_path (window->archive_filename));

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; save_type[i] != FR_FILE_TYPE_NULL; i++)
		gtk_file_filter_add_mime_type (filter, file_type_desc[save_type[i]].mime_type);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (hbox), 
			    gtk_label_new (_("Archive type:")),
			    FALSE, FALSE, 0);
	
	combo_box = gtk_combo_box_new_text ();
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Automatic"));
	for (i = 0; save_type[i] != FR_FILE_TYPE_NULL; i++)
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
					   _(file_type_desc[save_type[i]].name));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	gtk_box_pack_start (GTK_BOX (hbox), combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);

	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (file_sel), hbox);
	
	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	g_object_set_data (G_OBJECT (file_sel), "fr_combo_box", combo_box);
	
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (save_file_response_cb), 
			  file_sel);
	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (save_file_destroy_cb),
			  file_sel);
	g_signal_connect (G_OBJECT (combo_box),
			  "changed", 
			  G_CALLBACK (filetype_combobox_changed_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show_all (file_sel);
}


void
activate_action_test_archive (GtkAction *action,
			      gpointer   data)
{
	FRWindow *window = data;
	fr_archive_test (window->archive, window->password);
}


void
activate_action_properties (GtkAction *action,
			    gpointer   data)
{
	FRWindow *window = data;
	dlg_prop (window);
}


void
activate_action_close (GtkAction *action, 
		       gpointer   data)
{
	FRWindow *window = data;
	window_close (window);
}

void
activate_action_add_files (GtkAction *action, 
			   gpointer   data)
{
	add_files_cb (NULL, data);
}


void
activate_action_add_folder (GtkAction *action, 
			    gpointer   data)
{
	add_folder_cb (NULL, data);
}


void
activate_action_extract (GtkAction *action, 
			 gpointer   data)
{
	dlg_extract (NULL, data);
}


void
activate_action_copy (GtkAction *action, 
		      gpointer   data)
{
	window_copy_selection ((FRWindow*) data);
}


void
activate_action_cut (GtkAction *action, 
		     gpointer   data)
{
	window_cut_selection ((FRWindow*) data);
}


void
activate_action_paste (GtkAction *action, 
		       gpointer   data)
{
	window_paste_selection ((FRWindow*) data);
}


void
activate_action_rename (GtkAction *action, 
			gpointer   data)
{
	window_rename_selection ((FRWindow*) data);
}


void
activate_action_delete (GtkAction *action, 
			gpointer   data)
{
	dlg_delete (NULL, data);
}


void
activate_action_select_all (GtkAction *action, 
			    gpointer   data)
{
	FRWindow *window = data;
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
activate_action_deselect_all (GtkAction *action, 
			      gpointer   data)
{
	FRWindow *window = data;
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
activate_action_open_with (GtkAction *action, 
			   gpointer   data)
{
	open_with_cb (NULL, (FRWindow*) data);
}


void
activate_action_view_or_open (GtkAction *action, 
			      gpointer   data)
{
	FRWindow *window = data;
	GList    *file_list;

	file_list = window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL) 
		return;
	window_view_or_open_file (window, (char*) file_list->data);
	path_list_free (file_list);	
}


void
activate_action_open_folder (GtkAction *action, 
			     gpointer   data)
{
	FRWindow *window = data;
	window_current_folder_activated (window);
}


void
activate_action_password (GtkAction *action, 
			  gpointer   data)
{
	dlg_password (NULL, (FRWindow*) data);
}


void
activate_action_view_toolbar (GtkAction *action, 
			      gpointer   data)
{
	eel_gconf_set_boolean (PREF_UI_TOOLBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}


void
activate_action_view_statusbar (GtkAction *action, 
				gpointer   data)
{
	eel_gconf_set_boolean (PREF_UI_STATUSBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}


void
activate_action_stop (GtkAction *action, 
		      gpointer   data)
{
	FRWindow *window = data;
	window_stop (window);
}


void
activate_action_reload (GtkAction *action, 
			gpointer   data)
{
	FRWindow *window = data;
	if (window->activity_ref == 0)
		window_archive_reload (window);
}


void
activate_action_sort_reverse_order (GtkAction *action, 
				    gpointer   data)
{
	FRWindow *window = data;
	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
		window->sort_type = GTK_SORT_DESCENDING;
	else
		window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
activate_action_last_output (GtkAction *action, 
			     gpointer   data)
{
	FRWindow *window = data;
	window_view_last_output (window, _("Last Output"));
}


void
activate_action_manual (GtkAction *action, 
			gpointer   data)
{
	FRWindow *window = data;
	GError   *err;
	
        err = NULL;  
        gnome_help_display ("file-roller", NULL, &err);
        
        if (err != NULL) {
                GtkWidget *dialog;
                
                dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_DESTROY_WITH_PARENT, 
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not display help"),
						  err->message,
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
                gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
                g_signal_connect (G_OBJECT (dialog), "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  NULL);
                
                gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
                
                gtk_widget_show (dialog);
                
                g_error_free (err);
        }
}


void
activate_action_about (GtkAction *action, 
		       gpointer   data)
{
	FRWindow         *window = data;
	
	char *license_text;

	const char       *authors[] = {
		"Paolo Bacchilega <paolo.bacchilega@libero.it>", NULL
	};

	const char       *documenters [] = {
		"Alexander Kirillov", 
		"Breda McColgan",
		NULL
	};
	
	const char *license[] = {
		"File Roller is free software; you can redistribute it and/or modify "
		"it under the terms of the GNU General Public License as published by "
		"the Free Software Foundation; either version 2 of the License, or "
		"(at your option) any later version.",
		"File Roller is distributed in the hope that it will be useful, "
		"but WITHOUT ANY WARRANTY; without even the implied warranty of "
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		"GNU General Public License for more details.",
		"You should have received a copy of the GNU General Public License "
		"along with Nautilus; if not, write to the Free Software Foundation, Inc., "
		"51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA"
	};

	license_text = g_strconcat (license[0], "\n\n", license[1], "\n\n",
				    license[2], "\n\n", NULL);

	gtk_show_about_dialog (GTK_WINDOW (window->app),
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2001-2006 Free Software Foundation, Inc.",
			       "comments", _("An archive manager for GNOME."),
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", _("translator-credits"),
			       "logo-icon-name", "file-roller",
			       "license", license_text,
			       "wrap-license", TRUE,
			       NULL);

	g_free (license_text);
}
