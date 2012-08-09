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
#define PREF_FILE_SELECTOR_SHOW_HIDDEN "show-hidden"
#define PREF_FILE_SELECTOR_SIDEBAR_SIZE "sidebar-size"
#define FILE_LIST_LINES 45
#define FILE_LIST_CHARS 60
#define SIDEBAR_CHARS   12


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


typedef enum {
	PLACE_TYPE_NORMAL = 1 << 0,
	PLACE_TYPE_VOLUME = 1 << 1,
	PLACE_TYPE_BOOKMARK = 1 << 2
} PlaceType;


enum {
	PLACE_LIST_COLUMN_ICON,
	PLACE_LIST_COLUMN_NAME,
	PLACE_LIST_COLUMN_FILE,
	PLACE_LIST_COLUMN_IS_SEPARATOR,
	PLACE_LIST_COLUMN_TYPE,
	PLACE_LIST_COLUMN_SORT_ORDER
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


/*-- bookmarks -- */


typedef struct {
	GFile *file;
	char  *name;
} Bookmark;


static void
bookmark_free (Bookmark *b)
{
	if (b == NULL)
		return;
	_g_object_unref (b->file);
	g_free (b->name);
	g_slice_free (Bookmark, b);
}


static void
bookmark_list_free (GList *list)
{
	g_list_foreach (list, (GFunc) bookmark_free, NULL);
	g_list_free (list);
}


/* -- fr_file_selector_dialog -- */


struct _FrFileSelectorDialogPrivate {
	GtkBuilder    *builder;
	GtkWidget     *extra_widget;
	GFile         *current_folder;
	LoadData      *current_operation;
	LoadData      *places_operation;
	GthIconCache  *icon_cache;
	GSettings     *settings;
	GList         *special_places;
	GList         *bookmarks;
	gboolean       show_hidden;
};


static void
fr_file_selector_dialog_finalize (GObject *object)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (object);

	g_signal_handlers_disconnect_by_data (g_volume_monitor_get (), self);

	g_object_unref (self->priv->builder);
	_g_object_unref (self->priv->current_folder);
	g_object_unref (self->priv->settings);
	_g_object_list_unref (self->priv->special_places);
	bookmark_list_free (self->priv->bookmarks);

	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


static GList *
get_system_bookmarks (void)
{
	char  *filename;
	GFile *file;
	GList *list;
	char  *contents;

	filename = g_build_filename (g_get_user_config_dir (), "gtk-3.0", "bookmarks", NULL);
	file = g_file_new_for_path (filename);
	if (! g_file_query_exists (file, NULL)) {
		g_free (filename);
		g_object_unref (file);

		filename = g_build_filename (g_get_home_dir (), ".gtk-bookmarks", NULL);
		file = g_file_new_for_path (filename);
	}

	list = NULL;
	if (g_file_load_contents (file, NULL, &contents, NULL, NULL, NULL)) {
		char **lines;
		int    i;

		lines = g_strsplit (contents, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			Bookmark *bookmark;
			char     *space;

			if (lines[i][0] == '\0')
				continue;

			bookmark = g_slice_new0 (Bookmark);

			if ((space = strchr (lines[i], ' ')) != NULL) {
				space[0] = '\0';
				bookmark->name = g_strdup (space + 1);
			}
			bookmark->file = g_file_new_for_uri (lines[i]);

			list = g_list_prepend (list, bookmark);
		}

		g_strfreev (lines);
		g_free (contents);
	}

	g_object_unref (file);
	g_free (filename);

	return g_list_reverse (list);
}


static void
_gtk_list_store_clear_type (GtkListStore *list_store,
			    PlaceType     type_to_delete)
{
	GtkTreeIter iter;
	gboolean    iter_is_valid;

	iter_is_valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter);
	while (iter_is_valid) {
		PlaceType item_type;

	        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
	        		    PLACE_LIST_COLUMN_TYPE, &item_type,
	                            -1);

	        if (item_type & type_to_delete)
	        	iter_is_valid = gtk_list_store_remove (list_store, &iter);
	        else
	        	iter_is_valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), &iter);
	}
}


static void
update_bookmarks (FrFileSelectorDialog *self)
{
	GtkListStore *list_store;
	GList        *scan;
	GtkTreeIter   iter;
	int           sort_order = 0;

	bookmark_list_free (self->priv->bookmarks);
	self->priv->bookmarks = get_system_bookmarks ();

	list_store = GTK_LIST_STORE (GET_WIDGET ("places_liststore"));
	_gtk_list_store_clear_type (list_store, PLACE_TYPE_BOOKMARK);

	/* separator */

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    PLACE_LIST_COLUMN_IS_SEPARATOR, TRUE,
			    PLACE_LIST_COLUMN_TYPE, PLACE_TYPE_BOOKMARK,
			    PLACE_LIST_COLUMN_SORT_ORDER, sort_order++,
			    -1);


	for (scan = self->priv->bookmarks; scan; scan = scan->next) {
		Bookmark  *bookmark = scan->data;
		char      *name;
		GIcon     *icon;
		GdkPixbuf *icon_pixbuf;

		name = g_strdup (bookmark->name);

		if (g_file_is_native (bookmark->file)) {
			GFileInfo *info;

			info = g_file_query_info (bookmark->file,
						  (G_FILE_ATTRIBUTE_STANDARD_NAME ","
						   G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
						   G_FILE_ATTRIBUTE_STANDARD_ICON),
						  0,
						  NULL,
						  NULL);

			if (info == NULL) {
				g_free (name);
				continue;
			}

			if (name == NULL)
				name = g_strdup (g_file_info_get_display_name (info));
			icon = g_object_ref (g_file_info_get_icon (info));

			g_object_unref (info);
		}
		else {
			if (name == NULL)
				name = _g_file_get_display_basename (bookmark->file);
			icon = g_themed_icon_new ("folder-remote");
		}

		gtk_list_store_append (list_store, &iter);

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, icon);
		gtk_list_store_set (list_store, &iter,
				    PLACE_LIST_COLUMN_ICON, icon_pixbuf,
				    PLACE_LIST_COLUMN_NAME, name,
				    PLACE_LIST_COLUMN_FILE, bookmark->file,
				    PLACE_LIST_COLUMN_TYPE, PLACE_TYPE_BOOKMARK,
				    PLACE_LIST_COLUMN_SORT_ORDER, sort_order++,
				    -1);

		g_object_unref (icon_pixbuf);
		g_object_unref (icon);
		g_free (name);
	}
}



static gboolean
mount_referenced_by_volume_activation_root (GList *volumes, GMount *mount)
{
  GList *l;
  GFile *mount_root;
  gboolean ret;

  ret = FALSE;

  mount_root = g_mount_get_root (mount);

  for (l = volumes; l != NULL; l = l->next)
    {
      GVolume *volume = G_VOLUME (l->data);
      GFile *volume_activation_root;

      volume_activation_root = g_volume_get_activation_root (volume);
      if (volume_activation_root != NULL)
        {
          if (g_file_has_prefix (volume_activation_root, mount_root))
            {
              ret = TRUE;
              g_object_unref (volume_activation_root);
              break;
            }
          g_object_unref (volume_activation_root);
        }
    }

  g_object_unref (mount_root);
  return ret;
}


static void
places_treeview_selection_changed_cb (GtkTreeSelection *treeselection,
				      gpointer          user_data)
{
	FrFileSelectorDialog *self = user_data;
	GtkTreeModel         *tree_model;
	GtkTreeIter           iter;
	GFile                *file;

	if (! gtk_tree_selection_get_selected (treeselection, &tree_model, &iter))
		return;

        gtk_tree_model_get (tree_model, &iter,
        		    PLACE_LIST_COLUMN_FILE, &file,
        		    -1);
       	fr_file_selector_dialog_set_current_folder (self, file);

        g_object_unref (file);
}


static gboolean
_gtk_list_store_get_iter_for_file (GtkListStore *list_store,
				   GtkTreeIter  *iter,
				   GFile        *file)
{
	if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), iter))
		return FALSE;

	do {
		GFile *item_file;

	        gtk_tree_model_get (GTK_TREE_MODEL (list_store), iter,
	        		    PLACE_LIST_COLUMN_FILE, &item_file,
	                            -1);

	        if ((item_file != NULL) && g_file_equal (item_file, file)) {
	        	_g_object_unref (item_file);
	        	return TRUE;
	        }

	        _g_object_unref (item_file);
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), iter));

	return FALSE;
}


static void
set_current_folder (FrFileSelectorDialog *self,
		    GFile                *folder)
{
	char             *folder_name;
	GtkTreeIter       iter;
	GtkTreeSelection *tree_selection;

	if (folder != self->priv->current_folder) {
		_g_object_unref (self->priv->current_folder);
		self->priv->current_folder = g_object_ref (folder);
	}

	if (self->priv->current_folder == NULL)
		return;

	folder_name = g_file_get_parse_name (folder);
	gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("location_entry")), folder_name);

	tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (GET_WIDGET ("places_treeview")));
	g_signal_handlers_block_by_func (tree_selection, places_treeview_selection_changed_cb, self);
	if (_gtk_list_store_get_iter_for_file (GTK_LIST_STORE (GET_WIDGET ("places_liststore")), &iter, folder))
		gtk_tree_selection_select_iter (tree_selection, &iter);
	else
		gtk_tree_selection_unselect_all (tree_selection);
	g_signal_handlers_unblock_by_func (tree_selection, places_treeview_selection_changed_cb, self);
}


static void
update_spacial_places_list (FrFileSelectorDialog *self)
{
	GList *drives;
	GList *volumes;
	GList *mounts;
	GList *scan;

	_g_object_list_unref (self->priv->special_places);
	self->priv->special_places = NULL;

	/* connected drives */

	drives = g_volume_monitor_get_connected_drives (g_volume_monitor_get ());
	for (scan = drives; scan; scan = scan->next) {
		GDrive *drive = scan->data;
		GList  *volumes;
		GList  *scan_volume;

		volumes = g_drive_get_volumes (drive);
		if (volumes != NULL) {
			for (scan_volume = volumes; scan_volume; scan_volume = scan_volume->next) {
				GVolume *volume = scan_volume->data;
				GMount  *mount;

				mount = g_volume_get_mount (volume);
				if (mount != NULL)
					self->priv->special_places = g_list_prepend (self->priv->special_places, mount);
				else
					self->priv->special_places = g_list_prepend (self->priv->special_places, g_object_ref (volume));
			}
			_g_object_list_unref (volumes);
		}
		else if (g_drive_is_media_removable (drive) && !g_drive_is_media_check_automatic (drive))
			self->priv->special_places = g_list_prepend (self->priv->special_places, g_object_ref (drive));
	}
	_g_object_list_unref (drives);

	/* special_places */

	volumes = g_volume_monitor_get_volumes (g_volume_monitor_get ());
	for (scan = volumes; scan; scan = scan->next) {
		GVolume *volume = scan->data;
		GDrive  *drive;
		GMount  *mount;

		drive = g_volume_get_drive (volume);
		if (drive != NULL) {
			g_object_unref (drive);
			continue;
		}

		mount = g_volume_get_mount (volume);
		if (mount != NULL)
			self->priv->special_places = g_list_prepend (self->priv->special_places, mount);
		else
			self->priv->special_places = g_list_prepend (self->priv->special_places, g_object_ref (volume));
	}

	/* mounts */

	mounts = g_volume_monitor_get_mounts (g_volume_monitor_get ());
	for (scan = mounts; scan; scan = scan->next) {
		GMount  *mount = scan->data;
		GVolume *volume;

		volume = g_mount_get_volume (mount);
		if (volume != NULL) {
			g_object_unref (volume);
			continue;
		}

		if (mount_referenced_by_volume_activation_root (volumes, mount)) {
			g_object_unref (mount);
			continue;
		}

		self->priv->special_places = g_list_prepend (self->priv->special_places, g_object_ref (mount));
	}

	self->priv->special_places = g_list_reverse (self->priv->special_places);

	_g_object_list_unref (mounts);
	_g_object_list_unref (volumes);
}


static void
update_places_list_ready_cb (GList    *files, /* FileInfo list */
			     GError   *error,
			     gpointer  user_data)
{
	LoadData             *load_data = user_data;
	FrFileSelectorDialog *self = load_data->dialog;
	GtkTreeSelection     *tree_selection;
	GtkListStore         *list_store;
	GList                *scan;
	GtkTreeIter           iter;
	int                   sort_order = 0;

	tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (GET_WIDGET ("places_treeview")));
	g_signal_handlers_block_by_func (tree_selection, places_treeview_selection_changed_cb, self);
	gtk_tree_selection_unselect_all (tree_selection);

	/* normal places */

	list_store = GTK_LIST_STORE (GET_WIDGET ("places_liststore"));
	_gtk_list_store_clear_type (list_store, PLACE_TYPE_NORMAL | PLACE_TYPE_VOLUME);

	for (scan = files; scan; scan = scan->next) {
		FileInfo  *file_info = scan->data;
		GdkPixbuf *icon_pixbuf;

		gtk_list_store_append (list_store, &iter);

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, g_file_info_get_icon (file_info->info));
		gtk_list_store_set (list_store, &iter,
				    PLACE_LIST_COLUMN_ICON, icon_pixbuf,
				    PLACE_LIST_COLUMN_NAME, g_file_info_get_display_name (file_info->info),
				    PLACE_LIST_COLUMN_FILE, file_info->file,
				    PLACE_LIST_COLUMN_TYPE, PLACE_TYPE_NORMAL,
				    PLACE_LIST_COLUMN_SORT_ORDER, sort_order++,
				    -1);

		g_object_unref (icon_pixbuf);
	}

	/* root filesystem */

	{
		GIcon     *icon;
		GdkPixbuf *icon_pixbuf;
		GFile     *file;

		gtk_list_store_append (list_store, &iter);

		icon = g_themed_icon_new ("drive-harddisk");
		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, icon);
		file = g_file_new_for_path ("/");
		gtk_list_store_set (list_store, &iter,
				    PLACE_LIST_COLUMN_ICON, icon_pixbuf,
				    PLACE_LIST_COLUMN_NAME, N_("File System"),
				    PLACE_LIST_COLUMN_FILE, file,
				    PLACE_LIST_COLUMN_TYPE, PLACE_TYPE_VOLUME,
				    PLACE_LIST_COLUMN_SORT_ORDER, sort_order++,
				    -1);

		g_object_unref (icon_pixbuf);
		g_object_unref (icon);
		g_object_unref (file);
	}

	/* drives / volumes / mounts */

	update_spacial_places_list (self);
	for (scan = self->priv->special_places; scan; scan = scan->next) {
		GObject   *place = scan->data;
		GIcon     *icon;
		char      *name;
		GFile     *file;
		GdkPixbuf *icon_pixbuf;

		gtk_list_store_append (list_store, &iter);

		if (G_IS_DRIVE (place)) {
			icon = g_drive_get_icon (G_DRIVE (place));
			name = g_drive_get_name (G_DRIVE (place));
			file = NULL;
		}
		else if (G_IS_VOLUME (place)) {
			icon = g_volume_get_icon (G_VOLUME (place));
			name = g_volume_get_name (G_VOLUME (place));
			file = NULL;
		}
		else if (G_IS_MOUNT (place)) {
			icon = g_mount_get_icon (G_MOUNT (place));
			name = g_mount_get_name (G_MOUNT (place));
			file = g_mount_get_root (G_MOUNT (place));
		}
		else
			continue;

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, icon);
		gtk_list_store_set (list_store, &iter,
				    PLACE_LIST_COLUMN_ICON, icon_pixbuf,
				    PLACE_LIST_COLUMN_NAME, name,
				    PLACE_LIST_COLUMN_FILE, file,
				    PLACE_LIST_COLUMN_TYPE, PLACE_TYPE_VOLUME,
				    PLACE_LIST_COLUMN_SORT_ORDER, sort_order++,
				    -1);

		g_object_unref (icon_pixbuf);
		_g_object_unref (icon);
		g_free (name);
		_g_object_unref (file);
	}

	g_signal_handlers_unblock_by_func (tree_selection, places_treeview_selection_changed_cb, self);

	set_current_folder (self, self->priv->current_folder);

	if (load_data->dialog->priv->places_operation == load_data)
		load_data->dialog->priv->places_operation = NULL;

	load_data_free (load_data);
}


static void
update_places_list (FrFileSelectorDialog *self)
{
	GList *places;

	if (self->priv->places_operation != NULL)
		g_cancellable_cancel (self->priv->places_operation->cancellable);

	self->priv->places_operation = load_data_new (self, NULL);

	places = NULL;
	places = g_list_prepend (places, g_object_ref (_g_file_get_home ()));
	places = g_list_prepend (places, g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP)));
	places = g_list_reverse (places);
	_g_file_list_query_info_async (places,
				       FILE_LIST_DEFAULT,
				       (G_FILE_ATTRIBUTE_STANDARD_TYPE ","
					G_FILE_ATTRIBUTE_STANDARD_NAME ","
					G_FILE_ATTRIBUTE_STANDARD_SIZE ","
					G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
					G_FILE_ATTRIBUTE_STANDARD_ICON),
				       self->priv->places_operation->cancellable,
				       NULL,
				       NULL,
				       update_places_list_ready_cb,
				       self->priv->places_operation);

	_g_object_list_unref (places);
}


/* Taken from the Gtk+ file gtkfilechooserdefault.c
 * Copyright (C) 2003, Red Hat, Inc.
 *
 * Guesses a size based upon font sizes */
static int
get_font_size (GtkWidget *widget)
{
	GtkStyleContext *context;
	GtkStateFlags    state;
	int              font_size;
	GdkScreen       *screen;
	double           resolution;

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

	font_size = pango_font_description_get_size (gtk_style_context_get_font (context, state));
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

	g_settings_get (self->priv->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", &width, &height);
	if ((width > 0) && (height > 0)) {
		*default_width = width;
		*default_height = height;
		return;
	}

	find_good_window_size_from_style (GTK_WIDGET (self), default_width, default_height);

	if ((self->priv->extra_widget != NULL) && gtk_widget_get_visible (self->priv->extra_widget)) {
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

	self->priv->icon_cache = gth_icon_cache_new_for_widget (GTK_WIDGET (self), GTK_ICON_SIZE_MENU);
	icon = g_content_type_get_icon ("text/plain");
	gth_icon_cache_set_fallback (self->priv->icon_cache, icon);
	g_object_unref (icon);

	_fr_file_selector_dialog_update_size (self);

	sidebar_size = g_settings_get_int (self->priv->settings, PREF_FILE_SELECTOR_SIDEBAR_SIZE);
	if (sidebar_size <= 0)
		sidebar_size = get_font_size (widget) * SIDEBAR_CHARS;
		gtk_paned_set_position (GTK_PANED (GET_WIDGET ("main_paned")), sidebar_size);

	update_places_list (self);
	update_bookmarks (self);
}


static void
fr_file_selector_dialog_unrealize (GtkWidget *widget)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (widget);

	gth_icon_cache_free (self->priv->icon_cache);
	self->priv->icon_cache = NULL;

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
	g_settings_set (self->priv->settings, PREF_FILE_SELECTOR_WINDOW_SIZE, "(ii)", width, height);
	g_settings_set_boolean (self->priv->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN, self->priv->show_hidden);
	g_settings_set_int (self->priv->settings,
			    PREF_FILE_SELECTOR_SIDEBAR_SIZE,
			    gtk_paned_get_position (GTK_PANED (GET_WIDGET ("main_paned"))));

	if (self->priv->current_operation != NULL)
		g_cancellable_cancel (self->priv->current_operation->cancellable);
	if (self->priv->places_operation != NULL)
		g_cancellable_cancel (self->priv->places_operation->cancellable);

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


static gint
places_default_sort_func (GtkTreeModel *model,
			  GtkTreeIter  *a,
			  GtkTreeIter  *b,
			  gpointer      user_data)
{
	int type_a, type_b;
	int sort_order_a;
	int sort_order_b;

        gtk_tree_model_get (model, a,
        		    PLACE_LIST_COLUMN_TYPE, &type_a,
        		    PLACE_LIST_COLUMN_SORT_ORDER, &sort_order_a,
                            -1);
        gtk_tree_model_get (model, b,
        		    PLACE_LIST_COLUMN_TYPE, &type_b,
        		    PLACE_LIST_COLUMN_SORT_ORDER, &sort_order_b,
                            -1);

        if (type_a == type_b)
        	return sort_order_a - sort_order_b;
        else
        	return type_a - type_b;
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


static gboolean
places_treeview_row_separator_func (GtkTreeModel *model,
				    GtkTreeIter  *iter,
				    gpointer      user_data)
{
	gboolean is_separator;

        gtk_tree_model_get (model, iter,
        		    PLACE_LIST_COLUMN_IS_SEPARATOR, &is_separator,
        		    -1);

        return is_separator;
}


static void
go_up_button_clicked_cb (GtkButton *button,
			 gpointer   user_data)
{
	FrFileSelectorDialog *self = user_data;
	GFile                *parent;

	if (self->priv->current_folder == NULL)
		return;

	parent = g_file_get_parent (self->priv->current_folder);
	if (parent == NULL)
		return;

	fr_file_selector_dialog_set_current_folder (self, parent);

	g_object_unref (parent);
}


static gboolean
files_treeview_button_press_event_cb (GtkWidget      *widget,
				      GdkEventButton *event,
				      gpointer        user_data)
{
	FrFileSelectorDialog *self = user_data;

	if (event->button == 3) {
		gtk_menu_popup (GTK_MENU (GET_WIDGET ("file_list_context_menu")),
				NULL,
				NULL,
				NULL,
				NULL,
				event->button,
				event->time);

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

	self->priv->show_hidden = gtk_check_menu_item_get_active (checkmenuitem);
	folder = fr_file_selector_dialog_get_current_folder (self);
	selected_files = fr_file_selector_dialog_get_selected_files (self);
	_set_current_folder (self, folder, selected_files);

	_g_object_list_unref (selected_files);
	_g_object_unref (folder);
}


static void
volume_list_changed_cb (GVolumeMonitor *volume_monitor,
			GObject        *drive_mount_volume,
			gpointer        user_data)
{
	FrFileSelectorDialog *self = user_data;

	if (! gtk_widget_get_realized (GTK_WIDGET (self)))
		return;

	update_places_list (self);
}


static void
fr_file_selector_dialog_init (FrFileSelectorDialog *self)
{
	GVolumeMonitor *monitor;

	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_FILE_SELECTOR_DIALOG, FrFileSelectorDialogPrivate);
	self->priv->current_folder = NULL;
	self->priv->builder = _gtk_builder_new_from_resource ("file-selector.ui");
	self->priv->icon_cache = NULL;
	self->priv->settings = g_settings_new ("org.gnome.FileRoller.FileSelector");
	self->priv->special_places = NULL;
	self->priv->show_hidden = g_settings_get_boolean (self->priv->settings, PREF_FILE_SELECTOR_SHOW_HIDDEN);

	gtk_container_set_border_width (GTK_CONTAINER (self), 5);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"));

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME, files_name_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_SIZE, files_size_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_MODIFIED, files_modified_column_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("files_liststore")), FILE_LIST_COLUMN_NAME, GTK_SORT_ASCENDING);

	gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (GET_WIDGET ("places_liststore")), places_default_sort_func, self, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (GET_WIDGET ("places_liststore")), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (GET_WIDGET ("places_treeview")), places_treeview_row_separator_func, self, NULL);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (GET_WIDGET ("show_hidden_files_menuitem")), self->priv->show_hidden);

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
	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (GET_WIDGET ("places_treeview"))),
			  "changed",
			  G_CALLBACK (places_treeview_selection_changed_cb),
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

	monitor = g_volume_monitor_get ();
	g_signal_connect (monitor, "drive-changed", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "drive-connected", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "drive-disconnected", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "mount-added", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "mount-changed", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "mount-removed", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "volume-added", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "volume-changed", G_CALLBACK (volume_list_changed_cb), self);
	g_signal_connect (monitor, "volume-removed", G_CALLBACK (volume_list_changed_cb), self);

	_fr_file_selector_dialog_update_size (self);
	gtk_widget_grab_focus (GET_WIDGET ("files_treeview"));
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

		if (load_data->dialog->priv->current_operation == load_data)
			load_data->dialog->priv->current_operation = NULL;
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

		if (load_data->dialog->priv->current_operation == load_data)
			load_data->dialog->priv->current_operation = NULL;
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

		if (! self->priv->show_hidden && g_file_info_get_is_hidden (file_info->info))
			continue;

		gtk_list_store_append (list_store, &iter);

		icon_pixbuf = gth_icon_cache_get_pixbuf (self->priv->icon_cache, g_file_info_get_icon (file_info->info));
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

	if (load_data->dialog->priv->current_operation == load_data)
		load_data->dialog->priv->current_operation = NULL;

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
	if (self->priv->current_operation != NULL)
		g_cancellable_cancel (self->priv->current_operation->cancellable);

	gtk_list_store_clear (GTK_LIST_STORE (GET_WIDGET ("files_liststore")));

	self->priv->current_operation = load_data_new (self, folder);
	self->priv->current_operation->files_to_select = _g_object_list_ref (files);
	_get_folder_list (self->priv->current_operation);
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
	return _g_object_ref (self->priv->current_folder);
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
