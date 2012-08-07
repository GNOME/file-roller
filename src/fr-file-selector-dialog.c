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
#include "gtk-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (self->priv->builder, (x)))


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
	GtkBuilder *builder;
	GtkWidget  *extra_widget;
	GFile      *current_folder;
	LoadData   *current_operation;
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

	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


static void
fr_file_selector_dialog_class_init (FrFileSelectorDialogClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (FrFileSelectorDialogPrivate));

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_file_selector_dialog_finalize;
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

	gtk_container_set_border_width (GTK_CONTAINER (self), 5);
	gtk_window_set_default_size (GTK_WINDOW (self), 830, 510); /* FIXME: find a good size */
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


static void
get_folder_content_done_cb (GError   *error,
		            gpointer  user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	int                   file_list_icon_size;
	GtkListStore         *list_store;
	GList                *scan;
	GtkTreeIter           iter;

	if (error != NULL) {
		g_warning ("%s", error->message);
		load_data_free (load_data);
		return;
	}

	file_list_icon_size = _gtk_widget_lookup_for_size (GTK_WIDGET (self), GTK_ICON_SIZE_MENU);
	load_data->files = g_list_reverse (load_data->files);

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

		icon_pixbuf = _g_icon_get_pixbuf (g_file_info_get_icon (file_info->info), file_list_icon_size, gtk_icon_theme_get_default ());
		size = g_format_size (g_file_info_get_size (file_info->info));
		g_file_info_get_modification_time (file_info->info, &timeval);
		datetime = g_date_time_new_from_timeval_local (&timeval);
		modified = g_date_time_format (datetime, "%x %X");
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
