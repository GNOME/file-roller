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
#include "fr-marshal.h"
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
	GCancellable *cancellable;
	GVolumeMonitor *mount_monitor;
	guint mount_changed_event_id;
	guint mount_added_event_id;
	guint mount_removed_event_id;
	guint volume_changed_event_id;
	guint volume_added_event_id;
	guint volume_removed_event_id;
	guint update_volumes_id;
} FrPlacesSidebarPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrPlacesSidebar, fr_places_sidebar, GTK_TYPE_BOX)


static void
fr_places_sidebar_finalize (GObject *object)
{
	FrPlacesSidebar *self = FR_PLACES_SIDEBAR (object);
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);

	if (private->update_volumes_id != 0) {
		g_source_remove (private->update_volumes_id);
		private->update_volumes_id = 0;
	}

	if (private->mount_monitor != NULL) {
		if (private->mount_changed_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->mount_changed_event_id);
			private->mount_changed_event_id = 0;
		}

		if (private->mount_added_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->mount_added_event_id);
			private->mount_added_event_id = 0;
		}

		if (private->mount_removed_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->mount_removed_event_id);
			private->mount_removed_event_id = 0;
		}

		if (private->volume_changed_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->volume_changed_event_id);
			private->volume_changed_event_id = 0;
		}

		if (private->volume_added_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->volume_added_event_id);
			private->volume_added_event_id = 0;
		}

		if (private->volume_removed_event_id != 0) {
			g_signal_handler_disconnect (private->mount_monitor, private->volume_removed_event_id);
			private->volume_removed_event_id = 0;
		}
		g_object_unref (private->mount_monitor);
		private->mount_monitor = NULL;
	}

	if (private->cancellable != NULL) {
		g_cancellable_cancel (private->cancellable);
		private->cancellable = NULL;
	}

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
			      fr_marshal_VOID__OBJECT_OBJECT,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_OBJECT,
			      G_TYPE_OBJECT);
}


static GtkWidget *
row_box_for_file (GFile      *file,
		  GFileInfo  *info,
		  const char *display_name)
{
	GFileInfo *local_info = NULL;

	if (info == NULL) {
		local_info = g_file_query_info (
			file,
			(G_FILE_ATTRIBUTE_STANDARD_TYPE ","
			 G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
			 G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON),
			G_FILE_QUERY_INFO_NONE,
			NULL,
			NULL
		);
		if (local_info == NULL) {
			local_info = g_file_info_new ();
			g_file_info_set_file_type (local_info, G_FILE_TYPE_DIRECTORY);

			char *name = _g_file_get_display_name (file);
			g_file_info_set_display_name (local_info, name);

			char *uri = g_file_get_uri (file);
			GIcon *icon = g_themed_icon_new (g_str_has_prefix (uri, "file://") ? "folder-symbolic" : "folder-remote-symbolic");
			g_file_info_set_symbolic_icon (local_info, icon);

			g_object_unref (icon);
			g_free (uri);
			g_free (name);
		}
		info = local_info;
	}

	GtkWidget *icon = gtk_image_new_from_gicon (g_file_info_get_symbolic_icon (info));
	GtkWidget *label = gtk_label_new ((display_name != NULL) ? display_name : g_file_info_get_display_name (info));
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (box), icon);
	gtk_box_append (GTK_BOX (box), label);

	GtkWidget *row = gtk_list_box_row_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (row), "fr-sidebar-row");
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
	g_object_set_data_full (G_OBJECT (row), "sidebar-file", g_object_ref (file), g_object_unref);
	g_object_set_data_full (G_OBJECT (row), "sidebar-file-info", g_object_ref (info), g_object_unref);

	char *name = g_file_get_parse_name (file);
	gtk_widget_set_tooltip_text (row, name);
	g_free (name);

	_g_object_unref (local_info);

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
	if (file != NULL) {
		GFileInfo *info = g_object_get_data (G_OBJECT (row), "sidebar-file-info");
		g_signal_emit (self,
			       fr_places_sidebar_signals[OPEN],
			       0,
			       file,
			       info);
	}
}


/* Load bookmarks */


typedef struct {
	FrPlacesSidebar *self;
	GHashTable *locations;
} LoadContext;


static void load_context_free (LoadContext *ctx)
{
	g_object_unref (ctx->self);
	g_hash_table_unref (ctx->locations);
	g_free (ctx);
}


static void load_context_finish (LoadContext *ctx)
{
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (ctx->self);
	if (private->cancellable != NULL) {
		g_object_unref (private->cancellable);
		private->cancellable = NULL;
	}
	load_context_free (ctx);
}

static void
add_location_if_not_present (LoadContext *ctx,
			     FrPlacesSidebarPrivate *private,
			     GFile *location)
{
	if (!g_hash_table_contains (ctx->locations, location)) {
		GtkWidget *row = row_box_for_file (location, NULL, NULL);
		if (row != NULL) {
			gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
			g_hash_table_add (ctx->locations, g_object_ref (location));
		}
	}
}

static void
load_context_add_volumes (LoadContext *ctx)
{
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (ctx->self);
	GFile *location;

	// Special directories, added if not present in the bookmarks.

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES));
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_MUSIC));
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	location = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS));
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	// Root

	gtk_list_box_append (GTK_LIST_BOX (private->list_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

	location = g_file_new_for_path ("/");
	add_location_if_not_present (ctx, private, location);
	g_object_unref (location);

	// Mounted volumes

	GVolumeMonitor *monitor = g_volume_monitor_get ();
	GList *mounts = g_volume_monitor_get_mounts (monitor);
	GList *scan;
	for (scan = mounts; scan; scan = scan->next) {
		GMount    *mount = scan->data;
		GIcon     *icon;
		char      *name;
		GDrive    *drive;
		GFileInfo *info;

		if (g_mount_is_shadowed (mount))
			continue;

		location = g_mount_get_root (mount);
		if (g_hash_table_contains (ctx->locations, location)) {
			g_object_unref (location);
			continue;
		}

		icon = g_mount_get_symbolic_icon (mount);
		name = g_mount_get_name (mount);

		drive = g_mount_get_drive (mount);
		if (drive != NULL) {
			char *drive_name;
			char *tmp;

			drive_name = g_drive_get_name (drive);
			tmp = g_strconcat (drive_name, ": ", name, NULL);
			g_free (name);
			g_object_unref (drive);
			name = tmp;

			g_free (drive_name);
		}

		info = g_file_info_new ();
		g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, TRUE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
		g_file_info_set_symbolic_icon (info, icon);
		g_file_info_set_display_name (info, name);
		g_file_info_set_name (info, name);

		GtkWidget *row = row_box_for_file (location, info, NULL);
		if (row != NULL) {
			gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
			g_hash_table_add (ctx->locations, g_object_ref (location));
		}

		g_object_unref (info);
		g_free (name);
		_g_object_unref (icon);
		g_object_ref (location);
	}
	_g_object_list_unref (mounts);

	// Not mounted mountable volumes.

	GList *volumes = g_volume_monitor_get_volumes (monitor);
	for (scan = volumes; scan; scan = scan->next) {
		GVolume   *volume = scan->data;
		GMount    *mount;
		GIcon     *icon;
		char      *name;
		GFileInfo *info;

		/*if (!g_volume_can_mount (volume)) {
			continue;
		}*/

		mount = g_volume_get_mount (volume);
		if (mount != NULL) {
			/* Already mounted, ignore. */
			g_object_unref (mount);
			continue;
		}

		location = g_volume_get_activation_root (volume);
		if (location == NULL) {
			char *device;

			device = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
			if (device == NULL) {
				continue;
			}
			location = g_file_new_for_path (device);

			g_free (device);
		}

		if (g_hash_table_contains (ctx->locations, location)) {
			// Already mounted, ignore.
			g_object_unref (location);
			continue;
		}

		icon = g_volume_get_symbolic_icon (volume);
		name = g_volume_get_name (volume);

		info = g_file_info_new ();
		g_file_info_set_file_type (info, G_FILE_TYPE_MOUNTABLE);
		g_file_info_set_attribute_object (info, FR_FILE_ATTRIBUTE_VOLUME, G_OBJECT (volume));
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, TRUE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, FALSE);
		g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
		g_file_info_set_symbolic_icon (info, icon);
		g_file_info_set_display_name (info, name);
		g_file_info_set_name (info, name);

		GtkWidget *row = row_box_for_file (location, info, NULL);
		if (row != NULL) {
			gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
			g_hash_table_add (ctx->locations, g_object_ref (location));
		}

		g_object_unref (info);
		g_object_unref (location);
		g_free (name);
		_g_object_unref (icon);
	}
	_g_object_list_unref (volumes);

	g_object_unref (monitor);
	load_context_finish (ctx);
}


static void
bookmark_file_ready_cb (GObject      *source_object,
			GAsyncResult *result,
			gpointer      user_data)
{
	LoadContext *ctx = user_data;
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (ctx->self);
	char *content = NULL;
	gsize content_size;
	GError *error = NULL;

	if (! _g_file_load_buffer_finish (G_FILE (source_object), result, &content, &content_size, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			load_context_add_volumes (ctx);
		return;
	}

	if (content == NULL) {
		load_context_add_volumes (ctx);
		return;
	}

	gboolean first_bookmark = TRUE;
	char **lines = g_strsplit (content, "\n", -1);
	for (int i = 0; lines[i] != NULL; i++) {
		char **line = g_strsplit (lines[i], " ", 2);
		const char *uri = line[0];
		if (uri == NULL) {
			g_strfreev (line);
			continue;
		}
		const char *display_name = line[1];

		GFile *file = g_file_new_for_uri (uri);
		if (!g_hash_table_contains (ctx->locations, file)) {
			GtkWidget *row = row_box_for_file (file, NULL, display_name);
			if (row != NULL) {
				if (first_bookmark) {
					gtk_list_box_append (GTK_LIST_BOX (private->list_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
					first_bookmark = FALSE;
				}
				gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
				g_hash_table_add (ctx->locations, g_object_ref (file));
			}
		}

		g_object_unref (file);
		g_strfreev (line);
	}

	g_strfreev (lines);

	load_context_add_volumes (ctx);
}


static void update_entries (FrPlacesSidebar *self) {
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);

	if (private->cancellable != NULL) {
		g_cancellable_cancel (private->cancellable);
		private->cancellable = NULL;
	}
	private->cancellable = g_cancellable_new ();
	gtk_list_box_remove_all (GTK_LIST_BOX (private->list_box));

	LoadContext *ctx = g_new0 (LoadContext, 1);
	ctx->self = g_object_ref (self);
	ctx->locations = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, (GDestroyNotify) g_object_unref, NULL);

	/* Home */

	GtkWidget *row;
	GFile *location;

	location = _g_file_get_home ();
	/* Translators: this is the name of the home directory. */
	row = row_box_for_file (location, NULL, _("Home"));
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (ctx->locations, g_object_ref (location));
	}

	location = g_file_new_for_uri ("recent:///");
	row = row_box_for_file (location, NULL, NULL);
	if (row != NULL) {
		gtk_list_box_append (GTK_LIST_BOX (private->list_box), row);
		g_hash_table_add (ctx->locations, g_object_ref (location));
	}
	g_object_unref (location);

	/* Bookmarks */

	char *path = g_build_filename (g_get_user_config_dir (), "gtk-3.0", "bookmarks", NULL);
	GFile *bookmark_file = g_file_new_for_path (path);
	_g_file_load_buffer_async (bookmark_file,
				   -1,
				   private->cancellable,
				   bookmark_file_ready_cb,
				   ctx);

	g_object_unref (bookmark_file);
	g_free (path);
}


static gboolean
update_volume_list_cb (gpointer user_data)
{
	FrPlacesSidebar *self = user_data;
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);

	private->update_volumes_id = 0;
	update_entries (self);

	return G_SOURCE_REMOVE;
}


static void
mount_monitor_changed_cb (GVolumeMonitor *volume_monitor,
			  gpointer        mount_or_volume,
			  gpointer        user_data)
{
	FrPlacesSidebar *self = user_data;
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);

	if (private->update_volumes_id != 0)
		return;

	g_object_ref (self);
	private->update_volumes_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
		update_volume_list_cb,
		self,
		(GDestroyNotify) g_object_unref
	);
}

static void
fr_places_sidebar_init (FrPlacesSidebar *self)
{
	FrPlacesSidebarPrivate *private = fr_places_sidebar_get_instance_private (self);
	GtkWidget *scrolled_window;

	private->mount_monitor = g_volume_monitor_get ();
	private->mount_changed_event_id = g_signal_connect (
		private->mount_monitor,
		"mount-changed",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->mount_added_event_id = g_signal_connect (
		private->mount_monitor,
		"mount-added",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->mount_removed_event_id = g_signal_connect (
		private->mount_monitor,
		"mount-removed",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->volume_changed_event_id = g_signal_connect (
		private->mount_monitor,
		"volume-changed",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->volume_added_event_id = g_signal_connect (
		private->mount_monitor,
		"volume-added",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->volume_removed_event_id = g_signal_connect (
		private->mount_monitor,
		"volume-removed",
		G_CALLBACK (mount_monitor_changed_cb),
		self);
	private->update_volumes_id = 0;

	private->cancellable = g_cancellable_new ();

	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);

	scrolled_window = gtk_scrolled_window_new ();
	gtk_widget_set_vexpand (scrolled_window, TRUE);
	gtk_box_append (GTK_BOX (self), scrolled_window);

	private->list_box = gtk_list_box_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (private->list_box), "fr-sidebar");
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (private->list_box), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), private->list_box);
	g_signal_connect (private->list_box, "row-activated", G_CALLBACK (row_activated_cb), self);

	update_entries (self);
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
