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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include "dlg-add.h"
#include "file-utils.h"
#include "fr-file-selector-dialog.h"
#include "fr-window.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (data->builder, (x)))


typedef struct {
	FrWindow   *window;
	GSettings  *settings;
	GtkWidget  *dialog;
	GtkBuilder *builder;
	char       *last_options;
} DialogData;


static void
file_selector_destroy_cb (GtkWidget  *widget,
			  DialogData *data)
{
	g_object_unref (data->builder);
	g_object_unref (data->settings);
	g_free (data->last_options);
	g_free (data);
}


static void dlg_add_folder_save_last_options (DialogData *data);


static int
file_selector_response_cb (GtkWidget    *widget,
			   int           response,
			   DialogData   *data)
{
	FrWindow    *window = data->window;
	GFile       *current_folder;
	gboolean     update, follow_links;
	const char  *include_files;
	const char  *exclude_files;
	const char  *exclude_folders;
	GList       *files;

	if (response == GTK_RESPONSE_NONE)
		return TRUE;

	dlg_add_folder_save_last_options (data);

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (data->dialog);
		return TRUE;
	}

	current_folder = fr_file_selector_dialog_get_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog));

	/* check folder permissions. */

	if (! _g_file_check_permissions (current_folder, R_OK)) {
		GtkWidget *d;
		char      *utf8_path;

		utf8_path = g_file_get_parse_name (current_folder);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not add the files to the archive"),
					   _("You don’t have the right permissions to read files from folder “%s”"),
					   utf8_path);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (utf8_path);
		g_object_unref (current_folder);

		return FALSE;
	}

	update = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("update_checkbutton")));
	follow_links = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("follow_links_checkbutton")));

	include_files = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("include_files_entry")));
	if (_g_utf8_all_spaces (include_files))
		include_files = "*";

	exclude_files = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("exclude_files_entry")));
	if (_g_utf8_all_spaces (exclude_files))
		exclude_files = NULL;

	exclude_folders = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("exclude_folders_entry")));
	if (_g_utf8_all_spaces (exclude_folders))
		exclude_folders = NULL;

	files = fr_file_selector_dialog_get_selected_files (FR_FILE_SELECTOR_DIALOG (data->dialog));

	fr_window_archive_add_with_filter (window,
					   files,
					   current_folder,
					   include_files,
					   exclude_files,
					   exclude_folders,
					   fr_window_get_current_location (window),
					   update,
					   follow_links);

	_g_object_list_unref (files);
	g_object_unref (current_folder);

	gtk_widget_destroy (data->dialog);

	return TRUE;
}


static void load_options_activate_cb (GtkMenuItem *menu_item, DialogData *data);
static void save_options_activate_cb (GtkMenuItem *menu_item, DialogData *data);
static void clear_options_activate_cb (GtkMenuItem *menu_item, DialogData *data);
static void dlg_add_folder_load_last_options (DialogData *data);


/* create the "add" dialog. */
void
dlg_add (FrWindow *window)
{
	DialogData *data;
	GtkWidget  *options_button;
	GtkWidget  *options_menu;
	GtkWidget  *menu_item;
	gboolean    use_header;
	GtkWidget  *button;

	data = g_new0 (DialogData, 1);
	data->settings = g_settings_new (FILE_ROLLER_SCHEMA_ADD);
	data->window = window;
	data->dialog = fr_file_selector_dialog_new (C_("Window title", "Add Files"), GTK_WINDOW (data->window));
	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

	g_object_get (data->dialog, "use-header-bar", &use_header, NULL);

	data->builder = _gtk_builder_new_from_resource ("add-dialog-options.ui");
	if (data->builder == NULL)
		return;
	fr_file_selector_dialog_set_extra_widget (FR_FILE_SELECTOR_DIALOG (data->dialog), GET_WIDGET ("extra_widget"));

	/* options menu button */

	options_button = gtk_menu_button_new ();
	gtk_button_set_label (GTK_BUTTON (options_button), _("_Options"));
	gtk_button_set_use_underline (GTK_BUTTON (options_button), TRUE);
	gtk_menu_button_set_use_popover (GTK_MENU_BUTTON (options_button), TRUE);
	gtk_widget_show (options_button);

	options_menu = gtk_menu_new ();

	/* load options */

	menu_item = gtk_menu_item_new_with_label (C_("Action", "Load Options"));
	gtk_widget_show (menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (load_options_activate_cb), data);
	gtk_menu_shell_append (GTK_MENU_SHELL (options_menu), menu_item);

	/* save options */

	menu_item = gtk_menu_item_new_with_label (C_("Action", "Save Options"));
	gtk_widget_show (menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (save_options_activate_cb), data);
	gtk_menu_shell_append (GTK_MENU_SHELL (options_menu), menu_item);

	/* clear options */

	menu_item = gtk_menu_item_new_with_label (_("Reset Options"));
	gtk_widget_show (menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (clear_options_activate_cb), data);
	gtk_menu_shell_append (GTK_MENU_SHELL (options_menu), menu_item);

	gtk_menu_button_set_popup (GTK_MENU_BUTTON (options_button), options_menu);

	/* add the buttons */

	if (! use_header)
		gtk_dialog_add_action_widget (GTK_DIALOG (data->dialog), options_button, GTK_RESPONSE_NONE);

	gtk_dialog_add_button (GTK_DIALOG (data->dialog),
			       _GTK_LABEL_CANCEL,
			       GTK_RESPONSE_CANCEL);
	button = gtk_dialog_add_button (GTK_DIALOG (data->dialog),
					_GTK_LABEL_ADD,
					GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_SUGGESTED_ACTION);

	if (use_header)
		gtk_header_bar_pack_end (GTK_HEADER_BAR (gtk_dialog_get_header_bar (GTK_DIALOG (data->dialog))),
				         options_button);

	/* set data */

	dlg_add_folder_load_last_options (data);

	/* signals */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (file_selector_destroy_cb),
			  data);
	g_signal_connect (G_OBJECT (data->dialog),
			  "response",
			  G_CALLBACK (file_selector_response_cb),
			  data);

	gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);
	gtk_widget_show (data->dialog);
}


/* load/save the dialog options */


static void
dlg_add_folder_save_last_used_options (DialogData *data,
			               const char *options_path)
{
	g_free (data->last_options);
	data->last_options = g_strdup (_g_path_get_basename (options_path));
}


static void
sync_widgets_with_options (DialogData *data,
			   GFile      *directory,
			   GList      *files,
			   const char *include_files,
			   const char *exclude_files,
			   const char *exclude_folders,
			   gboolean    update,
			   gboolean    no_follow_symlinks)
{
	if (directory == NULL)
		directory = fr_window_get_add_default_dir (data->window);
	if (directory == NULL)
		directory = _g_file_get_home ();

	if (files != NULL)
		fr_file_selector_dialog_set_selected_files (FR_FILE_SELECTOR_DIALOG (data->dialog), files);
	else
		fr_file_selector_dialog_set_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog), directory);

	if ((include_files == NULL) || (include_files[0] == '\0'))
		include_files = "*";
	gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("include_files_entry")), include_files);

	if (exclude_files != NULL)
		gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("exclude_files_entry")), exclude_files);
	if (exclude_folders != NULL)
		gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("exclude_folders_entry")), exclude_folders);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("update_checkbutton")), update);

	if ((data->window->archive != NULL) && data->window->archive->propAddCanStoreLinks) {
		gtk_widget_set_sensitive (GET_WIDGET ("follow_links_checkbutton"), TRUE);
		gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("follow_links_checkbutton")), FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("follow_links_checkbutton")), ! no_follow_symlinks);
	}
	else {
		gtk_widget_set_sensitive (GET_WIDGET ("follow_links_checkbutton"), FALSE);
		gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("follow_links_checkbutton")), TRUE);
	}
}


static void
clear_options_activate_cb (GtkMenuItem *menu_item,
			   DialogData  *data)
{
	GFile *folder;

	folder = fr_file_selector_dialog_get_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog));
	sync_widgets_with_options (data,
				   folder,
				   NULL,
				   "",
				   "",
				   "",
				   FALSE,
				   TRUE);

	_g_object_unref (folder);
}


static gboolean
dlg_add_folder_load_options (DialogData *data,
			     const char *name)
{
	GFile      *options_dir;
	GFile      *options_file;
	char       *file_path;
	GKeyFile   *key_file;
	GError     *error = NULL;
	char       *folder_uri = NULL;
	GList      *uris = NULL;
	GList      *files;
	char       *include_files = NULL;
	char       *exclude_files = NULL;
	char       *exclude_folders = NULL;
	gboolean    update;
	gboolean    no_symlinks;
	GFile      *folder;

	options_dir = _g_file_new_user_config_subdir (ADD_FOLDER_OPTIONS_DIR, TRUE);
	options_file = g_file_get_child (options_dir, name);
	file_path = g_file_get_path (options_file);
	key_file = g_key_file_new ();
	if (! g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_KEEP_COMMENTS, &error)) {
		if (error->code != G_IO_ERROR_NOT_FOUND)
			g_warning ("Could not load options file: %s\n", error->message);
		g_clear_error (&error);
		g_object_unref (options_file);
		g_object_unref (options_dir);
		g_key_file_free (key_file);
		return FALSE;
	}

	folder_uri = g_key_file_get_string (key_file, "Options", "base_dir", NULL);
	folder = g_file_new_for_uri (folder_uri);

	uris = _g_key_file_get_string_list (key_file, "Options", "files", NULL);
	if (uris == NULL) {
		char *filename = g_key_file_get_string (key_file, "Options", "filename", NULL);
		if (filename != NULL)
			uris = g_list_append (NULL, filename);
	}
	files = _g_file_list_new_from_uri_list (uris);

	include_files = g_key_file_get_string (key_file, "Options", "include_files", NULL);
	exclude_files = g_key_file_get_string (key_file, "Options", "exclude_files", NULL);
	exclude_folders = g_key_file_get_string (key_file, "Options", "exclude_folders", NULL);
	update = g_key_file_get_boolean (key_file, "Options", "update", NULL);
	no_symlinks = g_key_file_get_boolean (key_file, "Options", "no_symlinks", NULL);

	sync_widgets_with_options (data,
				   folder,
				   files,
			   	   include_files,
			   	   exclude_files,
			   	   exclude_folders,
			   	   update,
			   	   no_symlinks);

	dlg_add_folder_save_last_used_options (data, file_path);

	_g_object_unref (folder);
	g_free (folder_uri);
	_g_string_list_free (uris);
	_g_object_list_unref (files);
	g_free (include_files);
	g_free (exclude_files);
	g_free (exclude_folders);
	g_key_file_free (key_file);
	g_free (file_path);
	g_object_unref (options_file);
	g_object_unref (options_dir);

	return TRUE;
}


static void
dlg_add_folder_load_last_options (DialogData *data)
{
	char      *base_dir = NULL;
	char     **uris;
	GList     *files;
	char      *include_files = NULL;
	char      *exclude_files = NULL;
	char      *exclude_folders = NULL;
	gboolean   update;
	gboolean   no_follow_symlinks;
	GFile     *folder = NULL;
	int        i;

	base_dir = g_settings_get_string (data->settings, PREF_ADD_CURRENT_FOLDER);
	uris = g_settings_get_strv (data->settings, PREF_ADD_SELECTED_FILES);
	if (g_strcmp0 (base_dir, "") != 0)
		folder = g_file_new_for_uri (base_dir);
	include_files = g_settings_get_string (data->settings, PREF_ADD_INCLUDE_FILES);
	exclude_files = g_settings_get_string (data->settings, PREF_ADD_EXCLUDE_FILES);
	exclude_folders = g_settings_get_string (data->settings, PREF_ADD_EXCLUDE_FOLDERS);
	update = g_settings_get_boolean (data->settings, PREF_ADD_UPDATE);
	no_follow_symlinks = g_settings_get_boolean (data->settings, PREF_ADD_NO_FOLLOW_SYMLINKS);

	files = NULL;
	for (i = 0; uris[i] != 0; i++)
		files = g_list_prepend (files, g_file_new_for_uri (uris[i]));
	files = g_list_reverse (files);

	sync_widgets_with_options (data,
				   folder,
			   	   files,
			   	   include_files,
			   	   exclude_files,
			   	   exclude_folders,
			   	   update,
			   	   no_follow_symlinks);

	_g_object_unref (folder);
	g_strfreev (uris);
	g_free (base_dir);
	g_free (include_files);
	g_free (exclude_files);
	g_free (exclude_folders);
	g_list_free_full (files, (GDestroyNotify) g_object_unref);
}


static void
get_options_from_widgets (DialogData   *data,
			  GFile       **base_dir,
			  char       ***file_uris,
			  const char  **include_files,
			  const char  **exclude_files,
			  const char  **exclude_folders,
			  gboolean     *update,
			  gboolean     *no_symlinks)
{
	GList  *files;
	char  **uris;
	GList  *scan;
	int     i;

	*base_dir = fr_file_selector_dialog_get_current_folder (FR_FILE_SELECTOR_DIALOG (data->dialog));

	files = fr_file_selector_dialog_get_selected_files (FR_FILE_SELECTOR_DIALOG (data->dialog));
	uris = g_new (char *, g_list_length (files) + 1);
	for (scan = files, i = 0; scan; scan = scan->next)
		uris[i++] = g_file_get_uri (G_FILE (scan->data));
	uris[i] = NULL;
	*file_uris = uris;
	_g_object_list_unref (files);

	*update = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("update_checkbutton")));
	*no_symlinks = ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("follow_links_checkbutton")));

	*include_files = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("include_files_entry")));
	if (_g_utf8_all_spaces (*include_files))
		*include_files = "";

	*exclude_files = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("exclude_files_entry")));
	if (_g_utf8_all_spaces (*exclude_files))
		*exclude_files = "";

	*exclude_folders = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("exclude_folders_entry")));
	if (_g_utf8_all_spaces (*exclude_folders))
		*exclude_folders = "";
}


static void
_g_key_file_set_file_uri (GKeyFile   *key_file,
			  const char *group_name,
			  const char *key,
			  GFile      *file)
{
	char *uri;

	uri = g_file_get_uri (file);
	g_key_file_set_string (key_file, group_name, key, uri);

	g_free (uri);
}


static void
dlg_add_folder_save_current_options (DialogData *data,
				     GFile      *options_file)
{
	GFile       *folder;
	char       **files;
	const char  *include_files;
	const char  *exclude_files;
	const char  *exclude_folders;
	gboolean     update;
	gboolean     no_symlinks;
	GKeyFile    *key_file;

	get_options_from_widgets (data,
				  &folder,
				  &files,
				  &include_files,
				  &exclude_files,
				  &exclude_folders,
				  &update,
				  &no_symlinks);

	fr_window_set_add_default_dir (data->window, folder);

	key_file = g_key_file_new ();
	_g_key_file_set_file_uri (key_file, "Options", "base_dir", folder);
	g_key_file_set_string_list (key_file, "Options", "files", (const char * const *) files, g_strv_length (files));
	g_key_file_set_string (key_file, "Options", "include_files", include_files);
	g_key_file_set_string (key_file, "Options", "exclude_files", exclude_files);
	g_key_file_set_string (key_file, "Options", "exclude_folders", exclude_folders);
	g_key_file_set_boolean (key_file, "Options", "update", update);
	g_key_file_set_boolean (key_file, "Options", "no_symlinks", no_symlinks);
	_g_key_file_save (key_file, options_file);

	g_key_file_free (key_file);
	g_object_unref (folder);
	g_strfreev (files);
}


static void
dlg_add_folder_save_last_options (DialogData *data)
{
	GFile       *folder;
	char       **files;
	const char  *include_files;
	const char  *exclude_files;
	const char  *exclude_folders;
	gboolean     update;
	gboolean     no_symlinks;

	get_options_from_widgets (data,
				  &folder,
				  &files,
				  &include_files,
				  &exclude_files,
				  &exclude_folders,
				  &update,
				  &no_symlinks);

	if (folder != NULL) {
		char *base_dir = g_file_get_uri (folder);

		g_settings_set_string (data->settings, PREF_ADD_CURRENT_FOLDER, base_dir);
		g_settings_set_strv (data->settings, PREF_ADD_SELECTED_FILES, (const char * const *) files);
		g_settings_set_string (data->settings, PREF_ADD_INCLUDE_FILES, include_files);
		g_settings_set_string (data->settings, PREF_ADD_EXCLUDE_FILES, exclude_files);
		g_settings_set_string (data->settings, PREF_ADD_EXCLUDE_FOLDERS, exclude_folders);
		g_settings_set_boolean (data->settings, PREF_ADD_UPDATE, update);
		g_settings_set_boolean (data->settings, PREF_ADD_NO_FOLLOW_SYMLINKS, no_symlinks);

		g_free (base_dir);
	}

	_g_object_unref (folder);
	g_strfreev (files);
}


typedef struct {
	DialogData   *data;
	GtkBuilder *builder;
	GtkWidget    *dialog;
	GtkWidget    *aod_treeview;
	GtkTreeModel *aod_model;
} LoadOptionsDialogData;


static void
aod_destroy_cb (GtkWidget             *widget,
		LoadOptionsDialogData *aod_data)
{
	g_object_unref (aod_data->builder);
	g_free (aod_data);
}


static void
aod_apply_cb (GtkWidget *widget,
	      gpointer   callback_data)
{
	LoadOptionsDialogData *aod_data = callback_data;
	DialogData            *data = aod_data->data;
	GtkTreeSelection      *selection;
	GtkTreeIter            iter;
	char                  *options_name;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (aod_data->aod_treeview));
	if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (aod_data->aod_model, &iter, 1, &options_name, -1);

	dlg_add_folder_load_options (data, options_name);
	g_free (options_name);

	gtk_widget_destroy (aod_data->dialog);
}


static void
aod_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  gpointer           callback_data)
{
	aod_apply_cb (NULL, callback_data);
}


static void
aod_update_option_list (LoadOptionsDialogData *aod_data)
{
	GtkListStore    *list_store = GTK_LIST_STORE (aod_data->aod_model);
	GFile           *options_dir;
	GFileEnumerator *file_enum;
	GFileInfo       *info;
	GError          *err = NULL;

	gtk_list_store_clear (list_store);

	options_dir = _g_file_new_user_config_subdir (ADD_FOLDER_OPTIONS_DIR, TRUE);
	_g_file_make_directory_tree (options_dir, 0700, NULL);

	file_enum = g_file_enumerate_children (options_dir, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &err);
	if (err != NULL) {
		g_warning ("Failed to enumerate children: %s", err->message);
		g_clear_error (&err);
		g_object_unref (options_dir);
		return;
	}

	while ((info = g_file_enumerator_next_file (file_enum, NULL, &err)) != NULL) {
		const char  *name;
		char        *display_name;
		GtkTreeIter  iter;

		if (err != NULL) {
			g_warning ("Failed to get info while enumerating: %s", err->message);
			g_clear_error (&err);
			continue;
		}

		name = g_file_info_get_name (info);
		display_name = g_filename_display_name (name);

		gtk_list_store_append (GTK_LIST_STORE (aod_data->aod_model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (aod_data->aod_model), &iter,
				    0, name,
				    1, display_name,
				    -1);

		g_free (display_name);
		g_object_unref (info);
	}

	if (err != NULL) {
		g_warning ("Failed to get info after enumeration: %s", err->message);
		g_clear_error (&err);
	}

	g_object_unref (options_dir);
}


static void
aod_remove_cb (GtkWidget             *widget,
	       LoadOptionsDialogData *aod_data)
{
	GtkTreeSelection *selection;
	GtkTreeIter       iter;
	char             *filename;
	GFile            *options_dir;
	GFile            *options_file;
	GError           *error = NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (aod_data->aod_treeview));
	if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (aod_data->aod_model, &iter, 1, &filename, -1);
	gtk_list_store_remove (GTK_LIST_STORE (aod_data->aod_model), &iter);

	options_dir = _g_file_new_user_config_subdir (ADD_FOLDER_OPTIONS_DIR, TRUE);
	options_file = g_file_get_child (options_dir, filename);
	if (! g_file_delete (options_file, NULL, &error)) {
		g_warning ("could not delete the options: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (options_file);
	g_object_unref (options_dir);
	g_free (filename);
}


#define RESPONSE_DELETE_OPTIONS 10


static void
load_options_activate_cb (GtkMenuItem *menu_item,
			  DialogData  *data)
{
	LoadOptionsDialogData *aod_data;
	GtkWidget             *ok_button;
	GtkWidget             *cancel_button;
	GtkWidget             *remove_button;
	GtkCellRenderer       *renderer;
	GtkTreeViewColumn     *column;

	aod_data = g_new0 (LoadOptionsDialogData, 1);

	aod_data->data = data;
	aod_data->builder = _gtk_builder_new_from_resource ("add-options.ui");
	if (aod_data->builder == NULL) {
		g_free (aod_data);
		return;
	}

	/* Get the widgets. */

	aod_data->dialog = g_object_new (GTK_TYPE_DIALOG,
					 "title", C_("Window title", "Load Options"),
					 "modal", TRUE,
					 "use-header-bar", _gtk_settings_get_dialogs_use_header (),
					 NULL);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (aod_data->dialog))),
			   _gtk_builder_get_widget (aod_data->builder, "add_options_dialog"));

	gtk_dialog_add_buttons (GTK_DIALOG (aod_data->dialog),
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				_("_Apply"), GTK_RESPONSE_OK,
				_("_Delete"), RESPONSE_DELETE_OPTIONS,
				NULL);

	aod_data->aod_treeview = _gtk_builder_get_widget (aod_data->builder, "aod_treeview");

	ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (aod_data->dialog), GTK_RESPONSE_OK);
	gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), GTK_STYLE_CLASS_SUGGESTED_ACTION);
	cancel_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (aod_data->dialog), GTK_RESPONSE_CANCEL);
	remove_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (aod_data->dialog), RESPONSE_DELETE_OPTIONS);
	gtk_style_context_add_class (gtk_widget_get_style_context (remove_button), GTK_STYLE_CLASS_DESTRUCTIVE_ACTION);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (aod_data->dialog),
			  "destroy",
			  G_CALLBACK (aod_destroy_cb),
			  aod_data);
	g_signal_connect (G_OBJECT (aod_data->aod_treeview),
			  "row_activated",
			  G_CALLBACK (aod_activated_cb),
			  aod_data);
	g_signal_connect_swapped (G_OBJECT (cancel_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (aod_data->dialog));
	g_signal_connect (G_OBJECT (ok_button),
			  "clicked",
			  G_CALLBACK (aod_apply_cb),
			  aod_data);
	g_signal_connect (G_OBJECT (remove_button),
			  "clicked",
			  G_CALLBACK (aod_remove_cb),
			  aod_data);

	/* Set data. */

	aod_data->aod_model = GTK_TREE_MODEL (gtk_list_store_new (2,
								  G_TYPE_STRING,
								  G_TYPE_STRING));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (aod_data->aod_model),
					      0,
					      GTK_SORT_ASCENDING);
	gtk_tree_view_set_model (GTK_TREE_VIEW (aod_data->aod_treeview),
				 aod_data->aod_model);
	g_object_unref (aod_data->aod_model);

	/**/

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (NULL,
							   renderer,
							   "text", 0,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW (aod_data->aod_treeview),
				     column);

	aod_update_option_list (aod_data);

	/* Run */

	gtk_window_set_transient_for (GTK_WINDOW (aod_data->dialog),
				      GTK_WINDOW (data->dialog));
	gtk_window_set_modal (GTK_WINDOW (aod_data->dialog), TRUE);
	_gtk_dialog_add_to_window_group (GTK_DIALOG (aod_data->dialog));
	gtk_widget_show (aod_data->dialog);
}


static void
save_options_activate_cb (GtkMenuItem *menu_item,
			  DialogData  *data)
{
	GFile *options_dir;
	GFile *options_file;
	char  *opt_filename;

	options_dir = _g_file_new_user_config_subdir (ADD_FOLDER_OPTIONS_DIR, TRUE);
	_g_file_make_directory_tree (options_dir, 0700, NULL);

	opt_filename = _gtk_request_dialog_run (GTK_WINDOW (data->dialog),
						GTK_DIALOG_MODAL,
						C_("Window title", "Save Options"),
						_("_Options Name:"),
						(data->last_options != NULL) ? data->last_options : "",
						1024,
						_GTK_LABEL_CANCEL,
						_GTK_LABEL_SAVE);
	if (opt_filename == NULL)
		return;

	options_file = g_file_get_child_for_display_name (options_dir, opt_filename, NULL);
	dlg_add_folder_save_current_options (data, options_file);
	dlg_add_folder_save_last_used_options (data, opt_filename);

	g_free (opt_filename);
	g_object_unref (options_file);
	g_object_unref (options_dir);
}
