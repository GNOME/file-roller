/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2008 Free Software Foundation, Inc.
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
#include <gtk/gtk.h>
#include "dlg-update.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "fr-init.h"
#include "fr-window.h"


typedef enum {
	DIALOG_RESPONSE_UPDATE = 1
} DialogResponse;


enum {
	IS_SELECTED_COLUMN,
	NAME_COLUMN,
	DATA_COLUMN,
	N_COLUMNS
};

typedef struct {
	FrWindow     *window;
	GtkBuilder   *builder;

	GtkWidget    *update_file_dialog;

	GtkWidget    *update_files_dialog;
	GtkWidget    *update_files_primary_text_label;
	GtkWidget    *update_files_secondary_text_label;
	GtkWidget    *update_files_treeview;

	GList        *file_list;
	GtkTreeModel *list_model;
} DialogData;


static void
update_dialog_destroy (GtkWidget  *widget,
		       DialogData *data)
{
	fr_window_update_dialog_closed (data->window);
	g_object_unref (data->window);
	g_object_unref (data->builder);
	if (data->file_list != NULL)
		g_list_free (data->file_list);
	g_free (data);
}


static GList*
get_selected_files (DialogData *data)
{
	GList       *selection = NULL;
	GtkTreeIter  iter;

	if (! gtk_tree_model_get_iter_first (data->list_model, &iter))
		return NULL;

	do {
		gboolean  is_selected;
		OpenFile *file;

                gtk_tree_model_get (data->list_model, &iter,
                		    IS_SELECTED_COLUMN, &is_selected,
                		    DATA_COLUMN, &file,
                		    -1);
                if (is_selected)
                	selection = g_list_prepend (selection, file);
	} while (gtk_tree_model_iter_next (data->list_model, &iter));

	return g_list_reverse (selection);
}


static void
execute_update (DialogData *data)
{
	GList *selection;

	selection = get_selected_files (data);
	if (fr_window_update_files (data->window, selection)) {
		int n_files;

		n_files = g_list_length (data->file_list);
		if (n_files == 1)
			gtk_window_destroy (GTK_WINDOW (data->update_file_dialog));
		else
			gtk_window_destroy (GTK_WINDOW (data->update_files_dialog));
	}
	if (selection != NULL)
		g_list_free (selection);
}


static void
update_file_list (DialogData *data)
{
	gboolean     n_files;
	GList       *scan;
	GtkTreeIter  iter;

	/*gtk_window_set_modal (GTK_WINDOW (data->update_files_dialog), FALSE);*/
	gtk_widget_hide (data->update_files_dialog);
	/*gtk_window_set_modal (GTK_WINDOW (data->update_file_dialog), FALSE);*/
	gtk_widget_hide (data->update_file_dialog);

	if (data->file_list == NULL) {
		return;
	}

	n_files = g_list_length (data->file_list);

	/* update the file list */

	gtk_list_store_clear (GTK_LIST_STORE (data->list_model));
	for (scan = data->file_list; scan; scan = scan->next) {
		char     *display_name;
		OpenFile *file = scan->data;

		gtk_list_store_append (GTK_LIST_STORE (data->list_model),
				       &iter);

		display_name = _g_file_get_display_basename (file->extracted_file);
		gtk_list_store_set (GTK_LIST_STORE (data->list_model),
				    &iter,
				    IS_SELECTED_COLUMN, TRUE,
				    NAME_COLUMN, display_name,
				    DATA_COLUMN, file,
				    -1);

		g_free (display_name);
	}

	/* update the labels */

	if (n_files == 1) {
		OpenFile *file = data->file_list->data;
		char     *file_name;
		char     *archive_name;
		char     *label;
		char     *markup;

		/* primary text */

		file_name = _g_file_get_display_basename (file->extracted_file);
		archive_name = _g_file_get_display_name (fr_window_get_archive_file (data->window));
		label = g_markup_printf_escaped (_("Update the file “%s” in the archive “%s”?"), file_name, archive_name);
		markup = g_strdup_printf ("<big><b>%s</b></big>", label);
		/*gtk_label_set_markup (GTK_LABEL (data->update_file_primary_text_label), markup);*/
		g_object_set (data->update_file_dialog , "text", markup, "use-markup", TRUE, NULL);

		g_free (markup);
		g_free (label);
		g_free (archive_name);
		g_free (file_name);

		/* secondary text */

		label = g_strdup_printf (ngettext ("The file has been modified with an external application. If you don’t update the file in the archive, all of your changes will be lost.",
						   "%d files have been modified with an external application. If you don’t update the files in the archive, all of your changes will be lost.",
						   n_files),
					 n_files);
		/*gtk_label_set_text (GTK_LABEL (data->update_file_secondary_text_label), label);*/
		g_object_set (data->update_file_dialog , "secondary-text", label, NULL);
		g_free (label);
	}
	else if (n_files > 1) {
		char *archive_name;
		char *label;
		char *markup;

		/* primary text */

		archive_name = _g_file_get_display_name (fr_window_get_archive_file (data->window));
		label = g_markup_printf_escaped (_("Update the files in the archive “%s”?"), archive_name);
		markup = g_strdup_printf ("<big><b>%s</b></big>", label);
		gtk_label_set_markup (GTK_LABEL (data->update_files_primary_text_label), markup);

		g_free (markup);
		g_free (label);
		g_free (archive_name);

		/* secondary text */

		label = g_strdup_printf (ngettext ("The file has been modified with an external application. If you don’t update the file in the archive, all of your changes will be lost.",
						   "%d files have been modified with an external application. If you don’t update the files in the archive, all of your changes will be lost.",
						   n_files),
					 n_files);
		gtk_label_set_text (GTK_LABEL (data->update_files_secondary_text_label), label);
		g_free (label);
	}

	/* show the appropriate dialog */

	if (n_files == 1) {
		/*gtk_window_set_modal (GTK_WINDOW (data->update_file_dialog), TRUE);*/
		gtk_window_present (GTK_WINDOW (data->update_file_dialog));
	}
	else if (n_files > 1) {
		/*gtk_window_set_modal (GTK_WINDOW (data->update_files_dialog), TRUE);*/
		gtk_window_present (GTK_WINDOW (data->update_files_dialog));
	}
}


static int
n_selected (DialogData *data)
{
	int         n = 0;
	GtkTreeIter iter;

	if (! gtk_tree_model_get_iter_first (data->list_model, &iter))
		return 0;

	do {
		gboolean is_selected;
                gtk_tree_model_get (data->list_model, &iter, IS_SELECTED_COLUMN, &is_selected, -1);
                if (is_selected)
                	n++;
	} while (gtk_tree_model_iter_next (data->list_model, &iter));

	return n;
}


static void
is_selected_toggled (GtkCellRendererToggle *cell,
		     const char            *path_string,
		     gpointer               callback_data)
{
	DialogData   *data  = callback_data;
	GtkTreeModel *model = GTK_TREE_MODEL (data->list_model);
	GtkTreeIter   iter;
	GtkTreePath  *path = gtk_tree_path_new_from_string (path_string);
	guint         value;

	gtk_tree_model_get_iter (model, &iter, path);
	value = ! gtk_cell_renderer_toggle_get_active (cell);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, IS_SELECTED_COLUMN, value, -1);

	gtk_tree_path_free (path);

	gtk_dialog_set_response_sensitive (GTK_DIALOG( data->update_files_dialog), DIALOG_RESPONSE_UPDATE, n_selected (data) > 0);
}


static void
update_dialog_response (GtkDialog   *dialog,
			int          response_id,
			DialogData  *data)
{
	switch (response_id) {
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		gtk_window_destroy (GTK_WINDOW (dialog));
		break;

	case DIALOG_RESPONSE_UPDATE:
		execute_update (data);
		break;

	default:
		break;
	}
}


gpointer
dlg_update (FrWindow *window)
{
	DialogData        *data;
	GtkWidget         *content;
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;

	data = g_new0 (DialogData, 1);

	data->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "update.ui");

	data->file_list = NULL;
	data->window = g_object_ref (window);

	/* Get the widgets. */

	data->update_file_dialog = _gtk_message_dialog_new (GTK_WINDOW (window),
							    0,
							    "",
							    NULL,
							    _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
							    _("_Update"), DIALOG_RESPONSE_UPDATE,
							    NULL);

	data->update_files_dialog = g_object_new (GTK_TYPE_DIALOG,
						  "title", C_("Window title", "Update"),
						  "transient-for", GTK_WINDOW (window),
						  "use-header-bar", _gtk_settings_get_dialogs_use_header (),
						  NULL);
	gtk_window_set_default_size (GTK_WINDOW (data->update_files_dialog), 700, 500);
	content = _gtk_builder_get_widget (data->builder, "update_files_dialog_content");
	gtk_widget_set_vexpand (content, TRUE);
	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data->update_files_dialog))), content);

	gtk_dialog_add_button (GTK_DIALOG (data->update_files_dialog ), _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (data->update_files_dialog ), _("_Update"), DIALOG_RESPONSE_UPDATE);

	data->update_files_primary_text_label = _gtk_builder_get_widget (data->builder, "update_files_primary_text_label");
	data->update_files_secondary_text_label = _gtk_builder_get_widget (data->builder, "update_files_secondary_text_label");
	data->update_files_treeview = _gtk_builder_get_widget (data->builder, "update_files_treeview");

	/* Set the signals handlers. */

	g_signal_connect (GTK_DIALOG (data->update_file_dialog),
			  "destroy",
			  G_CALLBACK (update_dialog_destroy),
			  data);
	g_signal_connect (GTK_DIALOG (data->update_file_dialog),
			  "response",
			  G_CALLBACK (update_dialog_response),
			  data);
	g_signal_connect (GTK_DIALOG (data->update_files_dialog),
			  "destroy",
			  G_CALLBACK (update_dialog_destroy),
			  data);
	g_signal_connect (GTK_DIALOG (data->update_files_dialog),
			  "response",
			  G_CALLBACK (update_dialog_response),
			  data);

	/* Set dialog data. */

	data->list_model = GTK_TREE_MODEL (gtk_list_store_new (N_COLUMNS,
							       G_TYPE_BOOLEAN,
							       G_TYPE_STRING,
							       G_TYPE_POINTER));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->list_model),
					      NAME_COLUMN,
					      GTK_SORT_ASCENDING);
	gtk_tree_view_set_model (GTK_TREE_VIEW (data->update_files_treeview),
				 data->list_model);
	g_object_unref (G_OBJECT (data->list_model));

	column = gtk_tree_view_column_new ();

	renderer = gtk_cell_renderer_toggle_new ();
        g_signal_connect (GTK_CELL_RENDERER_TOGGLE (renderer),
                          "toggled",
                          G_CALLBACK (is_selected_toggled),
                          data);
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "active", IS_SELECTED_COLUMN,
                                             NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "text", NAME_COLUMN,
					     NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (data->update_files_treeview), column);

	/* Run dialog. */

	gtk_window_set_transient_for (GTK_WINDOW (data->update_file_dialog),
				      GTK_WINDOW (window));
	gtk_window_set_transient_for (GTK_WINDOW (data->update_files_dialog),
				      GTK_WINDOW (window));

	update_file_list (data);

	return data;
}


void
dlg_update_add_file (gpointer  dialog,
		     OpenFile *file)
{
	DialogData *data = dialog;
	GList      *scan;

	/* avoid duplicates */

	for (scan = data->file_list; scan; scan = scan->next) {
		OpenFile *test = scan->data;
		if (_g_file_cmp_uris (test->extracted_file, file->extracted_file) == 0)
			return;
	}

	/**/

	data->file_list = g_list_append (data->file_list, file);
	update_file_list (data);
}
