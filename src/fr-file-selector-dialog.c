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
#include "fr-file-selector-dialog.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "gth-icon-cache.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (self->builder, (x)))
#define PREF_FILE_SELECTOR_WINDOW_SIZE "window-size"
#define PREF_FILE_SELECTOR_SHOW_HIDDEN "show-hidden"
#define PREF_FILE_SELECTOR_SIDEBAR_SIZE "sidebar-size"
#define FILE_LIST_LINES 45
#define FILE_LIST_CHARS 60
#define SIDEBAR_CHARS   12


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
	GtkDialog      parent_instance;
	GtkBuilder    *builder;
	GtkWidget     *extra_widget;
	GFile         *current_folder;
	LoadData      *current_operation;
	GthIconCache  *icon_cache;
	GSettings     *settings;
	gboolean       show_hidden;
};


G_DEFINE_TYPE (FrFileSelectorDialog, fr_file_selector_dialog, GTK_TYPE_DIALOG)


static void
fr_file_selector_dialog_finalize (GObject *object)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (object);

	g_signal_handlers_disconnect_by_data (g_volume_monitor_get (), self);

	g_object_unref (self->builder);
	_g_object_unref (self->current_folder);
	g_object_unref (self->settings);

	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


static void
set_current_folder (FrFileSelectorDialog *self,
		    GFile                *folder)
{
	char *folder_name;

	if (folder != self->current_folder) {
		_g_object_unref (self->current_folder);
		self->current_folder = g_object_ref (folder);
	}

	if (self->current_folder == NULL)
		return;

	folder_name = g_file_get_parse_name (folder);
	gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("location_entry")), folder_name);
	g_free (folder_name);

	gtk_places_sidebar_set_location (GTK_PLACES_SIDEBAR (GET_WIDGET ("places_sidebar")), folder);
}


/* Taken from the Gtk+ file gtkfilechooserdefault.c
 * Copyright (C) 2003, Red Hat, Inc.
 *
 * Changed by File-Roller authors
 *
 * Guesses a size based upon font sizes */
static int
get_font_size (GtkWidget *widget)
{
	GtkStyleContext      *context;
	GtkStateFlags         state;
	int                   font_size;
	GdkScreen            *screen;
	double                resolution;
	PangoFontDescription *font;

	context = gtk_widget_get_style_context (widget);
	state = gtk_widget_get_state_flags (widget);

	screen = gtk_widget_get_screen (widget);
	if (screen) {
		resolution = gdk_screen_get_resolution (screen);
		if (resolution < 0.0) /* will be -1 if the resolution is not defined in the GdkScreen */
			resolution = 96.0;
	}
	else
		resolution = 96.0; /* wheeee */

	gtk_style_context_get (context, state, "font", &font, NULL);
	font_size = pango_font_description_get_size (font);
	font_size = PANGO_PIXELS (font_size) * resolution / 72.0;

	return font_size;
}


static void
find_good_window_size_from_style (GtkWidget *widget,
				  int       *width,
				  int       *height)
{
	int font_size;

	font_size = get_font_size (widget);
	*width = font_size * FILE_LIST_CHARS;
	*height = font_size * FILE_LIST_LINES;
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

	find_good_window_size_from_style (GTK_WIDGET (self), default_width, default_height);

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
	GIcon                *icon;
	int                   sidebar_size;

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->realize (widget);

	self = FR_FILE_SELECTOR_DIALOG (widget);

	self->icon_cache = gth_icon_cache_new_for_widget (GTK_WIDGET (self), GTK_ICON_SIZE_MENU);
	icon = g_content_type_get_icon ("text/plain");
	gth_icon_cache_set_fallback (self->icon_cache, icon);
	g_object_unref (icon);

	_fr_file_selector_dialog_update_size (self);

	sidebar_size = g_settings_get_int (self->settings, PREF_FILE_SELECTOR_SIDEBAR_SIZE);
	if (sidebar_size <= 0)
		sidebar_size = get_font_size (widget) * SIDEBAR_CHARS;
	gtk_paned_set_position (GTK_PANED (GET_WIDGET ("main_paned")), sidebar_size);
}


static void
fr_file_selector_dialog_unrealize (GtkWidget *widget)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (widget);

	gth_icon_cache_free (self->icon_cache);
	self->icon_cache = NULL;

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->unrealize (widget);
}


static void
fr_file_selector_dialog_unmap (GtkWidget *widget)
{
	FrFileSelectorDialog *self;
	int                   width;
	int                   height;

	self = FR_FILE_SELECTOR_DIALOG (widget);

	gtk_window_get_size (GTK_WINDOW (self), &width, &height);
	g_settings_set (self->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", width, height);
	g_settings_set_boolean (self->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN, self->show_hidden);
	g_settings_set_int (self->settings,
			    PREF_FILE_SELECTOR_SIDEBAR_SIZE,
			    gtk_paned_get_position (GTK_PANED (GET_WIDGET ("main_paned"))));

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
	object_class->finalize = fr_file_selector_dialog_finalize;

	widget_class = (GtkWidgetClass *) klass;
	widget_class->realize = fr_file_selector_dialog_realize;
	widget_class->unrealize = fr_file_selector_dialog_unrealize;
	widget_class->unmap = fr_file_selector_dialog_unmap;
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
_fr_file_selector_dialog_is_file_selected (FrFileSelectorDialog *self)
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


static void
_update_sensitivity (FrFileSelectorDialog *self)
{
	gtk_dialog_set_response_sensitive (GTK_DIALOG (self),
					   GTK_RESPONSE_OK,
					   _fr_file_selector_dialog_is_file_selected (self));
}


static void
is_selected_cellrenderertoggle_toggled_cb (GtkCellRendererToggle *cell_renderer,
					   gchar                 *path,
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
go_up_button_clicked_cb (GtkButton *button,
			 gpointer   user_data)
{
	FrFileSelectorDialog *self = user_data;
	GFile                *parent;

	if (self->current_folder == NULL)
		return;

	parent = g_file_get_parent (self->current_folder);
	if (parent == NULL)
		return;

	fr_file_selector_dialog_set_current_folder (self, parent);

	g_object_unref (parent);
}


static void
places_sidebar_open_location_cb (GtkPlacesSidebar  *sidebar,
				 GObject           *location,
				 GtkPlacesOpenFlags open_flags,
				 gpointer           user_data)
{
	FrFileSelectorDialog *self = user_data;

	fr_file_selector_dialog_set_current_folder (self, G_FILE (location));
}


static gboolean
files_treeview_button_press_event_cb (GtkWidget      *widget,
				      GdkEventButton *event,
				      gpointer        user_data)
{
	FrFileSelectorDialog *self = user_data;

	if (event->button == 3) {
		gtk_menu_popup_at_pointer (GTK_MENU (GET_WIDGET ("file_list_context_menu")),  (GdkEvent *) event);

		return TRUE;
	}

	return FALSE;
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
}


static void
select_all_menuitem_activate_cb (GtkMenuItem *menu_item,
			  	 gpointer     user_data)
{
	select_all_files (FR_FILE_SELECTOR_DIALOG (user_data), TRUE);
}


static void
unselect_all_menuitem_activate_cb (GtkMenuItem *menu_item,
				   gpointer     user_data)
{
	select_all_files (FR_FILE_SELECTOR_DIALOG (user_data), FALSE);
}


static void
_set_current_folder (FrFileSelectorDialog *self,
		     GFile                *folder,
		     GList                *files);


static void
show_hidden_files_menuitem_toggled_cb (GtkCheckMenuItem *checkmenuitem,
				       gpointer          user_data)
{
	FrFileSelectorDialog *self = user_data;
	GFile                *folder;
	GList                *selected_files;

	self->show_hidden = gtk_check_menu_item_get_active (checkmenuitem);
	folder = fr_file_selector_dialog_get_current_folder (self);
	selected_files = fr_file_selector_dialog_get_selected_files (self);
	_set_current_folder (self, folder, selected_files);

	_g_object_list_unref (selected_files);
	_g_object_unref (folder);
}


static void
fr_file_selector_dialog_init (FrFileSelectorDialog *self)
{
	self->current_folder = NULL;
	self->builder = _gtk_builder_new_from_resource ("file-selector.ui");
	self->icon_cache = NULL;
	self->settings = g_settings_new ("org.gnome.FileRoller.FileSelector");
	self->show_hidden = g_settings_get_boolean (self->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN);

	gtk_container_set_border_width (GTK_CONTAINER (self), 5);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"), TRUE, TRUE, 0);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME, files_name_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_SIZE, files_size_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_MODIFIED, files_modified_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME, GTK_SORT_ASCENDING);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (GET_WIDGET ("show_hidden_files_menuitem")), self->show_hidden);

	g_signal_connect (GET_WIDGET ("is_selected_cellrenderertoggle"),
			  "toggled",
			  G_CALLBACK (is_selected_cellrenderertoggle_toggled_cb),
			  self);
	g_signal_connect (GET_WIDGET ("files_treeview"),
			  "row-activated",
			  G_CALLBACK (files_treeview_row_activated_cb),
			  self);
	g_signal_connect (GET_WIDGET ("go_up_button"),
			  "clicked",
			  G_CALLBACK (go_up_button_clicked_cb),
			  self);
	g_signal_connect (GET_WIDGET ("places_sidebar"),
			  "open-location",
			  G_CALLBACK (places_sidebar_open_location_cb),
			  self);
	g_signal_connect (GET_WIDGET ("files_treeview"),
			  "button-press-event",
			  G_CALLBACK (files_treeview_button_press_event_cb),
			  self);
	g_signal_connect (GET_WIDGET ("select_all_menuitem"),
			  "activate",
			  G_CALLBACK (select_all_menuitem_activate_cb),
			  self);
	g_signal_connect (GET_WIDGET ("unselect_all_menuitem"),
			  "activate",
			  G_CALLBACK (unselect_all_menuitem_activate_cb),
			  self);
	g_signal_connect (GET_WIDGET ("show_hidden_files_menuitem"),
			  "toggled",
			  G_CALLBACK (show_hidden_files_menuitem_toggled_cb),
			  self);

	_fr_file_selector_dialog_update_size (self);
	gtk_widget_grab_focus (GET_WIDGET ("files_treeview"));
}


GtkWidget *
fr_file_selector_dialog_new (const char *title,
			     GtkWindow  *parent)
{
	return (GtkWidget *) g_object_new (fr_file_selector_dialog_get_type (),
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
		gtk_container_remove (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->extra_widget);
	self->extra_widget = extra_widget;
	if (self->extra_widget != NULL)
		gtk_container_add (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->extra_widget);
}


GtkWidget *
fr_file_selector_dialog_get_extra_widget (FrFileSelectorDialog *self)
{
	return self->extra_widget;
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
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GError               *error = NULL;

	g_file_mount_enclosing_volume_finish (G_FILE (source_object), result, &error);

	if ((error != NULL) && ! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			_gtk_error_dialog_run (GTK_WINDOW (self), _("Could not load the location"), "%s", error->message);

		if (load_data->dialog->current_operation == load_data)
			load_data->dialog->current_operation = NULL;
		load_data_free (load_data);

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

		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			_gtk_error_dialog_run (GTK_WINDOW (self), _("Could not load the location"), "%s", error->message);

		if (load_data->dialog->current_operation == load_data)
			load_data->dialog->current_operation = NULL;
		load_data_free (load_data);

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
		GdkPixbuf *icon_pixbuf;
		char      *size;
		GTimeVal   timeval;
		GDateTime *datetime;
		char      *modified;
		char      *collate_key;
		gboolean   is_folder;

		if (! self->show_hidden && g_file_info_get_is_hidden (file_info->info))
			continue;

		gtk_list_store_append (list_store, &iter);

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->icon_cache, g_file_info_get_icon (file_info->info));
		size = g_format_size (g_file_info_get_size (file_info->info));
		g_file_info_get_modification_time (file_info->info, &timeval);
		datetime = g_date_time_new_from_timeval_local (&timeval);
		modified = g_date_time_format (datetime, _g_date_time_same_day (datetime, today) ? "%X" : "%x");
		collate_key = g_utf8_collate_key_for_filename (g_file_info_get_display_name (file_info->info), -1);
		is_folder = (g_file_info_get_file_type (file_info->info) == G_FILE_TYPE_DIRECTORY);

		gtk_list_store_set (list_store, &iter,
				    FILE_LIST_COLUMN_ICON, icon_pixbuf,
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
		_g_object_unref (icon_pixbuf);
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
