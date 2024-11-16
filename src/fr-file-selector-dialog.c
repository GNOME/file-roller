/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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
#include "fr-enum-types.h"
#include "fr-file-selector-dialog.h"
#include "fr-location-bar.h"
#include "fr-places-sidebar.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (self->builder, (x)))
#define PREF_FILE_SELECTOR_WINDOW_SIZE "window-size"
#define PREF_FILE_SELECTOR_SHOW_HIDDEN "show-hidden"
#define PREF_FILE_SELECTOR_SIDEBAR_SIZE "sidebar-size"
#define PREF_FILE_SELECTOR_SORT_METHOD "sort-method"
#define PREF_FILE_SELECTOR_SORT_TYPE "sort-type"
#define FILE_LIST_LINES 45
#define FILE_LIST_CHARS 60
#define SIDEBAR_CHARS   12

enum {
	PROP_0,
	PROP_SELECTION_MODE,
};

enum {
	FILE_LIST_COLUMN_ICON,
	FILE_LIST_COLUMN_NAME,
	FILE_LIST_COLUMN_SIZE,
	FILE_LIST_COLUMN_MODIFIED,
	FILE_LIST_COLUMN_FILE,
	FILE_LIST_COLUMN_NAME_ORDER,
	FILE_LIST_COLUMN_SIZE_ORDER,
	FILE_LIST_COLUMN_MODIFIED_ORDER,
	FILE_LIST_COLUMN_IS_FOLDER,
	FILE_LIST_COLUMN_IS_SELECTED
};


/* -- load_data  -- */


typedef struct {
	FrFileSelectorDialog *dialog;
	GFile                *folder;
	GCancellable         *cancellable;
	GList                *files;
	GList                *files_to_select;
} LoadData;


static LoadData *
load_data_new (FrFileSelectorDialog *dialog,
	       GFile                *folder)
{
	LoadData *load_data;

	load_data = g_slice_new0 (LoadData);
	load_data->dialog = g_object_ref (dialog);
	load_data->folder = _g_object_ref (folder);
	load_data->cancellable = g_cancellable_new ();
	load_data->files = NULL;

	return load_data;
}


static void
load_data_free (LoadData *load_data)
{
	if (load_data == NULL)
		return;

	g_object_unref (load_data->dialog);
	_g_object_unref (load_data->folder);
	g_object_unref (load_data->cancellable);
	file_info_list_free (load_data->files);
	_g_object_list_unref (load_data->files_to_select);
	g_slice_free (LoadData, load_data);
}


/* -- fr_file_selector_dialog -- */


struct _FrFileSelectorDialog {
	GtkDialog           parent_instance;
	GtkBuilder         *builder;
	GtkWidget          *extra_widget;
	GFile              *current_folder;
	LoadData           *current_operation;
	GSettings          *settings;
	gboolean            show_hidden;
	GSimpleActionGroup *action_map;
	GtkPopover         *file_context_menu;
	GtkWidget          *location_bar;
	FrFileSelectorMode  selection_mode;
	GtkWidget          *places_sidebar;
};


G_DEFINE_TYPE (FrFileSelectorDialog, fr_file_selector_dialog, GTK_TYPE_DIALOG)


static void
set_selection_mode (FrFileSelectorDialog *self,
		    FrFileSelectorMode    mode)
{
	self->selection_mode = mode;
	gtk_cell_renderer_set_visible (GTK_CELL_RENDERER (GET_WIDGET ("is_selected_cellrenderertoggle")),
				       (self->selection_mode == FR_FILE_SELECTOR_MODE_FILES));
	gtk_tree_view_column_set_visible (GTK_TREE_VIEW_COLUMN (GET_WIDGET ("treeviewcolumn_size")),
					  (self->selection_mode == FR_FILE_SELECTOR_MODE_FILES));
	g_simple_action_set_enabled (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->action_map), "select-all")), (self->selection_mode == FR_FILE_SELECTOR_MODE_FILES));
	g_simple_action_set_enabled (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->action_map), "deselect-all")), (self->selection_mode == FR_FILE_SELECTOR_MODE_FILES));
}


static void
fr_file_selector_set_property (GObject      *object,
			       guint         property_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
	FrFileSelectorDialog *self = FR_FILE_SELECTOR_DIALOG (object);

	switch (property_id) {
	case PROP_SELECTION_MODE:
		set_selection_mode (self, g_value_get_enum (value));
		break;

	default:
		break;
	}
}


static void
fr_file_selector_get_property (GObject    *object,
			       guint       property_id,
			       GValue     *value,
			       GParamSpec *pspec)
{
	FrFileSelectorDialog *self = FR_FILE_SELECTOR_DIALOG (object);

	switch (property_id) {
	case PROP_SELECTION_MODE:
		g_value_set_enum (value, self->selection_mode);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}


static void
fr_file_selector_dialog_finalize (GObject *object)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (object);

	g_signal_handlers_disconnect_by_data (g_volume_monitor_get (), self);

	g_object_unref (self->builder);
	_g_object_unref (self->current_folder);
	g_object_unref (self->settings);
	g_clear_object (&self->action_map);


	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


static void
set_current_folder (FrFileSelectorDialog *self,
		    GFile                *folder)
{
	if (folder != self->current_folder) {
		_g_object_unref (self->current_folder);
		self->current_folder = g_object_ref (folder);
	}

	if (self->current_folder == NULL)
		return;

	fr_location_bar_set_location (FR_LOCATION_BAR (self->location_bar), folder);
	fr_places_sidebar_set_location (FR_PLACES_SIDEBAR (self->places_sidebar), folder);
}


static void
fr_file_selector_dialog_get_default_size (FrFileSelectorDialog *self,
					  int                  *default_width,
					  int                  *default_height)
{
	int width, height;

	g_settings_get (self->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", &width, &height);
	if ((width > 0) && (height > 0)) {
		*default_width = width;
		*default_height = height;
		return;
	}

	*default_width = 300;
	*default_height = 300;

	if ((self->extra_widget != NULL) && gtk_widget_get_visible (self->extra_widget)) {
		GtkRequisition req;

		gtk_widget_get_preferred_size (GET_WIDGET ("extra_widget_container"),  &req, NULL);
		*default_height += gtk_box_get_spacing (GTK_BOX (GET_WIDGET ("content"))) + req.height;
	}
}


static void
_fr_file_selector_dialog_update_size (FrFileSelectorDialog *self)
{
	int default_width;
	int default_height;

	fr_file_selector_dialog_get_default_size (self, &default_width, &default_height);
	gtk_window_set_default_size (GTK_WINDOW (self), default_width, default_height);
}


static void
fr_file_selector_dialog_realize (GtkWidget *widget)
{
	FrFileSelectorDialog *self;
	int                   sidebar_size;

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->realize (widget);

	self = FR_FILE_SELECTOR_DIALOG (widget);
	_fr_file_selector_dialog_update_size (self);
	sidebar_size = g_settings_get_int (self->settings, PREF_FILE_SELECTOR_SIDEBAR_SIZE);
	if (sidebar_size <= 0)
		sidebar_size = 300;
	gtk_paned_set_position (GTK_PANED (GET_WIDGET ("main_paned")), sidebar_size);
}


static void
fr_file_selector_dialog_unmap (GtkWidget *widget)
{
	FrFileSelectorDialog *self;
	int                   width;
	int                   height;

	self = FR_FILE_SELECTOR_DIALOG (widget);

	gtk_widget_get_size_request (GTK_WIDGET (self), &width, &height);
	g_settings_set (self->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", width, height);
	g_settings_set_boolean (self->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN, self->show_hidden);
	g_settings_set_int (self->settings,
			    PREF_FILE_SELECTOR_SIDEBAR_SIZE,
			    gtk_paned_get_position (GTK_PANED (GET_WIDGET ("main_paned"))));

	int sorted_column;
	GtkSortType sort_type;
	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), &sorted_column, &sort_type);

	FrWindowSortMethod sort_method = FR_WINDOW_SORT_BY_NAME;
	if (sorted_column == FILE_LIST_COLUMN_SIZE)
		sort_method = FR_WINDOW_SORT_BY_SIZE;
	else if (sorted_column == FILE_LIST_COLUMN_MODIFIED)
		sort_method = FR_WINDOW_SORT_BY_TIME;
	g_settings_set_enum (self->settings, PREF_FILE_SELECTOR_SORT_METHOD, sort_method);
	g_settings_set_enum (self->settings, PREF_FILE_SELECTOR_SORT_TYPE, sort_type);

	if (self->current_operation != NULL)
		g_cancellable_cancel (self->current_operation->cancellable);

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->unmap (widget);
}



static void
fr_file_selector_dialog_class_init (FrFileSelectorDialogClass *klass)
{
	GObjectClass   *object_class;
	GtkWidgetClass *widget_class;

	object_class = (GObjectClass*) klass;
	object_class->set_property = fr_file_selector_set_property;
	object_class->get_property = fr_file_selector_get_property;
	object_class->finalize = fr_file_selector_dialog_finalize;

	widget_class = (GtkWidgetClass *) klass;
	widget_class->realize = fr_file_selector_dialog_realize;
	widget_class->unmap = fr_file_selector_dialog_unmap;

	g_object_class_install_property (object_class,
					 PROP_SELECTION_MODE,
					 g_param_spec_enum ("selection-mode",
							    "Selection mode",
							    "The selection mode",
							    FR_TYPE_FILE_SELECTOR_MODE,
							    FR_FILE_SELECTOR_MODE_FILES,
							    G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
}


static gint
files_name_column_sort_func (GtkTreeModel *model,
			     GtkTreeIter  *a,
			     GtkTreeIter  *b,
			     gpointer      user_data)
{
	GtkSortType  sort_order;
	char        *key_a;
	char        *key_b;
	gboolean     is_folder_a;
	gboolean     is_folder_b;
	gint         result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a,
			    FILE_LIST_COLUMN_NAME_ORDER, &key_a,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_a,
			    -1);
	gtk_tree_model_get (model, b,
			    FILE_LIST_COLUMN_NAME_ORDER, &key_b,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_b,
			    -1);

	if (is_folder_a == is_folder_b) {
		result = strcmp (key_a, key_b);
	}
	else {
		result = is_folder_a ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	g_free (key_a);
	g_free (key_b);

	return result;
}


static gint
files_size_column_sort_func (GtkTreeModel *model,
			     GtkTreeIter  *a,
			     GtkTreeIter  *b,
			     gpointer      user_data)
{
	GtkSortType  sort_order;
	char        *key_a;
	char        *key_b;
	gint64       size_a;
	gint64       size_b;
	gboolean     is_folder_a;
	gboolean     is_folder_b;
	int          result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a,
			    FILE_LIST_COLUMN_NAME_ORDER, &key_a,
			    FILE_LIST_COLUMN_SIZE_ORDER, &size_a,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_a,
			    -1);
	gtk_tree_model_get (model, b,
			    FILE_LIST_COLUMN_NAME_ORDER, &key_b,
			    FILE_LIST_COLUMN_SIZE_ORDER, &size_b,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_b,
			    -1);

	if (is_folder_a == is_folder_b) {
		if (is_folder_a) {
			result = strcmp (key_a, key_b);
			if (sort_order == GTK_SORT_DESCENDING)
				result = -1 * result;
		}
		else
			result = size_a - size_b;
	}
	else {
		result = is_folder_a ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	g_free (key_a);
	g_free (key_b);

	return result;
}


static gint
files_modified_column_sort_func (GtkTreeModel *model,
				 GtkTreeIter  *a,
				 GtkTreeIter  *b,
				 gpointer      user_data)
{
	GtkSortType sort_order;
	glong       modified_a;
	glong       modified_b;
	gboolean    is_folder_a;
	gboolean    is_folder_b;
	int         result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a,
			    FILE_LIST_COLUMN_MODIFIED_ORDER, &modified_a,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_a,
			    -1);
	gtk_tree_model_get (model, b,
			    FILE_LIST_COLUMN_MODIFIED_ORDER, &modified_b,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_b,
			    -1);

	if (is_folder_a == is_folder_b) {
		result = modified_a - modified_b;
	}
	else {
		result = is_folder_a ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	return result;
}


static gboolean
_fr_file_selector_dialog_is_file_checked (FrFileSelectorDialog *self)
{
	GtkListStore *list_store;
	GtkTreeIter   iter;

	list_store = GTK_LIST_STORE (GET_WIDGET ("files_liststore"));
	if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter))
		return FALSE;

	do {
		gboolean is_selected;

		gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
				    FILE_LIST_COLUMN_IS_SELECTED, &is_selected,
				    -1);
		if (is_selected)
			return TRUE;
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), &iter));

	return FALSE;
}


static gboolean
_fr_file_selector_dialog_is_file_selected (FrFileSelectorDialog *self)
{
	if (self->selection_mode == FR_FILE_SELECTOR_MODE_FILES)
		return _fr_file_selector_dialog_is_file_checked (self);
	else
		return self->current_folder != NULL;
}


static void
_update_sensitivity (FrFileSelectorDialog *self)
{
	gtk_dialog_set_response_sensitive (GTK_DIALOG (self),
					   GTK_RESPONSE_OK,
					   _fr_file_selector_dialog_is_file_selected (self));
}


static void
is_selected_cellrenderertoggle_toggled_cb (GtkCellRendererToggle *cell_renderer,
					   const gchar           *path,
					   gpointer               user_data)
{
	FrFileSelectorDialog *self = user_data;
	GtkListStore         *list_store;
	GtkTreePath          *tree_path;
	GtkTreeIter           iter;
	gboolean              is_selected;

	list_store = GTK_LIST_STORE (GET_WIDGET ("files_liststore"));
	tree_path = gtk_tree_path_new_from_string (path);
	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), &iter, tree_path)) {
		gtk_tree_path_free (tree_path);
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
			    FILE_LIST_COLUMN_IS_SELECTED, &is_selected,
			    -1);
	gtk_list_store_set (list_store, &iter,
			    FILE_LIST_COLUMN_IS_SELECTED, ! is_selected,
			    -1);

	_update_sensitivity (self);

	gtk_tree_path_free (tree_path);
}


static void
files_treeview_selection_changed_cb (GtkTreeView *tree_view,
				     gpointer     user_data)
{
	FrFileSelectorDialog *self = user_data;

	if (self->selection_mode != FR_FILE_SELECTOR_MODE_FOLDER)
		return;

	_update_sensitivity (self);
}


static void
files_treeview_row_activated_cb (GtkTreeView       *tree_view,
				 GtkTreePath       *path,
				 GtkTreeViewColumn *column,
				 gpointer           user_data)
{
	FrFileSelectorDialog *self = user_data;
	GtkTreeModel         *tree_model;
	GtkTreeIter           iter;
	GFile                *file;
	gboolean              is_folder;

	tree_model = gtk_tree_view_get_model (tree_view);
	if (! gtk_tree_model_get_iter (tree_model, &iter, path))
		return;

	gtk_tree_model_get (tree_model, &iter,
			    FILE_LIST_COLUMN_FILE, &file,
			    FILE_LIST_COLUMN_IS_FOLDER, &is_folder,
			    -1);
	if (is_folder)
		fr_file_selector_dialog_set_current_folder (self, file);

	g_object_unref (file);
}


static void
location_bar_changed_cb (FrLocationBar *location_bar,
			 gpointer       user_data)
{
	FrFileSelectorDialog *self = user_data;
	fr_file_selector_dialog_set_current_folder (self, fr_location_bar_get_location (location_bar));
}


static void
_set_current_folder (FrFileSelectorDialog *self,
		     GFile                *folder,
		     GList                *files);


static void
new_folder_dialog_response_cb (GtkDialog *dialog,
			       int        response_id,
			       gpointer   user_data)
{
	FrFileSelectorDialog *self = user_data;

	if (response_id != GTK_RESPONSE_YES) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		return;
	}

	char *filename = _gth_request_dialog_get_text (dialog);
	if (filename == NULL)
		return;

	char *reason = NULL;
	if (!_g_basename_is_valid (filename, NULL, &reason)) {
		GtkWidget *dlg;

		dlg = _gtk_error_dialog_new (GTK_WINDOW (dialog),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     NULL,
					     _("Could not create the folder"),
					     "%s",
					     reason);
		_gtk_dialog_run (GTK_DIALOG (dlg));

		g_free (reason);
		g_free (filename);
		return;
	}

	GFile  *new_folder = g_file_get_child_for_display_name (self->current_folder, filename, NULL);
	GError *error = NULL;
	if (!g_file_make_directory (new_folder, NULL, &error)) {
		_gtk_error_dialog_run (GTK_WINDOW (dialog), _("Could not create the folder"), "%s", error->message);
		g_error_free (error);
	}
	else {
		gtk_window_destroy (GTK_WINDOW (dialog));

		GList *selected_files = fr_file_selector_dialog_get_selected_files (self);
		_set_current_folder (self, new_folder, selected_files);
		_g_object_list_unref (selected_files);
	}

	g_object_unref (new_folder);
	g_free (filename);
}


static void
new_folder_button_clicked_cb (GtkButton *button,
			      gpointer   user_data)
{
	FrFileSelectorDialog *self = user_data;
	GtkWidget            *dialog;

	dialog = _gtk_request_dialog_new (GTK_WINDOW (self),
					  GTK_DIALOG_MODAL,
					  C_("Window title", "New Folder"),
					  _("_Name:"),
					  "",
					  1024,
					  _GTK_LABEL_CANCEL,
					  _GTK_LABEL_CREATE_ARCHIVE);
	g_signal_connect (dialog,
			  "response",
			  G_CALLBACK (new_folder_dialog_response_cb),
			  self);
	gtk_window_present (GTK_WINDOW (dialog));
}


static void
load_data_done (LoadData *load_data, GError *error)
{
	FrFileSelectorDialog *self = load_data->dialog;

	if ((error != NULL) && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		_gtk_error_dialog_run (GTK_WINDOW (self), _("Could not load the location"), "%s", error->message);
	if (self->current_operation == load_data)
		self->current_operation = NULL;
	load_data_free (load_data);
}


static void
volume_mount_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GError               *error = NULL;

	if (! g_volume_mount_finish (G_VOLUME (source_object),
				     result,
				     &error))
	{
		load_data_done (load_data, error);
		return;
	}

	// Try to load again.

	GMount *mount = g_volume_get_mount (G_VOLUME (source_object));
	if (mount == NULL) {
		error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED, "");
		load_data_done (load_data, error);
		return;
	}

	GFile *root = g_mount_get_root (mount);
	if (root == NULL) {
		error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED, "");
		load_data_done (load_data, error);
		return;
	}
	fr_file_selector_dialog_set_current_folder (self, root);

	load_data_free (load_data);
}


static void
mount_mountable_ready_cb (GObject      *source_object,
			  GAsyncResult *result,
			  gpointer      user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GError               *error = NULL;

	if (! g_file_start_mountable_finish (G_FILE (source_object),
					     result,
					     &error))
	{
		load_data_done (load_data, error);
		return;
	}

	// Try to load again.

	fr_file_selector_dialog_set_current_folder (self, load_data->folder);
	load_data_free (load_data);
}


static void
places_sidebar_open_cb (FrPlacesSidebar *sidebar,
			GFile           *location,
			GFileInfo       *info,
			gpointer         user_data)
{
	FrFileSelectorDialog *self = user_data;
	if ((info != NULL) && g_file_info_get_file_type (info) == G_FILE_TYPE_MOUNTABLE) {
		self->current_operation = load_data_new (self, location);

		GMountOperation *mount_op = gtk_mount_operation_new (GTK_WINDOW (self));
		GVolume *volume = (GVolume *) g_file_info_get_attribute_object (info, FR_FILE_ATTRIBUTE_VOLUME);
		if (volume != NULL) {
			g_volume_mount (volume,
					0,
					mount_op,
					self->current_operation->cancellable,
					volume_mount_ready_cb,
					self->current_operation);
		}
		else {
			g_file_mount_mountable (location,
						0,
						mount_op,
						self->current_operation->cancellable,
						mount_mountable_ready_cb,
						self->current_operation);
		}
		g_object_unref (mount_op);
		return;
	}
	fr_file_selector_dialog_set_current_folder (self, location);
}


static void
files_treeview_button_pressed_cb (GtkGestureClick *gesture,
				  gint             n_press,
				  gdouble          x,
				  gdouble          y,
				  gpointer         user_data)
{
	FrFileSelectorDialog *self = user_data;
	if (n_press == 1)
		_gtk_popover_popup_at_position (self->file_context_menu, x, y);
}


static void
select_all_files (FrFileSelectorDialog *self,
		  gboolean              value)
{
	GtkListStore *list_store;
	GtkTreeIter   iter;

	list_store = GTK_LIST_STORE (GET_WIDGET ("files_liststore"));
	if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter))
		return;

	do {
		gtk_list_store_set (list_store, &iter,
				    FILE_LIST_COLUMN_IS_SELECTED, value,
				    -1);
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), &iter));

	_update_sensitivity (self);
}


static void
select_all_activate_cb (GSimpleAction *action,
			  GVariant *state,
			  gpointer user_data)
{
	select_all_files (FR_FILE_SELECTOR_DIALOG (user_data), TRUE);
}


static void
unselect_all_activate_cb (GSimpleAction *action,
			  GVariant *state,
			  gpointer user_data)
{
	select_all_files (FR_FILE_SELECTOR_DIALOG (user_data), FALSE);
}


static void
show_hidden_files_toggled_cb (GSimpleAction *action,
			      GVariant *state,
			      gpointer user_data)
{
	FrFileSelectorDialog *self = user_data;
	GFile                *folder;
	GList                *selected_files;

	self->show_hidden = g_variant_get_boolean (state);
	folder = fr_file_selector_dialog_get_current_folder (self);
	selected_files = fr_file_selector_dialog_get_selected_files (self);
	_set_current_folder (self, folder, selected_files);

	g_simple_action_set_state (action, state);

	_g_object_list_unref (selected_files);
	_g_object_unref (folder);
}


static void
activate_toggle (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
	g_variant_unref (state);
}


static GActionEntry dlg_entries[] = {
	{
		.name = "select-all",
		.activate = select_all_activate_cb,
	},
	{
		.name = "deselect-all",
		.activate = unselect_all_activate_cb,
	},
	{
		.name = "show-hidden",
		.activate = activate_toggle,
		.state = "false",
		.change_state = show_hidden_files_toggled_cb,
	},
};


static void
fr_file_selector_dialog_init (FrFileSelectorDialog *self)
{
	self->current_folder = NULL;
	self->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "file-selector.ui");
	self->settings = g_settings_new ("org.gnome.FileRoller.FileSelector");
	self->show_hidden = g_settings_get_boolean (self->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN);
	self->action_map = g_simple_action_group_new ();
	self->file_context_menu = GTK_POPOVER (gtk_popover_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (self->builder, "file_list_context_menu_model"))));

	gtk_popover_set_offset (GTK_POPOVER (self->file_context_menu), 0, 30);
	gtk_box_prepend (GTK_BOX (GET_WIDGET ("file_list_container")), GTK_WIDGET (self->file_context_menu));

	_gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"), TRUE, TRUE);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME, files_name_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_SIZE, files_size_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_MODIFIED, files_modified_column_sort_func, self, NULL);

	int sorted_column = FILE_LIST_COLUMN_NAME;
	FrWindowSortMethod sort_method = g_settings_get_enum (self->settings, PREF_FILE_SELECTOR_SORT_METHOD);
	if (sort_method == FR_WINDOW_SORT_BY_SIZE)
		sorted_column = FILE_LIST_COLUMN_SIZE;
	else if (sort_method == FR_WINDOW_SORT_BY_TIME)
		sorted_column = FILE_LIST_COLUMN_MODIFIED;
	GtkSortType sort_type = g_settings_get_enum (self->settings, PREF_FILE_SELECTOR_SORT_TYPE);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")),
					      sorted_column,
					      sort_type);

	GtkWidget *location_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (location_box)), "toolbar");
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (location_box)), "locationbar");
	_gtk_box_pack_start (GTK_BOX (GET_WIDGET ("content_box")), location_box, TRUE, FALSE);

	self->location_bar = fr_location_bar_new ();
	g_signal_connect (self->location_bar,
			  "changed",
			  G_CALLBACK (location_bar_changed_cb),
			  self);
	_gtk_box_pack_start (GTK_BOX (location_box), self->location_bar, TRUE, FALSE);

	GtkWidget *new_folder_button = gtk_button_new_from_icon_name ("folder-new-symbolic");
	gtk_widget_set_tooltip_text (new_folder_button, _("New Folder"));
	g_signal_connect (new_folder_button,
			  "clicked",
			  G_CALLBACK (new_folder_button_clicked_cb),
			  self);
	_gtk_box_pack_end (GTK_BOX (location_box), new_folder_button, FALSE, FALSE);

	self->places_sidebar = fr_places_sidebar_new ();
	_gtk_box_pack_start (GTK_BOX (GET_WIDGET ("places_sidebar")), self->places_sidebar, TRUE, TRUE);
	g_signal_connect (self->places_sidebar,
			  "open",
			  G_CALLBACK (places_sidebar_open_cb),
			  self);

	g_signal_connect (GTK_CELL_RENDERER_TOGGLE (GET_WIDGET ("is_selected_cellrenderertoggle")),
			  "toggled",
			  G_CALLBACK (is_selected_cellrenderertoggle_toggled_cb),
			  self);
	g_signal_connect (GTK_TREE_VIEW (GET_WIDGET ("files_treeview")),
			  "row-activated",
			  G_CALLBACK (files_treeview_row_activated_cb),
			  self);
	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (GET_WIDGET ("files_treeview"))),
			  "changed",
			  G_CALLBACK (files_treeview_selection_changed_cb),
			  self);

	GtkGesture *gesture_click = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture_click), GDK_BUTTON_SECONDARY);
	g_signal_connect (gesture_click,
			  "pressed",
			  G_CALLBACK (files_treeview_button_pressed_cb),
			  self);
	gtk_widget_add_controller (GET_WIDGET ("files_treeview"), GTK_EVENT_CONTROLLER (gesture_click));

	g_action_map_add_action_entries (G_ACTION_MAP (self->action_map),
			dlg_entries, G_N_ELEMENTS (dlg_entries),
			self);
	gtk_widget_insert_action_group (GTK_WIDGET (self), "file-selector-dialog", G_ACTION_GROUP (self->action_map));

	g_simple_action_set_state (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->action_map), "show-hidden")), g_variant_new_boolean (self->show_hidden));

	_fr_file_selector_dialog_update_size (self);
	gtk_widget_grab_focus (GET_WIDGET ("files_treeview"));
}


GtkWidget *
fr_file_selector_dialog_new (FrFileSelectorMode  mode,
			     const char         *title,
			     GtkWindow          *parent)
{
	return (GtkWidget *) g_object_new (fr_file_selector_dialog_get_type (),
					   "selection-mode", mode,
					   "title", title,
					   "transient-for", parent,
					   "use-header-bar", _gtk_settings_get_dialogs_use_header (),
					   NULL);
}


void
fr_file_selector_dialog_set_extra_widget (FrFileSelectorDialog *self,
					  GtkWidget            *extra_widget)
{
	if (self->extra_widget != NULL)
		gtk_box_remove (GTK_BOX (GET_WIDGET ("extra_widget_container")), self->extra_widget);
	self->extra_widget = extra_widget;
	if (self->extra_widget != NULL)
		gtk_box_append (GTK_BOX (GET_WIDGET ("extra_widget_container")), self->extra_widget);
}


GtkWidget *
fr_file_selector_dialog_get_extra_widget (FrFileSelectorDialog *self)
{
	return self->extra_widget;
}


GSimpleActionGroup *
fr_file_selector_dialog_get_action_map (FrFileSelectorDialog *self)
{
	return self->action_map;
}


static gboolean
_g_date_time_same_day (GDateTime *dt1,
		       GDateTime *dt2)
{
	int y1, m1, d1;
	int y2, m2, d2;

	g_date_time_get_ymd (dt1, &y1, &m1, &d1);
	g_date_time_get_ymd (dt2, &y2, &m2, &d2);

	return (y1 == y2) && (m1 == m2) && (d1 == d2);
}


static void
_get_folder_list (LoadData *load_data);


static void
folder_mount_enclosing_volume_ready_cb (GObject      *source_object,
					GAsyncResult *result,
					gpointer      user_data)
{
	LoadData *load_data = user_data;
	GError *error = NULL;

	g_file_mount_enclosing_volume_finish (G_FILE (source_object), result, &error);

	if ((error != NULL) && ! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
		load_data_done (load_data, error);
		return;
	}

	_get_folder_list (load_data);
}


static void
get_folder_content_done_cb (GError   *error,
			    gpointer  user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GtkListStore         *list_store;
	GList                *scan;
	GtkTreeIter           iter;
	GDateTime            *today;
	int                   sort_column_id;
	GtkSortType           sort_order;
	GHashTable           *selected_files;

	if (error != NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
			GMountOperation *operation;

			operation = gtk_mount_operation_new (GTK_WINDOW (self));
			g_file_mount_enclosing_volume (load_data->folder,
						       G_MOUNT_MOUNT_NONE,
						       operation,
						       load_data->cancellable,
						       folder_mount_enclosing_volume_ready_cb,
						       load_data);

			g_object_unref (operation);
			return;
		}

		load_data_done (load_data, error);
		return;
	}

	load_data->files = g_list_reverse (load_data->files);

	today = g_date_time_new_now_local ();

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), &sort_column_id, &sort_order);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, 0);

	selected_files = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);
	for (scan = load_data->files_to_select; scan; scan = scan->next)
		g_hash_table_insert(selected_files, scan->data, GINT_TO_POINTER (1));

	list_store = GTK_LIST_STORE (GET_WIDGET ("files_liststore"));
	gtk_list_store_clear (list_store);
	for (scan = load_data->files; scan; scan = scan->next) {
		FileInfo  *file_info = scan->data;
		GIcon     *icon;
		char      *size;
		GTimeVal   timeval;
		GDateTime *datetime;
		char      *modified;
		char      *collate_key;
		gboolean   is_folder;

		if (! self->show_hidden && g_file_info_get_is_hidden (file_info->info))
			continue;

		gtk_list_store_append (list_store, &iter);

		icon = g_file_info_get_icon (file_info->info);
		size = g_format_size (g_file_info_get_size (file_info->info));
		g_file_info_get_modification_time (file_info->info, &timeval);
		datetime = g_date_time_new_from_timeval_local (&timeval);
		modified = g_date_time_format (datetime, _g_date_time_same_day (datetime, today) ? "%X" : "%x");
		collate_key = g_utf8_collate_key_for_filename (g_file_info_get_display_name (file_info->info), -1);
		is_folder = (g_file_info_get_file_type (file_info->info) == G_FILE_TYPE_DIRECTORY);

		gtk_list_store_set (list_store, &iter,
				    FILE_LIST_COLUMN_ICON, icon,
				    FILE_LIST_COLUMN_NAME, g_file_info_get_display_name (file_info->info),
				    FILE_LIST_COLUMN_SIZE, (is_folder ? "" : size),
				    FILE_LIST_COLUMN_MODIFIED, modified,
				    FILE_LIST_COLUMN_FILE, file_info->file,
				    FILE_LIST_COLUMN_NAME_ORDER, collate_key,
				    FILE_LIST_COLUMN_SIZE_ORDER, g_file_info_get_size (file_info->info),
				    FILE_LIST_COLUMN_MODIFIED_ORDER, timeval.tv_sec,
				    FILE_LIST_COLUMN_IS_FOLDER, is_folder,
				    FILE_LIST_COLUMN_IS_SELECTED, (g_hash_table_lookup (selected_files, file_info->file) != NULL),
				    -1);

		g_free (collate_key);
		g_free (modified);
		g_date_time_unref (datetime);
		g_free (size);
	}

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), sort_column_id, sort_order);
	set_current_folder (self, load_data->folder);
	_update_sensitivity (self);

	if (load_data->dialog->current_operation == load_data)
		load_data->dialog->current_operation = NULL;

	g_hash_table_unref (selected_files);
	g_date_time_unref (today);
	load_data_free (load_data);
}


static void
get_folder_content_for_each_child_cb (GFile     *file,
				      GFileInfo *info,
				      gpointer   user_data)
{
	LoadData *load_data = user_data;
	FileInfo *file_info;

	if (load_data->dialog->selection_mode == FR_FILE_SELECTOR_MODE_FOLDER)
		if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
			return;

	file_info = file_info_new (file, info);
	load_data->files = g_list_prepend (load_data->files, file_info);
}


static void
_get_folder_list (LoadData *load_data)
{
	g_directory_foreach_child (load_data->folder,
				   FALSE,
				   TRUE,
				   (G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				    G_FILE_ATTRIBUTE_STANDARD_NAME ","
				    G_FILE_ATTRIBUTE_STANDARD_SIZE ","
				    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
				    G_FILE_ATTRIBUTE_STANDARD_ICON ","
				    G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
				    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
				    G_FILE_ATTRIBUTE_TIME_MODIFIED ","
				    G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),
				   load_data->cancellable,
				   NULL,
				   get_folder_content_for_each_child_cb,
				   get_folder_content_done_cb,
				   load_data);
}


static void
_set_current_folder (FrFileSelectorDialog *self,
		     GFile                *folder,
		     GList                *files)
{
	if (self->current_operation != NULL)
		g_cancellable_cancel (self->current_operation->cancellable);

	gtk_list_store_clear (GTK_LIST_STORE (GET_WIDGET ("files_liststore")));
	_update_sensitivity (self);

	self->current_operation = load_data_new (self, folder);
	self->current_operation->files_to_select = _g_object_list_ref (files);
	_get_folder_list (self->current_operation);
}


void
fr_file_selector_dialog_set_current_folder (FrFileSelectorDialog *self,
					    GFile                *folder)
{
	g_return_if_fail (folder != NULL);
	_set_current_folder (self, folder, NULL);
}


GFile *
fr_file_selector_dialog_get_current_folder (FrFileSelectorDialog *self)
{
	return _g_object_ref (self->current_folder);
}


void
fr_file_selector_dialog_set_selected_files (FrFileSelectorDialog  *self,
					    GList                 *files)
{
	GFile *folder;

	if (files == NULL)
		return;

	folder = g_file_get_parent (G_FILE (files->data));
	_set_current_folder (self, folder, files);

	g_object_unref (folder);
}


GList *
fr_file_selector_dialog_get_selected_files (FrFileSelectorDialog *self)
{
	GtkListStore *list_store;
	GtkTreeIter   iter;
	GList        *list;

	list_store = GTK_LIST_STORE (GET_WIDGET ("files_liststore"));
	if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter))
		return NULL;

	list = NULL;
	do {
		GFile    *file;
		gboolean  is_selected;

		gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
				    FILE_LIST_COLUMN_FILE, &file,
				    FILE_LIST_COLUMN_IS_SELECTED, &is_selected,
				    -1);

		if (is_selected)
			list = g_list_prepend (list, g_object_ref (file));

		g_object_unref (file);
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), &iter));

	return g_list_reverse (list);
}
