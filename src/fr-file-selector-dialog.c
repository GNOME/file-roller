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


#define GET_WIDGET(x) (_gtk_builder_get_widget (self->priv->builder, (x)))
#define PREF_FILE_SELECTOR_WINDOW_SIZE "window-size"


G_DEFINE_TYPE (FrFileSelectorDialog, fr_file_selector_dialog, GTK_TYPE_DIALOG)


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


enum {
	PLACE_LIST_COLUMN_ICON,
	PLACE_LIST_COLUMN_NAME,
	PLACE_LIST_COLUMN_FILE,
	PLACE_LIST_COLUMN_IS_SEPARATOR
};


typedef struct {
	FrFileSelectorDialog *dialog;
	GFile                *folder;
	GCancellable         *cancellable;
	GList                *files;
} LoadData;


struct _FrFileSelectorDialogPrivate {
	GtkBuilder    *builder;
	GtkWidget     *extra_widget;
	GFile         *current_folder;
	LoadData      *current_operation;
	GthIconCache  *icon_cache;
	GSettings     *settings;
};


/* -- load_data  -- */


static LoadData *
load_data_new (FrFileSelectorDialog *dialog,
	       GFile                *folder)
{
	LoadData *load_data;

	load_data = g_new (LoadData, 1);
	load_data->dialog = g_object_ref (dialog);
	load_data->folder = g_object_ref (folder);
	load_data->cancellable = g_cancellable_new ();
	file_info_list_free (load_data->files);

	return load_data;
}


static void
load_data_free (LoadData *load_data)
{
	if (load_data == NULL)
		return;

	if (load_data->dialog->priv->current_operation == load_data)
		load_data->dialog->priv->current_operation = NULL;

	g_object_unref (load_data->dialog);
	g_object_unref (load_data->folder);
	g_object_unref (load_data->cancellable);
	g_free (load_data);
}


/* -- fr_file_selector_dialog -- */


static void
fr_file_selector_dialog_finalize (GObject *object)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (object);
	g_object_unref (self->priv->builder);
	_g_object_unref (self->priv->current_folder);
	gth_icon_cache_free (self->priv->icon_cache);
	g_object_unref (self->priv->settings);

	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


/* Taken from the Gtk+ file gtkfilechooserdefault.c
 * Copyright (C) 2003, Red Hat, Inc.  */


#define NUM_LINES 45
#define NUM_CHARS 60


/* Guesses a size based upon font sizes */
static void
find_good_size_from_style (GtkWidget *widget,
                           gint      *width,
                           gint      *height)
{
  GtkStyleContext *context;
  GtkStateFlags state;
  int font_size;
  GdkScreen *screen;
  double resolution;

  context = gtk_widget_get_style_context (widget);
  state = gtk_widget_get_state_flags (widget);

  screen = gtk_widget_get_screen (widget);
  if (screen)
    {
      resolution = gdk_screen_get_resolution (screen);
      if (resolution < 0.0) /* will be -1 if the resolution is not defined in the GdkScreen */
        resolution = 96.0;
    }
  else
    resolution = 96.0; /* wheeee */

  font_size = pango_font_description_get_size (gtk_style_context_get_font (context, state));
  font_size = PANGO_PIXELS (font_size) * resolution / 72.0;

  *width = font_size * NUM_CHARS;
  *height = font_size * NUM_LINES;
}


static void
fr_file_selector_dialog_get_default_size (FrFileSelectorDialog *self,
					  int                  *default_width,
					  int                  *default_height)
{
	int width, height;

	g_settings_get (self->priv->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", &width, &height);
	if ((width > 0) && (height > 0)) {
		*default_width = width;
		*default_height = height;
		return;
	}

	find_good_size_from_style (GTK_WIDGET (self), default_width, default_height);

	if ((self->priv->extra_widget != NULL) && gtk_widget_get_visible (self->priv->extra_widget)) {
		GtkRequisition req;

		gtk_widget_get_preferred_size (GET_WIDGET ("extra_widget_container"),  &req, NULL);
		*default_height += gtk_box_get_spacing (GTK_BOX (GET_WIDGET ("content"))) + req.height;
	}
}


static void
fr_file_selector_dialog_realize (GtkWidget *widget)
{
	FrFileSelectorDialog *self;
	int                   default_width;
	int                   default_height;

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->realize (widget);

	self = FR_FILE_SELECTOR_DIALOG (widget);

	fr_file_selector_dialog_get_default_size (self, &default_width, &default_height);
	gtk_window_set_default_size (GTK_WINDOW (self), default_width, default_height);

}


static void
fr_file_selector_dialog_unmap (GtkWidget *widget)
{
	FrFileSelectorDialog *self;
	int                   width;
	int                   height;

	self = FR_FILE_SELECTOR_DIALOG (widget);

	gtk_window_get_size (GTK_WINDOW (self), &width, &height);
	g_settings_set (self->priv->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", width, height);

	/* FIXME: cancel all operations */

	GTK_WIDGET_CLASS (fr_file_selector_dialog_parent_class)->unmap (widget);
}



static void
fr_file_selector_dialog_class_init (FrFileSelectorDialogClass *klass)
{
	GObjectClass   *object_class;
	GtkWidgetClass *widget_class;

	g_type_class_add_private (klass, sizeof (FrFileSelectorDialogPrivate));

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_file_selector_dialog_finalize;

	widget_class = (GtkWidgetClass *) klass;
	widget_class->realize = fr_file_selector_dialog_realize;
	widget_class->unmap = fr_file_selector_dialog_unmap;
}


static gint
compare_name_func (GtkTreeModel *model,
		   GtkTreeIter  *a,
		   GtkTreeIter  *b,
		   gpointer      user_data)
{
        char     *key_a;
        char     *key_b;
        gboolean  is_folder_a;
        gboolean  is_folder_b;
        gint      result;

        gtk_tree_model_get (model, a,
        		    FILE_LIST_COLUMN_NAME_ORDER, &key_a,
        		    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_a,
                            -1);
        gtk_tree_model_get (model, b,
        		    FILE_LIST_COLUMN_NAME_ORDER, &key_b,
        		    FILE_LIST_COLUMN_IS_FOLDER, &is_folder_b,
                            -1);

        if (is_folder_a == is_folder_b)
        	result = strcmp (key_a, key_b);
        else if (is_folder_a)
        	return -1;
        else
        	return 1;

        g_free (key_a);
        g_free (key_b);

        return result;
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

	gtk_tree_path_free (tree_path);
}


static void
fr_file_selector_dialog_init (FrFileSelectorDialog *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_FILE_SELECTOR_DIALOG, FrFileSelectorDialogPrivate);
	self->priv->current_folder = NULL;
	self->priv->builder = _gtk_builder_new_from_resource ("file-selector.ui");
	self->priv->icon_cache = gth_icon_cache_new_for_widget (GTK_WIDGET (self), GTK_ICON_SIZE_MENU);
	self->priv->settings = g_settings_new ("org.gnome.FileRoller.FileSelector");

	gtk_container_set_border_width (GTK_CONTAINER (self), 5);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"));

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME_ORDER, compare_name_func, self, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME_ORDER, GTK_SORT_ASCENDING);

	g_signal_connect (GET_WIDGET ("is_selected_cellrenderertoggle"),
			  "toggled",
			  G_CALLBACK (is_selected_cellrenderertoggle_toggled_cb),
			  self);
}


GtkWidget *
fr_file_selector_dialog_new (const char *title,
			     GtkWindow  *parent)
{
	return (GtkWidget *) g_object_new (FR_TYPE_FILE_SELECTOR_DIALOG,
					   "title", title,
					   "transient-for", parent,
					   NULL);
}


void
fr_file_selector_dialog_set_extra_widget (FrFileSelectorDialog *self,
					  GtkWidget            *extra_widget)
{
	if (self->priv->extra_widget != NULL)
		gtk_container_remove (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->priv->extra_widget);
	self->priv->extra_widget = extra_widget;
	gtk_container_add (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->priv->extra_widget);
}


GtkWidget *
fr_file_selector_dialog_get_extra_widget (FrFileSelectorDialog *self)
{
	return self->priv->extra_widget;
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
get_folder_content_done_cb (GError   *error,
		            gpointer  user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GtkListStore         *list_store;
	GList                *scan;
	GtkTreeIter           iter;
	GDateTime            *today;

	if (error != NULL) {
		g_warning ("%s", error->message);
		load_data_free (load_data);
		return;
	}

	load_data->files = g_list_reverse (load_data->files);

	today = g_date_time_new_now_local ();

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

		gtk_list_store_append (list_store, &iter);

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, g_file_info_get_icon (file_info->info));
		size = g_format_size (g_file_info_get_size (file_info->info));
		g_file_info_get_modification_time (file_info->info, &timeval);
		datetime = g_date_time_new_from_timeval_local (&timeval);
		modified = g_date_time_format (datetime, _g_date_time_same_day (datetime, today) ? "%X" : "%x");
		collate_key = g_utf8_collate_key_for_filename (g_file_info_get_display_name (file_info->info), -1);

		gtk_list_store_set (list_store, &iter,
				    FILE_LIST_COLUMN_ICON, icon_pixbuf,
				    FILE_LIST_COLUMN_NAME, g_file_info_get_display_name (file_info->info),
				    FILE_LIST_COLUMN_SIZE, size,
				    FILE_LIST_COLUMN_MODIFIED, modified,
				    FILE_LIST_COLUMN_FILE, file_info->file,
				    FILE_LIST_COLUMN_NAME_ORDER, collate_key,
				    FILE_LIST_COLUMN_SIZE_ORDER, g_file_info_get_size (file_info->info),
				    FILE_LIST_COLUMN_MODIFIED_ORDER, timeval.tv_sec,
				    FILE_LIST_COLUMN_IS_FOLDER, (g_file_info_get_file_type (file_info->info) == G_FILE_TYPE_DIRECTORY),
				    -1);

		g_free (collate_key);
		g_free (modified);
		g_date_time_unref (datetime);
		g_free (size);
		g_object_unref (icon_pixbuf);
	}

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
get_folder_content (LoadData *load_data)
{
	FrFileSelectorDialog *self = load_data->dialog;
	char                 *folder_name;

	folder_name = g_file_get_parse_name (load_data->folder);
	gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("location_entry")), folder_name);

	load_data->files = NULL;
	g_directory_foreach_child (load_data->folder,
				   FALSE,
				   TRUE,
			           (G_FILE_ATTRIBUTE_STANDARD_TYPE ","
			            G_FILE_ATTRIBUTE_STANDARD_NAME ","
			            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
			            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
			            G_FILE_ATTRIBUTE_STANDARD_ICON ","
			            G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
			            G_FILE_ATTRIBUTE_STANDARD_SORT_ORDER ","
			            G_FILE_ATTRIBUTE_TIME_MODIFIED ","
			            G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),
			           load_data->cancellable,
			           NULL,
			           get_folder_content_for_each_child_cb,
			           get_folder_content_done_cb,
				   load_data);

	g_free (folder_name);
}


void
fr_file_selector_dialog_set_current_folder (FrFileSelectorDialog *self,
					    GFile                *folder)
{
	g_return_if_fail (folder != NULL);

	_g_clear_object (&self->priv->current_folder);
	self->priv->current_folder = g_object_ref (folder);

	if (self->priv->current_operation != NULL)
		g_cancellable_cancel (self->priv->current_operation->cancellable);

	self->priv->current_operation = load_data_new (self, folder);
	get_folder_content (self->priv->current_operation);
}


GFile *
fr_file_selector_dialog_get_current_folder (FrFileSelectorDialog *self)
{
	return NULL;
}


void
fr_file_selector_dialog_set_selected_files (FrFileSelectorDialog  *self,
					    GList                 *files)
{

}


GList *
fr_file_selector_dialog_get_selected_files (FrFileSelectorDialog *self)
{
	return NULL;
}
