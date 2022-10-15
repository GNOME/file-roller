/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2022 Free Software Foundation, Inc.
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
#include "fr-places-sidebar.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"


enum {
	OPEN,
	LAST_SIGNAL
};
static guint fr_places_sidebar_signals[LAST_SIGNAL] = { 0 };


typedef struct {
	GtkWidget *list_box;
} FrPlacesSidebarPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrPlacesSidebar, fr_places_sidebar, GTK_TYPE_BOX)


static void
fr_places_sidebar_finalize (GObject *object)
{
	//FrPlacesSidebar *self = FR_PLACES_SIDEBAR (object);
	G_OBJECT_CLASS (fr_places_sidebar_parent_class)->finalize (object);
}


static void
fr_places_sidebar_class_init (FrPlacesSidebarClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_places_sidebar_finalize;

	fr_places_sidebar_signals[OPEN] =
		g_signal_new ("open",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrPlacesSidebarClass, open),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);
}


static GtkWidget *
row_box_for_file (GFile      *file,
		  const char *display_name)
{
	GFileInfo *info;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  NULL);
	if (info == NULL)
		return NULL;

	GtkWidget *icon = gtk_image_new_from_gicon (g_file_info_get_symbolic_icon (info));
	GtkWidget *label = gtk_label_new ((display_name != NULL) ? display_name : g_file_info_get_display_name (info));

	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (box), icon);
	gtk_box_append (GTK_BOX (box), label);

	GtkWidget *row = gtk_list_box_row_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (row), "fr-sidebar-row");
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
	g_object_set_data_full (G_OBJECT (row), "sidebar-file", g_object_ref (file), g_object_unref);

	char *name = g_file_get_parse_name (file);
	gtk_widget_set_tooltip_text (row, name);
	g_free (name);

	g_object_unref (info);

	return row;
}


static void
row_activated_cb (GtkListBox    *list_box,
		  GtkListBoxRow *row,
		  gpointer       user_data)
{
	FrPlacesSidebar *self = user_data;
	GFile *file;

	file = g_object_get_data (G_OBJECT (row), "sidebar-file");
	if (file != NULL)
		g_signal_emit (self,
			       fr_places_sidebar_signals[OPEN],
			       0,
			       file);
}


static void
fr_places_sidebar_init (FrPlacesSidebar *self)
{
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);
	GtkWidget *scrolled_window;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);

	scrolled_window = gtk_scrolled_window_new ();
	gtk_widget_set_vexpand (scrolled_window, TRUE);
	gtk_box_append (GTK_BOX (self), scrolled_window);

	private->list_box = gtk_list_box_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (private->list_box), "fr-sidebar");
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (private->list_box), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), private->list_box);
	g_signal_connect (private->list_box, "row-activated", G_CALLBACK (row_activated_cb), self);

	GHashTable *locations = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);

	/* Home */

	GtkWidget *row;
	GFile *location;

	location = g_file_new_for_uri ("recent:///");
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = g_file_new_for_uri ("starred:///");
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = _g_file_get_home ();
	row = row_box_for_file (location, _("Home"));
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}

	/* Special directories. */

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES));
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_MUSIC));
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS));
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	/* Bookmarks */

	//gtk_list_box_append (GTK_LIST_BOX (private->list_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

	/* Root */

	gtk_list_box_append (GTK_LIST_BOX (private->list_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

	location = g_file_new_for_path ("/");
	row = row_box_for_file (location, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (locations, location);
	}
	g_object_unref (location);

	g_hash_table_unref (locations);
}


GtkWidget *
fr_places_sidebar_new (void)
{
	return (GtkWidget *) g_object_new (fr_places_sidebar_get_type (), NULL);
}


void
fr_places_sidebar_set_location (FrPlacesSidebar *self,
				GFile           *location)
{
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);

	gtk_list_box_unselect_all (GTK_LIST_BOX (private->list_box));
	for (int i = 0; /* void */; i++) {
		GtkListBoxRow *row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (private->list_box), i);
		if (row == NULL)
			break;
		GFile *file = g_object_get_data (G_OBJECT (row), "sidebar-file");
		if ((file != NULL) && g_file_equal (file, location))
			gtk_list_box_select_row (GTK_LIST_BOX (private->list_box), row);
	}
}
