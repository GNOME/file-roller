/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
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
#include <gtk/gtk.h>
#include <gnome.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "file-utils.h"
#include "gconf-utils.h"
#include "main.h"
#include "window.h"


#define GLADE_FILE "file_roller.glade2"
#define TEMP_DOCS  "temp_docs"


typedef struct {
	FRWindow     *window;
	GladeXML     *gui;

	GtkWidget    *dialog;
	GtkWidget    *o_app_tree_view;
	GtkWidget    *o_recent_tree_view;
	GtkWidget    *o_app_entry;
	GtkWidget    *o_del_button;

	GList        *app_list;
	GList        *file_list;

	GtkTreeModel *app_model;
	GtkTreeModel *recent_model;
} DialogData;


/* called when the main dialog is closed. */
static void
open_with__destroy_cb (GtkWidget  *widget, 
		       DialogData *data)
{
        g_object_unref (G_OBJECT (data->gui));

	if (data->app_list)
		gnome_vfs_mime_application_list_free (data->app_list);

	if (data->file_list != NULL)
		path_list_free (data->file_list);

	g_free (data);
}


static void
open_cb (GtkWidget *widget,
	 gpointer   callback_data)
{
	DialogData  *data = callback_data;
	const char  *application;
	gboolean     present = FALSE;
	char        *command = NULL;
	GList       *scan;
	GSList      *sscan, *editors;

	application = gtk_entry_get_text (GTK_ENTRY (data->o_app_entry));

	/* add the command to the editors list if not already present. */

	for (scan = data->app_list; scan && !present; scan = scan->next) {
		GnomeVFSMimeApplication *app = scan->data;
		if (strcmp (app->command, application) == 0) {
			command = application_get_command (app);
			present = TRUE;
		}
	}

	editors = eel_gconf_get_string_list (PREF_EDIT_EDITORS);
	for (sscan = editors; sscan && ! present; sscan = sscan->next) {
		char *recent_command = sscan->data;
		if (strcmp (recent_command, application) == 0) {
			command = g_strdup (recent_command);
			present = TRUE;
		}
	}

	if (! present) {
		editors = g_slist_prepend (editors, g_strdup (application));
		command = g_strdup (application);
		eel_gconf_set_string_list (PREF_EDIT_EDITORS, editors);
	}

	g_slist_foreach (editors, (GFunc) g_free, NULL);
	g_slist_free (editors);

	/* exec the application */

	if (command != NULL) {
		window_open_files (data->window,
				   command,
				   data->file_list);
		
		g_free (command);
	}

	gtk_widget_destroy (data->dialog);
}


static int
app_button_press_cb (GtkWidget      *widget, 
		     GdkEventButton *event,
		     gpointer        callback_data)
{
        DialogData              *data = callback_data;
	GtkTreePath             *path;
	GtkTreeIter              iter;
	GnomeVFSMimeApplication *app;

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (data->o_app_tree_view),
					     event->x, event->y,
					     &path, NULL, NULL, NULL))
		return FALSE;

	if (! gtk_tree_model_get_iter (data->app_model, &iter, path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}
	gtk_tree_path_free (path);

	gtk_tree_model_get (data->app_model, &iter,
			    1, &app,
			    -1);
	gtk_entry_set_text (GTK_ENTRY (data->o_app_entry), app->command);

	return FALSE;
}


static void
app_activated_cb (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  gpointer           callback_data)
{
        DialogData              *data = callback_data;
	GtkTreeIter              iter;
	GnomeVFSMimeApplication *app;

	if (! gtk_tree_model_get_iter (data->app_model, &iter, path)) 
		return;
	
	gtk_tree_model_get (data->app_model, &iter,
			    1, &app,
			    -1);

	gtk_entry_set_text (GTK_ENTRY (data->o_app_entry), app->command);

	open_cb (NULL, data);
}


static int
recent_button_press_cb (GtkWidget      *widget, 
			GdkEventButton *event,
			gpointer        callback_data)
{
        DialogData  *data = callback_data;
	GtkTreePath *path;
	GtkTreeIter  iter;
	gchar       *editor;

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (data->o_recent_tree_view),
					     event->x, event->y,
					     &path, NULL, NULL, NULL))
		return FALSE;

	if (! gtk_tree_model_get_iter (data->recent_model, &iter, path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	gtk_tree_model_get (data->recent_model, &iter,
			    0, &editor,
			    -1);
	gtk_entry_set_text (GTK_ENTRY (data->o_app_entry), editor);
	g_free (editor);
	gtk_tree_path_free (path);

	return FALSE;
}


static void
recent_activated_cb (GtkTreeView       *tree_view,
		     GtkTreePath       *path,
		     GtkTreeViewColumn *column,
		     gpointer           callback_data)
{
        DialogData   *data = callback_data;
	GtkTreeIter   iter;
	char         *editor;

	if (! gtk_tree_model_get_iter (data->recent_model, &iter, path)) 
		return;
	
	gtk_tree_model_get (data->recent_model, &iter,
			    0, &editor,
			    -1);
	gtk_entry_set_text (GTK_ENTRY (data->o_app_entry), editor);
	g_free (editor);

	open_cb (NULL, data);
}


static void
delete_recent_cb (GtkWidget *widget,
		  gpointer   callback_data)
{
	DialogData       *data = callback_data;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;
	char             *editor;
	GSList           *editors;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->o_recent_tree_view));
	if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (data->recent_model, &iter,
			    0, &editor,
			    -1);
	gtk_list_store_remove (GTK_LIST_STORE (data->recent_model), &iter);

	/**/

	editors = eel_gconf_get_string_list (PREF_EDIT_EDITORS);
	editors = g_slist_remove (editors, editor);
	eel_gconf_set_string_list (PREF_EDIT_EDITORS, editors);
	g_slist_foreach (editors, (GFunc) g_free, NULL);
	g_slist_free (editors);

	/**/

	g_free (editor);
}


/* create the "open with" dialog. */
void
dlg_open_with (FRWindow *window, 
	       GList    *file_list)
{
	DialogData              *data;
	GnomeVFSMimeApplication *app;
	GList                   *scan, *app_names = NULL;
	GSList                  *sscan, *editors;
	GtkWidget               *ok_button;
	GtkWidget               *cancel_button;
	GtkTreeIter              iter;
	GtkCellRenderer         *renderer;
	GtkTreeViewColumn       *column;

	if (file_list == NULL)
		return;

	data = g_new (DialogData, 1);

	data->file_list = path_list_dup (file_list);
	data->window = window;
	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
        if (! data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

	/* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "open_with_dialog");
	data->o_app_tree_view = glade_xml_get_widget (data->gui, "o_app_list_tree_view");
	data->o_recent_tree_view = glade_xml_get_widget (data->gui, "o_recent_tree_view");
	data->o_app_entry = glade_xml_get_widget (data->gui, "o_app_entry");
	data->o_del_button = glade_xml_get_widget (data->gui, "o_del_button");
	ok_button = glade_xml_get_widget (data->gui, "o_ok_button");
	cancel_button = glade_xml_get_widget (data->gui, "o_cancel_button");

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (open_with__destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (data->o_app_tree_view), 
			  "button_press_event",
			  G_CALLBACK (app_button_press_cb), 
			  data);
	g_signal_connect (G_OBJECT (data->o_app_tree_view),
                          "row_activated",
                          G_CALLBACK (app_activated_cb),
                          data);

	g_signal_connect (G_OBJECT (data->o_recent_tree_view), 
			  "button_press_event",
			  G_CALLBACK (recent_button_press_cb), 
			  data);
	g_signal_connect (G_OBJECT (data->o_recent_tree_view),
                          "row_activated",
                          G_CALLBACK (recent_activated_cb),
                          data);

	g_signal_connect_swapped (G_OBJECT (cancel_button), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (ok_button), 
			  "clicked",
			  G_CALLBACK (open_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (cancel_button), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (data->o_del_button), 
			  "clicked",
			  G_CALLBACK (delete_recent_cb),
			  data);

	/* Set data. */

	/* * registered applications list. */

	data->app_list = NULL;
	for (scan = data->file_list; scan; scan = scan->next) {
		const char *result;
		const char *name = scan->data;

		result = gnome_vfs_mime_type_from_name_or_default (name, NULL);
		if (result != NULL)
			data->app_list = g_list_concat (data->app_list, gnome_vfs_mime_get_all_applications (result));
	}

	data->app_model = GTK_TREE_MODEL (gtk_list_store_new (2, 
							      G_TYPE_STRING,
							      G_TYPE_POINTER));

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->app_model),
                                              0,
					      GTK_SORT_ASCENDING);
	
	gtk_tree_view_set_model (GTK_TREE_VIEW (data->o_app_tree_view),
				 data->app_model);
	g_object_unref (G_OBJECT (data->app_model));

	for (scan = data->app_list; scan; scan = scan->next) {
		gboolean found;

		app = scan->data;

		found = FALSE;
		if (app_names != NULL) {
			GList *p;
			for (p = app_names; p && !found; p = p->next)
				if (strcmp ((char*)p->data, app->command) == 0)
					found = TRUE;
		}

		if (found)
			continue;

		app_names = g_list_prepend (app_names, app->command);

		gtk_list_store_append (GTK_LIST_STORE (data->app_model),
				       &iter);
		gtk_list_store_set (GTK_LIST_STORE (data->app_model), &iter,
				    0, app->name,
				    1, app,
				    -1);
	}

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (NULL,
							   renderer,
							   "text", 0,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW (data->o_app_tree_view),
				     column);

	if (app_names)
		g_list_free (app_names);

	/* * recent editors list. */

	data->recent_model = GTK_TREE_MODEL (gtk_list_store_new (1, G_TYPE_STRING));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->recent_model), 0, GTK_SORT_ASCENDING);
	
	gtk_tree_view_set_model (GTK_TREE_VIEW (data->o_recent_tree_view),
				 data->recent_model);
	g_object_unref (G_OBJECT (data->recent_model));

	editors = eel_gconf_get_string_list (PREF_EDIT_EDITORS);
	for (sscan = editors; sscan; sscan = sscan->next) {
		char *editor = sscan->data;

		gtk_list_store_append (GTK_LIST_STORE (data->recent_model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (data->recent_model), &iter,
				    0, editor,
				    -1);
	}
	g_slist_foreach (editors, (GFunc) g_free, NULL);
	g_slist_free (editors);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (NULL,
							   renderer,
							   "text", 0,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW (data->o_recent_tree_view),
				     column);

	/* Run dialog. */
	gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
	gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);
	gtk_widget_show_all (data->dialog);
}


void
open_with_cb (GtkWidget *widget, 
	      void      *callback_data)
{
	FRWindow *window = callback_data;
	GList    *file_list;

	file_list = window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL) 
		return;
	
	dlg_open_with (window, file_list);
	path_list_free (file_list);
}
