/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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
#include <math.h>
#include <string.h>
#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <libgnomeui/gnome-icon-theme.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "dlg-add.h"
#include "dlg-batch-add.h"
#include "dlg-extract.h"
#include "dlg-viewer.h"
#include "dlg-viewer-or-app.h"
#include "egg-recent.h"
#include "eggtreemultidnd.h"
#include "ephy-ellipsizing-label.h"
#include "fr-archive.h"
#include "file-data.h"
#include "file-utils.h"
#include "window.h"
#include "main.h"
#include "menu-callbacks.h"
#include "menu.h"
#include "gtk-utils.h"
#include "gconf-utils.h"
#include "toolbar.h"
#include "typedefs.h"
#include "utf8-fnmatch.h"

#include "icons/pixbufs.h"


#define ACTIVITY_DELAY 100
#define ACTIVITY_PULSE_STEP (0.033)
#define FILES_TO_PROCESS_AT_ONCE 500
#define DISPLAY_TIMEOUT_INTERVAL_MSECS 300

#define PROGRESS_DIALOG_WIDTH 300
#define PROGRESS_TIMEOUT_MSECS 500     /* FIXME */
#define HIDE_PROGRESS_TIMEOUT_MSECS 200 /* FIXME */

#define MIME_TYPE_DIRECTORY "application/directory-normal"
#define ICON_TYPE_DIRECTORY "gnome-fs-directory"
#define ICON_TYPE_REGULAR   "gnome-fs-regular"
#define ICON_GTK_SIZE GTK_ICON_SIZE_LARGE_TOOLBAR


enum {
	TARGET_STRING,
	TARGET_URL
};

static GtkTargetEntry target_table[] = {
	{ "STRING",        0, TARGET_STRING },
	{ "text/plain",    0, TARGET_STRING },
	{ "text/uri-list", 0, TARGET_URL }
};

static guint n_targets = sizeof (target_table) / sizeof (target_table[0]);
static GdkPixbuf *folder_pixbuf = NULL;
static GdkPixbuf *file_pixbuf = NULL;
static GHashTable *pixbuf_hash = NULL;
static GnomeIconTheme *icon_theme = NULL;
static int icon_size = 0;


/* -- window_update_file_list -- */


static GList *
_window_get_current_dir (FRWindow *window)
{
	GList      *scan;
	GList      *dir_list = NULL;
	GHashTable *names_hash = NULL;

	names_hash = g_hash_table_new (g_str_hash, g_str_equal);

	scan = window->archive->command->file_list;
	for (; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		int       current_dir_len = strlen (window->current_dir);

		if (strncmp (fdata->full_path, 
			     window->current_dir, 
			     current_dir_len) == 0) {
			if (g_hash_table_lookup (names_hash, 
						 fdata->list_name) != NULL) 
				continue;

			g_hash_table_insert (names_hash, 
					     fdata->list_name, 
					     GINT_TO_POINTER (1));
			dir_list = g_list_prepend (dir_list, fdata);
		}
	}

	if (names_hash != NULL)
		g_hash_table_destroy (names_hash);

	return g_list_reverse (dir_list);
}


static gint
sort_by_name (gconstpointer  ptr1,
              gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;

	if (fdata1->is_dir != fdata2->is_dir) {
		if (fdata1->is_dir)
			return -1;
		else
			return 1;
	}  

	return strcasecmp (fdata1->list_name, fdata2->list_name); 
}


static gint
sort_by_size (gconstpointer  ptr1,
              gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;

	if (fdata1->is_dir != fdata2->is_dir) {
		if (fdata1->is_dir)
			return -1;
		else
			return 1;
	} else if (fdata1->is_dir && fdata2->is_dir) 
		return sort_by_name (ptr1, ptr2);

	if (fdata1->size == fdata2->size)
		return sort_by_name (ptr1, ptr2);
	else if (fdata1->size > fdata2->size)
		return 1;
	else
		return -1;
}


static gint
sort_by_type (gconstpointer  ptr1,
              gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;
	int             result;
	const char     *desc1, *desc2;

	if (fdata1->is_dir != fdata2->is_dir) {
		if (fdata1->is_dir)
			return -1;
		else
			return 1;
	} else if (fdata1->is_dir && fdata2->is_dir) 
		return sort_by_name (ptr1, ptr2);

	desc1 = file_data_get_type_description (fdata1);
	desc2 = file_data_get_type_description (fdata2);

	result = strcasecmp (desc1, desc2);
	if (result == 0)
		return sort_by_name (ptr1, ptr2);
	else
		return result;
}


static gint
sort_by_time (gconstpointer  ptr1,
              gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;

	if (fdata1->is_dir != fdata2->is_dir) {
		if (fdata1->is_dir)
			return -1;
		else
			return 1;
	} else if (fdata1->is_dir && fdata2->is_dir) 
		return sort_by_name (ptr1, ptr2);

	if (fdata1->modified == fdata2->modified)
		return sort_by_name (ptr1, ptr2);
	else if (fdata1->modified > fdata2->modified)
		return 1;
	else
		return -1;
}


static gint
sort_by_path (gconstpointer  ptr1,
              gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;
	int             result;

	if (fdata1->is_dir != fdata2->is_dir) {
		if (fdata1->is_dir)
			return -1;
		else
			return 1;
	} else if (fdata1->is_dir && fdata2->is_dir) 
		return sort_by_name (ptr1, ptr2);

	result = strcasecmp (fdata1->path, fdata2->path);
	if (result == 0)
		return sort_by_name (ptr1, ptr2);
	else
		return result;
}


#define COMPARE_FUNC_NUM 5


static GCompareFunc
get_compare_func_from_idx (int column_index)
{
	static GCompareFunc compare_funcs[COMPARE_FUNC_NUM] = {
		sort_by_name,
		sort_by_type,
		sort_by_size,
		sort_by_time,
		sort_by_path
	};

	column_index = CLAMP (column_index, 0, COMPARE_FUNC_NUM - 1);

	return compare_funcs [column_index];
}


static void
compute_file_list_name (FRWindow *window, 
			FileData *fdata)
{
	char *scan, *end;

	fdata->is_dir = FALSE;
	if (fdata->list_name != NULL)
		g_free (fdata->list_name);

	if (window->list_mode == WINDOW_LIST_MODE_FLAT) {
		fdata->list_name = g_strdup (fdata->name);
		return;
	}

	scan = fdata->full_path + strlen (window->current_dir);
	end = strchr (scan, '/');
	if (end == NULL)
		fdata->list_name = g_strdup (scan);
	else {
		fdata->is_dir = TRUE;
		fdata->list_name = g_strndup (scan, end - scan);
	}
}


static void
set_check_menu_item_state (FRWindow  *window,
                           GtkWidget *mitem, 
                           gboolean   active)
{
        g_signal_handlers_block_matched (G_OBJECT (mitem), 
					 G_SIGNAL_MATCH_DATA,
					 0, 0, NULL, 0, window);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (mitem), active);
        g_signal_handlers_unblock_matched (G_OBJECT (mitem), 
					   G_SIGNAL_MATCH_DATA,
					   0, 0, NULL, 0, window);
}


static void
_window_compute_list_names (FRWindow *window, GList *file_list)
{
	GList *scan;

	for (scan = file_list; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		compute_file_list_name (window, fdata);
	}
}


static void
_window_sort_file_list (FRWindow *window, GList **file_list)
{
	*file_list = g_list_sort (*file_list, get_compare_func_from_idx (window->sort_method));
	if (window->sort_type == GTK_SORT_DESCENDING)
		*file_list = g_list_reverse (*file_list);
}


static void
_go_up_one_level (FRWindow *window)
{
	char *new_dir;

	g_return_if_fail (window != NULL);

	if (window->list_mode != WINDOW_LIST_MODE_AS_DIR)
		return;
	if (window->current_dir == NULL)
		return;
	if (strcmp (window->current_dir, "/") == 0)
		return;

	window->current_dir[strlen (window->current_dir) - 1] = 0;
	new_dir = remove_level_from_path (window->current_dir);
	g_free (window->current_dir);
	if (new_dir[strlen (new_dir) - 1] == '/')
		window->current_dir = new_dir;
	else {
		window->current_dir = g_strconcat (new_dir, "/", NULL);
		g_free (new_dir);
	}	
}


static void _window_update_statusbar_list_info (FRWindow *window);


#define MAX_S_TIME_LEN 128

static const char *
get_time_string (time_t time)
{
	struct tm   *tm;
	static char  s_time[MAX_S_TIME_LEN];

	tm = localtime (& time);
	strftime (s_time, MAX_S_TIME_LEN - 1, "%d %b %Y, %H:%M", tm);
	
	return s_time;
}

#undef MAX_S_TIME_LEN


/* taken from egg-recent-util.c */
static GdkPixbuf *
scale_icon (GdkPixbuf *pixbuf,
	    double    *scale)
{
	guint width, height;
	
	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	
	width = floor (width * *scale + 0.5);
	height = floor (height * *scale + 0.5);
	
        return gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
}


/* taken from egg-recent-util.c */
static GdkPixbuf *
load_icon_file (char          *filename,
		guint          base_size,
		guint          nominal_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
        guint width, height, size;
        double scale;
	
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	
	if (pixbuf == NULL) {
		return NULL;
	}

	if (base_size == 0) {
		width = gdk_pixbuf_get_width (pixbuf);
		height = gdk_pixbuf_get_height (pixbuf);
		size = MAX (width, height);
		if (size > nominal_size) {
			base_size = size;
		} else {
			/* Don't scale up small icons */
			base_size = nominal_size;
		}

	} 

	if (base_size != nominal_size) {
		scale = (double) nominal_size / base_size;
		scaled_pixbuf = scale_icon (pixbuf, &scale);
		g_object_unref (pixbuf);
		pixbuf = scaled_pixbuf;
	}

        return pixbuf;
}


static GdkPixbuf *
get_icon (GtkWidget *widget,
	  FileData  *fdata)
{
	GdkPixbuf   *pixbuf;
	char        *icon_name;
	char        *icon_path;
	const char  *mime_type;
       
	if (fdata->is_dir)
		mime_type = MIME_TYPE_DIRECTORY;
	else
		mime_type = fdata->type;

	/* look in the hash table. */

	pixbuf = g_hash_table_lookup (pixbuf_hash, mime_type);
	if (pixbuf != NULL) {
		g_object_ref (G_OBJECT (pixbuf));
		return pixbuf;
	}

	if (fdata->is_dir)
		icon_name = g_strdup (ICON_TYPE_DIRECTORY);
	else if (! eel_gconf_get_boolean (PREF_LIST_USE_MIME_ICONS)) 
		icon_name = g_strdup (ICON_TYPE_REGULAR);
	else
		icon_name = gnome_icon_lookup (icon_theme,
					       NULL,
					       NULL,
					       NULL,
					       NULL,
					       mime_type,
					       GNOME_ICON_LOOKUP_FLAGS_NONE,
					       NULL);

	if (icon_name == NULL) {
		/* if nothing was found use the default internal icons. */
		if (fdata->is_dir)
			pixbuf = folder_pixbuf;
		else
			pixbuf = file_pixbuf;
		g_object_ref (pixbuf);

	} else {
		const GnomeIconData *icon_data;
		int   base_size;

		icon_path = gnome_icon_theme_lookup_icon (icon_theme, 
							  icon_name,
							  icon_size,
							  &icon_data,
							  &base_size);

		if (icon_path == NULL) {
			/* if nothing was found use the default internal 
			 * icons. */
			if (fdata->is_dir)
				pixbuf = folder_pixbuf;
			else
				pixbuf = file_pixbuf;
			g_object_ref (pixbuf);

		} else {
			/* ...else load the file from disk. */
			pixbuf = load_icon_file (icon_path, base_size, icon_size);

			if (pixbuf == NULL) {
				/* ...else use the default internal icons. */

				if (fdata->is_dir)
					pixbuf = folder_pixbuf;
				else
					pixbuf = file_pixbuf;
				g_object_ref (pixbuf);
			}
		}
	}
	
	g_hash_table_insert (pixbuf_hash, (gpointer) mime_type, pixbuf);
	g_object_ref (pixbuf);

	g_free (icon_path);
	g_free (icon_name);

	return pixbuf;
}


static int
get_column_from_sort_method (WindowSortMethod sort_method)
{
	switch (sort_method) {
	case WINDOW_SORT_BY_NAME : return COLUMN_NAME;
	case WINDOW_SORT_BY_SIZE : return COLUMN_SIZE;
	case WINDOW_SORT_BY_TYPE : return COLUMN_TYPE;
	case WINDOW_SORT_BY_TIME : return COLUMN_TIME;
	case WINDOW_SORT_BY_PATH : return COLUMN_PATH;
	default: 
		break;
	}

	return COLUMN_NAME;
}


static int
get_sort_method_from_column (int column_id)
{
	switch (column_id) {
	case COLUMN_NAME : return WINDOW_SORT_BY_NAME;
	case COLUMN_SIZE : return WINDOW_SORT_BY_SIZE;
	case COLUMN_TYPE : return WINDOW_SORT_BY_TYPE;
	case COLUMN_TIME : return WINDOW_SORT_BY_TIME;
	case COLUMN_PATH : return WINDOW_SORT_BY_PATH;
	default: 
		break;
	}

	return WINDOW_SORT_BY_NAME;
}


typedef struct {
	FRWindow *window;
	GList    *file_list;
	gboolean  add_timeout;
} UpdateData;


static void
update_data_free (gpointer callback_data)
{
	UpdateData *data = callback_data;
	FRWindow   *window = data->window;

	g_return_if_fail (data != NULL);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->list_store), get_column_from_sort_method (window->sort_method), window->sort_type);
	gtk_tree_view_set_model (GTK_TREE_VIEW (window->list_view), 
				 GTK_TREE_MODEL (window->list_store));
	window_stop_activity_mode (window);
	_window_update_statusbar_list_info (window);

	if (data->file_list != NULL)
		g_list_free (data->file_list);

	g_free (data);
}


static gboolean
update_file_list_idle (gpointer callback_data)
{
	UpdateData *data = callback_data;
	FRWindow   *window = data->window;
	GList      *file_list;
	GList      *scan;
	int         i;
	int         n = FILES_TO_PROCESS_AT_ONCE;

	if (data->file_list == NULL) {
		update_data_free (data);
		return FALSE;
	}

	file_list = data->file_list;
	for (i = 0, scan = file_list; (i < n) && scan->next; i++) 
		scan = scan->next;

	data->file_list = scan->next;
	scan->next = NULL;

	for (scan = file_list; scan; scan = scan->next) {
		FileData    *fdata = scan->data;
		GtkTreeIter  iter;
		GdkPixbuf   *pixbuf;
		char        *utf8_name;

		pixbuf = get_icon (window->app, fdata);
		utf8_name = g_locale_to_utf8 (fdata->list_name, 
					      -1, NULL, NULL, NULL);
		gtk_list_store_prepend (window->list_store, &iter);
		if (fdata->is_dir)
			gtk_list_store_set (window->list_store, &iter,
					    COLUMN_FILE_DATA, fdata,
					    COLUMN_ICON, pixbuf,
					    COLUMN_NAME, utf8_name,
					    COLUMN_TYPE, _("Folder"),
					    COLUMN_SIZE, "",
					    COLUMN_TIME, "",
					    COLUMN_PATH, "",
					    -1);
		else {
			char       *s_size;
			const char *s_time;
			const char *desc;
			char       *utf8_path;

			s_size = gnome_vfs_format_file_size_for_display (fdata->size);
			s_time = get_time_string (fdata->modified);
			desc = file_data_get_type_description (fdata);

			utf8_path = g_locale_to_utf8 (fdata->path, -1, 0, 0, 0);

			gtk_list_store_set (window->list_store, &iter,
					    COLUMN_FILE_DATA, fdata,
					    COLUMN_ICON, pixbuf,
					    COLUMN_NAME, utf8_name,
					    COLUMN_TYPE, desc,
					    COLUMN_SIZE, s_size,
					    COLUMN_TIME, s_time,
					    COLUMN_PATH, utf8_path,
					    -1);
			g_free (utf8_path);
			g_free (s_size);
		}
		g_free (utf8_name);
		g_object_unref (pixbuf);
	}

	if (gtk_events_pending ())
		gtk_main_iteration_do (TRUE);

	g_list_free (file_list);

	if (data->file_list == NULL) {
		update_data_free (data);
		return FALSE;
	} else if (data->add_timeout) {
		data->add_timeout = FALSE;
		g_timeout_add (DISPLAY_TIMEOUT_INTERVAL_MSECS, 
			       update_file_list_idle, 
			       data);
	}

	return TRUE;
}



void
window_update_file_list (FRWindow *window)
{
	GList      *dir_list = NULL;
	GList      *file_list;
	UpdateData *udata;

	if (GTK_WIDGET_REALIZED (window->list_view))
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (window->list_view), 0, 0);

	if (! window->archive_present || window->archive_new) {
		gtk_list_store_clear (window->list_store);

		if (window->archive_new) {
			gtk_widget_set_sensitive (window->list_view, TRUE);
			gtk_widget_show_all (window->list_view->parent);

		} else {
			gtk_widget_set_sensitive (window->list_view, FALSE);
			gtk_widget_hide_all (window->list_view->parent);
		}

		return;
	} else
		gtk_widget_set_sensitive (window->list_view, TRUE);

	if (window->give_focus_to_the_list) {
		gtk_widget_grab_focus (window->list_view);
		window->give_focus_to_the_list = FALSE;
	}

	gtk_tree_view_set_model (GTK_TREE_VIEW (window->list_view), 
				 GTK_TREE_MODEL (window->empty_store));

	gtk_list_store_clear (window->list_store);
	if (! GTK_WIDGET_VISIBLE (window->list_view))
		gtk_widget_show_all (window->list_view->parent);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->list_store), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, window->sort_type);

	window_start_activity_mode (window);

	if (window->list_mode == WINDOW_LIST_MODE_FLAT) {
		_window_compute_list_names (window, window->archive->command->file_list);
		_window_sort_file_list (window, &window->archive->command->file_list);
		file_list = window->archive->command->file_list;

	} else {
		_window_compute_list_names (window, window->archive->command->file_list);
		dir_list = _window_get_current_dir (window);

		while ((dir_list == NULL) 
		       && (strcmp (window->current_dir, "/") != 0)) {
			_go_up_one_level (window);
			_window_compute_list_names (window, window->archive->command->file_list);
			dir_list = _window_get_current_dir (window);
		}

		_window_sort_file_list (window, &dir_list);
		file_list = dir_list;
	}

	udata = g_new0 (UpdateData, 1);
	udata->window = window;
	udata->file_list = g_list_copy (file_list);
	udata->add_timeout = TRUE;

	update_file_list_idle (udata);

	if (dir_list != NULL)
		g_list_free (dir_list);

	_window_update_statusbar_list_info (window);
}


void
window_update_list_order (FRWindow *window)
{
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->list_store), get_column_from_sort_method (window->sort_method), window->sort_type);
}


static void
_window_update_title (FRWindow *window)
{
	if (! window->archive_present)
		gtk_window_set_title (GTK_WINDOW (window->app), 
				      _("File Roller"));
	else {
		char *title;
		char *utf8_name;
		
		utf8_name = g_locale_to_utf8 (file_name_from_path (window->archive_filename), -1, NULL, NULL, NULL);
		title = g_strdup_printf ("%s %s - %s",
					 utf8_name,
					 (window->archive->read_only) ? _("[read only]") : "",
					 _("File Roller"));

		gtk_window_set_title (GTK_WINDOW (window->app), title);
		g_free (title);
		g_free (utf8_name);
	}
}


static void 
add_selected_fd (GtkTreeModel *model,
		 GtkTreePath  *path,
		 GtkTreeIter  *iter,
		 gpointer      data)
{
	GList    **list = data;
	FileData  *fdata;
        
        gtk_tree_model_get (model, iter,
                            COLUMN_FILE_DATA, &fdata,
                            -1);

	if (! fdata->is_dir) 
		*list = g_list_prepend (*list, fdata);
}


static GList *
_get_selection_as_fd (FRWindow *window)
{
	GtkTreeSelection *selection;
	GList            *list = NULL;

	if (! GTK_WIDGET_REALIZED (window->list_view))
		return NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected_fd, &list);

	return list;
}


static void
_window_update_statusbar_list_info (FRWindow *window)
{
	char             *info, *archive_info, *selected_info;
	char             *size_txt, *sel_size_txt;
	int               tot_n, sel_n;
	GnomeVFSFileSize  tot_size, sel_size;
	GList            *scan;
	GList            *selection;

	if (window == NULL)
		return;

	if ((window->archive == NULL)
	    || (window->archive->command == NULL)) {
		gnome_appbar_set_default (GNOME_APPBAR (window->statusbar), "");
		return;
	}

	tot_n = 0;
	tot_size = 0;

	if (window->archive_present) {
		scan = window->archive->command->file_list;
		for (; scan; scan = scan->next) {
			FileData *fd = scan->data;
			tot_n++;
			tot_size += fd->size;
		}
	}

	sel_n = 0;
	sel_size = 0;

	if (window->archive_present) {
		selection = _get_selection_as_fd (window);
		for (scan = selection; scan; scan = scan->next) {
			FileData *fd = scan->data;
			sel_n++;
			sel_size += fd->size;
		}
		g_list_free (selection);
	}

	size_txt = gnome_vfs_format_file_size_for_display (tot_size);
	sel_size_txt = gnome_vfs_format_file_size_for_display (sel_size);

	if (tot_n == 0)
		archive_info = g_strdup ("");
	else if (tot_n == 1)
		archive_info = g_strdup_printf (_("1 file (%s)"),
						size_txt);
	else
		archive_info = g_strdup_printf (_("%d files (%s)"),
						tot_n,
						size_txt);


	if (sel_n == 0)
		selected_info = g_strdup ("");
	else if (sel_n == 1)
		selected_info = g_strdup_printf (_("1 file selected (%s)"), 
						 sel_size_txt);
	else
		selected_info = g_strdup_printf (_("%d files selected (%s)"), 
						 sel_n, 
						 sel_size_txt);

	info = g_strconcat (archive_info,
			    ((sel_n == 0) ? NULL : ", "), 
			    selected_info, 
			    NULL);

	gnome_appbar_set_default (GNOME_APPBAR (window->statusbar), info);

	g_free (size_txt);
	g_free (sel_size_txt);
	g_free (archive_info);
	g_free (selected_info);
	g_free (info);
}


static void
check_whether_is_a_dir (GtkTreeModel *model,
			GtkTreePath  *path,
			GtkTreeIter  *iter,
			gpointer      data)
{
	gboolean *is_a_dir = data;
	FileData *fdata;
        
        gtk_tree_model_get (model, iter,
                            COLUMN_FILE_DATA, &fdata,
                            -1);

	if (fdata->is_dir)
		*is_a_dir = fdata->is_dir;
}


static gboolean
selection_has_a_dir (FRWindow *window)
{
	GtkTreeSelection *selection;
	gboolean          is_a_dir = FALSE;

	if (! GTK_WIDGET_REALIZED (window->list_view))
		return FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
	if (selection == NULL)
		return FALSE;
	gtk_tree_selection_selected_foreach (selection, 
					     check_whether_is_a_dir, 
					     &is_a_dir);

	return is_a_dir;
}


static void
_window_update_sensitivity (FRWindow *window)
{
	gboolean no_archive;
	gboolean ro;
	gboolean file_op;
	gboolean running;
	gboolean compr_file;
	gboolean sel_not_null;
	gboolean one_file_selected;
	gboolean dir_selected;
	int      n_selected;

	if (window->batch_mode)
		return;

	running           = window->activity_ref > 0;
	no_archive        = (window->archive == NULL) || ! window->archive_present;
	ro                = ! no_archive && window->archive->read_only;
	file_op           = ! no_archive && ! window->archive_new  && ! running;
	compr_file        = ! no_archive && window->archive->is_compressed_file;
	n_selected        = _gtk_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
	sel_not_null      = n_selected > 0;
	one_file_selected = n_selected == 1;
	dir_selected      = selection_has_a_dir (window); 

	gtk_widget_set_sensitive (window->mitem_new_archive, ! running);
	gtk_widget_set_sensitive (window->mitem_open_archive, ! running);
	gtk_widget_set_sensitive (window->mitem_save_as_archive, ! no_archive && ! compr_file);
	gtk_widget_set_sensitive (window->mitem_close, ! no_archive);
	
	gtk_widget_set_sensitive (window->mitem_archive_prop, file_op);
	
	gtk_widget_set_sensitive (window->mitem_move_archive, file_op && ! ro);
	gtk_widget_set_sensitive (window->mitem_copy_archive, file_op);
	gtk_widget_set_sensitive (window->mitem_rename_archive, file_op && ! ro);
	gtk_widget_set_sensitive (window->mitem_delete_archive, file_op && ! ro);
	
	gtk_widget_set_sensitive (window->mitem_add, ! no_archive && ! ro && ! running && ! compr_file);
	gtk_widget_set_sensitive (window->mitem_delete, ! no_archive && ! ro && ! window->archive_new && ! running && ! compr_file);
	gtk_widget_set_sensitive (window->mitem_extract, file_op);
	gtk_widget_set_sensitive (window->mitem_test, ! no_archive && ! running && window->archive->command->propTest);
	gtk_widget_set_sensitive (window->mitem_open, file_op && sel_not_null && ! dir_selected);
	gtk_widget_set_sensitive (window->mitem_view, file_op && one_file_selected && ! dir_selected);

	gtk_widget_set_sensitive (window->mitem_stop, running && window->stoppable);
	if (window->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->progress_dialog),
						   GTK_RESPONSE_OK,
						   running && window->stoppable);

	gtk_widget_set_sensitive (window->mitem_reload, ! (no_archive || running));
	gtk_widget_set_sensitive (window->mitem_password, ! no_archive && ! running && window->archive->command->propPassword);

	gtk_widget_set_sensitive (window->mitem_select_all, ! no_archive);
	gtk_widget_set_sensitive (window->mitem_unselect_all, ! no_archive);
	
	/* toolbar */
	
	gtk_widget_set_sensitive (window->toolbar_new, ! running);
	gtk_widget_set_sensitive (window->toolbar_open, ! running);
	gtk_widget_set_sensitive (window->toolbar_add, ! no_archive && ! ro && ! running && ! compr_file);
	gtk_widget_set_sensitive (window->toolbar_extract, file_op);
	gtk_widget_set_sensitive (window->toolbar_view, file_op && one_file_selected && ! dir_selected);
	gtk_widget_set_sensitive (window->toolbar_stop, running && window->stoppable);
	
	/* popup menu */

	gtk_widget_set_sensitive (window->popupmenu_file[FILE_POPUP_MENU_ADD], ! no_archive && ! ro && ! running && ! compr_file);
	gtk_widget_set_sensitive (window->popupmenu_file[FILE_POPUP_MENU_DELETE], ! no_archive && ! ro && ! window->archive_new && ! running && ! compr_file);
	gtk_widget_set_sensitive (window->popupmenu_file[FILE_POPUP_MENU_EXTRACT], file_op);
	gtk_widget_set_sensitive (window->popupmenu_file[FILE_POPUP_MENU_OPEN], file_op && sel_not_null && ! dir_selected);
	gtk_widget_set_sensitive (window->popupmenu_file[FILE_POPUP_MENU_VIEW], file_op && one_file_selected && ! dir_selected);
}


static gboolean
location_entry_key_press_event_cb (GtkWidget   *widget,
				   GdkEventKey *event,
				   FRWindow    *window)
{
	if ((event->keyval == GDK_Return) 
	    || (event->keyval == GDK_KP_Enter) 
	    || (event->keyval == GDK_ISO_Enter)) 
		window_go_to_location (window, gtk_entry_get_text (GTK_ENTRY (window->location_entry)));

	return FALSE;
}


static void
_window_update_current_location (FRWindow *window)
{
	if (window->list_mode == WINDOW_LIST_MODE_FLAT) {
		gtk_widget_hide (window->location_bar);
		return;
	} 

	gtk_widget_show (window->location_bar);

	gtk_entry_set_text (GTK_ENTRY (window->location_entry), window->archive_present? window->current_dir: "");
	gtk_widget_set_sensitive (window->up_button, window->archive_present && (window->current_dir != NULL) && (strcmp (window->current_dir, "/") != 0));
	gtk_widget_set_sensitive (window->location_entry, window->archive_present);
	gtk_widget_set_sensitive (window->location_label, window->archive_present);
}


static gboolean
open_recent_cb (EggRecentView *view, 
		EggRecentItem *item, 
		gpointer       data)
{
	FRWindow   *window = data;
        char       *uri;
	int         prefix_len = strlen ("file://");
	gboolean    result = FALSE;

        uri = egg_recent_item_get_uri (item);

	if (strlen (uri) > prefix_len) {
		char *path = gnome_vfs_unescape_string (uri + prefix_len, "");
		window_archive_open (window, path, GTK_WINDOW (window->app));
		g_free (path);
		result = TRUE;
	}

        g_free (uri);

        return result;
}


static gboolean
real_close_progress_dialog (gpointer data)
{
	FRWindow *window = data;

	if (window->hide_progress_timeout != 0) {
		g_source_remove (window->hide_progress_timeout);
		window->hide_progress_timeout = 0;
	}

	if (window->progress_dialog != NULL) 
		gtk_widget_hide (window->progress_dialog);

	return FALSE;
}


static void
close_progress_dialog (FRWindow *window)
{
	if (window->progress_timeout != 0) {
		g_source_remove (window->progress_timeout);
		window->progress_timeout = 0;
	}

	if (window->hide_progress_timeout != 0)
		return;
	
	if (window->progress_dialog != NULL) 
		window->hide_progress_timeout = g_timeout_add (HIDE_PROGRESS_TIMEOUT_MSECS, 
							       real_close_progress_dialog,
							       window);
}


static void 
progress_dialog_response (GtkDialog *dialog, 
			  int        response_id,
			  FRWindow  *window)
{
	if (response_id == GTK_RESPONSE_DELETE_EVENT) 
		window->progress_dialog = NULL;
	
	else if ((response_id == GTK_RESPONSE_OK) && window->stoppable) {
		stop_cb (NULL, window);
		close_progress_dialog (window);
	}
}


static gboolean
display_progress_dialog (gpointer data)
{
	FRWindow *window = data;

	g_source_remove (window->progress_timeout);

	if (window->progress_dialog != NULL) {
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->progress_dialog),
						   GTK_RESPONSE_OK,
						   window->stoppable);
		gtk_window_present (GTK_WINDOW (window->progress_dialog));
	}

	window->progress_timeout = 0;

	return FALSE;
}


static void
open_progress_dialog (FRWindow *window)
{
	GtkDialog *d;
	GtkWidget *vbox;
	GtkWidget *lbl;

	if (window->hide_progress_timeout != 0) {
		g_source_remove (window->hide_progress_timeout);
		window->hide_progress_timeout = 0;
	}

	if (window->progress_timeout != 0)
		return;

	if (window->progress_dialog == NULL) {
		window->progress_dialog = gtk_dialog_new_with_buttons (
						       _("File Roller"),
						       GTK_WINDOW (window->app),
						       GTK_DIALOG_DESTROY_WITH_PARENT,
						       GTK_STOCK_STOP, GTK_RESPONSE_OK,
						       NULL);
		d = GTK_DIALOG (window->progress_dialog);
		gtk_dialog_set_has_separator (d, FALSE);
		gtk_window_set_resizable (GTK_WINDOW (d), FALSE);
		
		vbox = gtk_vbox_new (FALSE, 5);
		gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
		gtk_box_pack_start (GTK_BOX (d->vbox), vbox, FALSE, FALSE, 10);
		
		window->pd_progress_bar = gtk_progress_bar_new ();
		gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (window->pd_progress_bar), ACTIVITY_PULSE_STEP);
		
		gtk_widget_set_size_request (window->pd_progress_bar, PROGRESS_DIALOG_WIDTH, -1);
		gtk_box_pack_start (GTK_BOX (vbox), window->pd_progress_bar, TRUE, TRUE, 0);
		
		lbl = window->pd_message = ephy_ellipsizing_label_new ("");
		gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
		ephy_ellipsizing_label_set_mode (EPHY_ELLIPSIZING_LABEL (lbl), EPHY_ELLIPSIZE_MIDDLE);
		gtk_box_pack_start (GTK_BOX (vbox), lbl, TRUE, TRUE, 0);
		
		g_signal_connect (G_OBJECT (window->progress_dialog), 
				  "response",
				  G_CALLBACK (progress_dialog_response),
				  window);

		gtk_widget_show_all (vbox);
	}

	window->progress_timeout = g_timeout_add (PROGRESS_TIMEOUT_MSECS, 
						  display_progress_dialog, 
						  window);
}


void
window_push_message (FRWindow   *window, 
		      const char *msg)
{
	gnome_appbar_push (GNOME_APPBAR (window->statusbar), msg);
}


void
window_pop_message (FRWindow *window)
{
	gnome_appbar_pop (GNOME_APPBAR (window->statusbar));
}


static void
_action_started (FRArchive *archive,
		 FRAction   action, 
		 gpointer   data)
{
	FRWindow *window = data;
	char     *message;
	char     *full_msg;

	window_start_activity_mode (window);
	_window_update_sensitivity (window);

#ifdef DEBUG
	switch (action) {
	case FR_ACTION_LIST:
		g_print ("List");
		break;
	case FR_ACTION_DELETE:
		g_print ("Delete");
		break;
	case FR_ACTION_ADD:
		g_print ("Add");
		break;
	case FR_ACTION_EXTRACT:
		g_print ("Extract");
		break;
	case FR_ACTION_TEST:
		g_print ("Test");
		break;
	case FR_ACTION_GET_LIST:
		g_print ("Get list");
		break;
	default:
		break;
	}
	g_print (" [START]\n");
#endif

	switch (action) {
	case FR_ACTION_LIST:
		message = _("Reading archive");
		break;
	case FR_ACTION_DELETE:
		message = _("Deleting files from archive");
		break;
	case FR_ACTION_ADD:
		message = _("Adding files to archive");
		break;
	case FR_ACTION_EXTRACT:
		message = _("Extracting files from archive");
		break;
	case FR_ACTION_TEST:
		message = _("Testing archive");
		break;
	case FR_ACTION_GET_LIST:
		message = _("Getting the file list");
		break;
	default:
		break;
	}

	full_msg = g_strdup_printf ("%s, %s", message, _("wait please..."));
	window_push_message (window, full_msg);
	g_free (full_msg);

	open_progress_dialog (window); 
	fr_command_progress (window->archive->command, -1.0);
	fr_command_message (window->archive->command, message);
}


static void
_window_add_to_recent (FRWindow *window, 
		       char     *filename, 
		       gboolean  add)
{
	char *tmp;
	char *uri;
	char *filename_e;

	/* avoid adding temporary archives to the list. */

	tmp = g_strconcat (g_get_tmp_dir (), "/file-roller", NULL);
	if (strncmp (tmp, filename, strlen (tmp)) == 0) {
		g_free (tmp);
		return;
	}
	g_free (tmp);

	/**/

	filename_e = gnome_vfs_escape_path_string (filename);
	uri = g_strconcat ("file://", filename_e, NULL);

	egg_recent_model_add (window->recent_model, uri);

	g_free (uri);
	g_free (filename_e);
}


static void drag_drop_add_file_list            (FRWindow *window);
static void _window_batch_start_current_action (FRWindow *window);


static void
open_nautilus (GtkWindow  *parent,
	       const char *folder)
{
	char   *command_line;
	char   *e_folder;
	GError *err = NULL;
	
	e_folder = shell_escape (folder);
	command_line = g_strconcat ("nautilus ", e_folder, NULL);
	g_free (e_folder);
	
	if (!g_spawn_command_line_async (command_line, &err) || (err != NULL)){
		GtkWidget *d;
		char      *utf8_name;
		char      *message;

		utf8_name = g_locale_to_utf8 (folder, -1, 0, 0, 0);
		message = g_strdup_printf (_("Could not display the folder \"%s\""), utf8_name);
		g_free (utf8_name);
		d = _gtk_message_dialog_new (parent,
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_ERROR,
					     message,
					     err->message,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		g_free (message);

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
		g_clear_error (&err);
	}

	g_free (command_line);
}


void
window_convert_data_free (FRWindow *window)
{
	window->convert_data.converting = FALSE;

	g_free (window->convert_data.temp_dir);
	window->convert_data.temp_dir = NULL;
	
	g_object_unref (window->convert_data.new_archive);
	window->convert_data.new_archive = NULL;
}


static void
convert__get_files_done_cb (gpointer data)
{
	FRWindow  *window = data;

	window_pop_message (window);
	window_stop_activity_mode (window);

	visit_dir_handle_free (window->vd_handle);
	window->vd_handle = NULL;
}


static void
handle_errors (FRWindow    *window,
	       FRArchive   *archive,
	       FRAction     action, 
	       FRProcError *error)
{
	if (error->type == FR_PROC_ERROR_STOPPED) {
		GtkWidget *dialog;
		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  0,
						  GTK_STOCK_DIALOG_WARNING,
						  _("Operation stopped"),
						  NULL,
						  GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						  NULL);
		g_signal_connect (dialog, 
				  "response",
				  G_CALLBACK (gtk_widget_destroy), 
				  NULL);
		gtk_widget_show (dialog);

	} else if (error->type != FR_PROC_ERROR_NONE) {
		char      *msg = NULL;
		char      *details = NULL;
		GtkWidget *dialog;
		FRProcess *process = archive->process;

		if (action == FR_ACTION_LIST) 
			window_archive_close (window);

		switch (action) {
		case FR_ACTION_EXTRACT:
			msg = _("An error occurred while extracting files.");
			break;

		case FR_ACTION_LIST:
			msg = _("An error occurred while loading the archive.");
			break;
			
		case FR_ACTION_DELETE:
			msg = _("An error occurred while deleting files from the archive.");
			break;
			
		case FR_ACTION_ADD:
			msg = _("An error occurred while adding files to the archive.");
			break;

		case FR_ACTION_TEST:
			msg = _("An error occurred while testing archive.");
			break;
		case FR_ACTION_GET_LIST:
			/* FIXME */
			break;
		}

		switch (error->type) {
		case FR_PROC_ERROR_COMMAND_NOT_FOUND:
			details = _("Command not found.");
			break;
		case FR_PROC_ERROR_EXITED_ABNORMALLY:
			details = _("Command exited abnormally.");
			break;
		case FR_PROC_ERROR_SPAWN:
			details = error->gerror->message;
			break;
		default:
		case FR_PROC_ERROR_GENERIC:
			details = NULL;
			break;
		}

		dialog = _gtk_error_dialog_new (GTK_WINDOW (window->app),
						GTK_DIALOG_DESTROY_WITH_PARENT,
						(process->raw_error != NULL) ? process->raw_error : process->raw_output,
						details ? "%s\n%s" : "%s",
						msg,
						details);
		g_signal_connect (dialog, 
				  "response",
				  G_CALLBACK (gtk_widget_destroy), 
				  NULL);

		gtk_widget_show (dialog);
	}
}


static void
convert__action_performed (FRArchive   *archive,
			   FRAction     action, 
			   FRProcError *error,
			   gpointer     data)
{
	FRWindow *window = data;

	window_stop_activity_mode (window);
	window_pop_message (window);
	close_progress_dialog (window);

	handle_errors (window, archive, action, error);
	rmdir_recursive (window->convert_data.temp_dir);
	window_convert_data_free (window);

	_window_update_sensitivity (window);
	_window_update_statusbar_list_info (window);
}


static void
_action_performed (FRArchive   *archive,
		   FRAction     action, 
		   FRProcError *error, 
		   gpointer     data)
{
	FRWindow *window = data;

	window_stop_activity_mode (window);
	window_pop_message (window);
	close_progress_dialog (window);

	handle_errors (window, archive, action, error);

	switch (action) {
	case FR_ACTION_LIST:
		if (error->type != FR_PROC_ERROR_NONE) {
			window_archive_close (window);
			break;
		}

		if (! window->archive_present) {
			char *tmp;

			window->archive_present = TRUE;
			
			if (window->current_dir != NULL)
				g_free (window->current_dir);
			window->current_dir = g_strdup ("/");

			tmp = remove_level_from_path (window->archive_filename);
			window_set_open_default_dir (window, tmp);
			window_set_add_default_dir (window, tmp);
			window_set_extract_default_dir (window, tmp);
			g_free (tmp);

			window->archive_new = FALSE;
		}

		_window_add_to_recent (window, window->archive_filename, TRUE);
		window_update_file_list (window);
		_window_update_title (window);
		_window_update_current_location (window);
		break;

	case FR_ACTION_DELETE:
		window_archive_reload (window);
		return;

	case FR_ACTION_ADD:
		if (error->type != FR_PROC_ERROR_NONE) {
			window_archive_reload (window);
			break;
		}

		if (window->archive_new) {
			window->archive_new = FALSE;
			/* the archive file is created only when you add some
			 * file to it. */
			_window_add_to_recent (window, 
					       window->archive_filename,
					       TRUE);
		}

		if (window->adding_dropped_files) {
			/* continue adding dropped files. */
			if (window->dropped_file_list != NULL) {
				drag_drop_add_file_list (window);
				return;
			}

		} else {
			_window_add_to_recent (window, 
					       window->archive_filename,
					       TRUE);
			
			if (! window->batch_mode) {
				window_archive_reload (window);
				return;
			} 
		}
		break;

	case FR_ACTION_TEST:
		if (error->type != FR_PROC_ERROR_NONE) 
			break;
		
		window_view_last_output (window, _("Test Result"));
		return;

	case FR_ACTION_EXTRACT:
		if (error->type != FR_PROC_ERROR_NONE) {
			if (window->convert_data.converting) {
				rmdir_recursive (window->convert_data.temp_dir);
				window_convert_data_free (window);
			}
			window->view_folder_after_extraction = FALSE;
			break;
		}

		if (window->convert_data.converting) {
			_action_started (window->archive, FR_ACTION_GET_LIST, window);

			window->vd_handle = fr_archive_add_with_wildcard (
				  window->convert_data.new_archive,
				  "*",
				  NULL,
				  window->convert_data.temp_dir,
				  FALSE,
				  TRUE,
				  FALSE,
				  FALSE,
				  FALSE,
				  FALSE,
				  FALSE,
				  window->password,
				  window->compression,
				  convert__get_files_done_cb,
				  window);
			
		} else if (window->view_folder_after_extraction) {
			open_nautilus (GTK_WINDOW (window->app), window->folder_to_view);
			window->view_folder_after_extraction = FALSE;
		}
		break;

	default:
		break;
	}

	_window_update_sensitivity (window);
	_window_update_statusbar_list_info (window);

	if (error->type != FR_PROC_ERROR_NONE) 
		window_batch_mode_stop (window);

	else if (window->batch_mode) {
		window->batch_action = g_list_next (window->batch_action);
		_window_batch_start_current_action (window);
	}
}


static void
row_activated_cb (GtkTreeView       *tree_view, 
		  GtkTreePath       *path, 
		  GtkTreeViewColumn *column, 
		  gpointer           data)
{
	FRWindow    *window = data;
	FileData    *fdata;
	GtkTreeIter  iter;

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->list_store), 
				       &iter, 
				       path))
		return;
	
	gtk_tree_model_get (GTK_TREE_MODEL (window->list_store), &iter,
                            COLUMN_FILE_DATA, &fdata,
                            -1);

	if (fdata->is_dir) {
		char *new_dir;
		
		new_dir = g_strconcat (window->current_dir, 
				       fdata->list_name,
				       "/",
				       NULL);
		
		g_free (window->current_dir);
		window->current_dir = new_dir;
		
		window_update_file_list (window);
		_window_update_current_location (window);
	} else 
		window_view_or_open_file (window, 
					  fdata->original_path);
}


static int
file_button_press_cb (GtkWidget      *widget, 
		      GdkEventButton *event,
		      gpointer        data)
{
        FRWindow *window = data;

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
		GtkTreeSelection *selection;
		GtkTreePath *path;
		GtkTreeIter iter;

		if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->list_view),
						     event->x, event->y,
						     &path, NULL, NULL, NULL))
			return FALSE;
		
		if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->list_store), &iter, path)) {
			gtk_tree_path_free (path);
			return FALSE;
		}
		gtk_tree_path_free (path);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
		if (selection == NULL)
			return FALSE;

		if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_iter (selection, &iter);
		}

		gtk_menu_popup (GTK_MENU (window->file_popup_menu),
				NULL, NULL, NULL, 
				window, 
				event->button,
				event->time);
		return TRUE;
	}

	return FALSE;
}


/* -- drag and drop -- */


static char *
get_path_from_url (char *url)
{
	GnomeVFSURI *uri;
	char        *escaped;
	char        *path;

	if (url == NULL)
		return NULL;

	uri = gnome_vfs_uri_new (url);
	escaped = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
	path = gnome_vfs_unescape_string (escaped, NULL);

	gnome_vfs_uri_unref (uri);
	g_free (escaped);

	return path;
}


static GList *
get_file_list_from_url_list (gchar *url_list)
{
	GList *list = NULL;
	int    i;
	char  *url_start, *url_end;

	i = 0;
	url_start = url_list;
	while (url_list[i] != '\0')	{
		char *url;

		while ((url_list[i] != '\0')
		       && (url_list[i] != '\r')
		       && (url_list[i] != '\n')) i++;

		url_end = url_list + i;
		if (strncmp (url_start, "file:", 5) == 0) {
			url_start += 5;
			if ((url_start[0] == '/') 
			    && (url_start[1] == '/')) url_start += 2;
		}

		url = g_strndup (url_start, url_end - url_start);
		list = g_list_prepend (list, get_path_from_url (url));
		g_free (url);

		while ((url_list[i] != '\0')
		       && ((url_list[i] == '\r')
			   || (url_list[i] == '\n'))) i++;
		url_start = url_list + i;
	}
	
	return g_list_reverse (list);
}


static void
drag_drop_add_file_list (FRWindow *window)
{
	GList     *list = window->dropped_file_list;
	FRArchive *archive = window->archive;
	GList     *scan;
	char      *first_basedir;
	gboolean   same_dir;

	if (window->activity_ref > 0) 
		return;

	/**/

	if (window->archive->read_only) {
		GtkWidget *dialog;

		dialog = _gtk_message_dialog_new (NULL,
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not add the files to the archive"),
						  _("You don't have the right permissions."),
						  GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						  NULL);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		window->adding_dropped_files = FALSE;

		if (window->batch_mode) {
			window_archive_close (window);
			_window_update_sensitivity (window);
			_window_update_statusbar_list_info (window);
			window_batch_mode_stop (window);
		}

		return;
	}

	if (list == NULL) {
		window->adding_dropped_files = FALSE;
		if (! window->batch_mode)
			window_archive_reload (window);
		return;
	}

	for (scan = list; scan; scan = scan->next) {
		if (strcmp (scan->data, window->archive_filename) == 0) {
			GtkWidget *dialog;

			window->adding_dropped_files = FALSE;
			dialog = _gtk_message_dialog_new (NULL,
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not add the files to the archive"),
							  _("You can't add an archive to itself."),
							  GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
							  NULL);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			
			if (window->batch_mode) {
				window_archive_close (window);
				_window_update_sensitivity (window);
				_window_update_statusbar_list_info (window);
				window_batch_mode_stop (window);
			}

			return;
		}
	}

	/* add directories. */

	for (scan = list; scan; scan = scan->next) {
		char *path = scan->data;
		char *base_dir;

		if (! path_is_dir (path)) 
			continue;

		window->dropped_file_list = g_list_remove_link (list, scan);
		window->adding_dropped_files = TRUE;

		base_dir = remove_level_from_path (path);
		window_archive_add_directory (window,
					      file_name_from_path (path),
					      base_dir,
					      window->update_dropped_files,
					      window->password,
					      window->compression);
		g_free (base_dir);
		g_free (path);

		return;
	}

	window->adding_dropped_files = FALSE;

	/* if all files are in the same directory call fr_archive_add once. */

	same_dir = TRUE;
	first_basedir = remove_level_from_path (list->data);

	if (first_basedir == NULL) 
		return;

	for (scan = list->next; scan; scan = scan->next) {
		char *basedir;

		basedir = remove_level_from_path (scan->data);
		if (basedir == NULL) {
			same_dir = FALSE;
			break;
		}

		if (strcmp (first_basedir, basedir) != 0) {
			same_dir = FALSE;
			g_free (basedir);
			break;
		}
		g_free (basedir);
	}

	if (same_dir) {
		GList *only_names_list = NULL;

		for (scan = list; scan; scan = scan->next)
			only_names_list = g_list_prepend (only_names_list, (gpointer) file_name_from_path (scan->data));

		fr_archive_add (archive,
				only_names_list,
				first_basedir,
				window->update_dropped_files,
				window->password,
				window->compression);

		g_list_free (only_names_list);
		g_free (first_basedir);

		return;
	}
	g_free (first_basedir);
	
	/* ...else call fr_command_add for each file.  This is needed to add
	 * files without path info. */

	fr_archive_stoppable (archive, FALSE);

	fr_process_clear (archive->process);
	fr_command_uncompress (archive->command);
	for (scan = list; scan; scan = scan->next) {
		gchar *fullpath = scan->data;
		gchar *basedir;
		GList *singleton;
		
		basedir = remove_level_from_path (fullpath);
		singleton = g_list_prepend (NULL, 
					    shell_escape (file_name_from_path (fullpath)));
		fr_command_add (archive->command,
				singleton,
				basedir,
				window->update_dropped_files,
				window->password,
				window->compression);
		path_list_free (singleton);
		g_free (basedir);
	}
	fr_command_recompress (archive->command, window->compression);
	fr_process_start (archive->process);
}


void  
window_drag_data_received  (GtkWidget          *widget,
			    GdkDragContext     *context,
			    gint                x,
			    gint                y,
			    GtkSelectionData   *data,
			    guint               info,
			    guint               time,
			    gpointer            extra_data)
{
	FRWindow *window = extra_data;
	GList    *list;
	gboolean  one_file;
	gboolean  is_an_archive;

#ifdef DEBUG
	g_print ("::DragDataReceived -->\n");
#endif

	if (! ((data->length >= 0) && (data->format == 8))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	if (window->activity_ref > 0) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	list = get_file_list_from_url_list ((char *)data->data);
	if (list == NULL)
		return;

	if (window->dropped_file_list != NULL)
		path_list_free (window->dropped_file_list);
	window->dropped_file_list = list;
	window->update_dropped_files = FALSE;

	one_file = (list->next == NULL);
	if (one_file)
		is_an_archive = fr_archive_utils__file_is_archive (list->data);
	else
		is_an_archive = FALSE;

	if (window->archive_present && ! window->archive->is_compressed_file) {
		if (one_file && is_an_archive) {
			GtkWidget *d;
			gint       r;

			d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     _("Do you want to add this file to the current archive or open it as a new archive?"),
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     GTK_STOCK_ADD, 0,
						     GTK_STOCK_OPEN, 1,
						     NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (d), 2);

			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			if (r == 0) { /* Add */
				/* this way we do not free the list saved in
				 * window->dropped_file_list. */
				list = NULL;
				drag_drop_add_file_list (window);

			} else if (r == 1) { /* Open */
				/* if this window already has an archive 
				 * create a new window. */
				if (window->archive_present) {
					FRWindow *new_window;
					new_window = window_new ();
					gtk_widget_show (new_window->app);
					window_archive_open (new_window, list->data, GTK_WINDOW (new_window->app));
				} else
					window_archive_open (window, list->data, GTK_WINDOW (window->app));
			}
 		} else {
			/* this way we do not free the list saved in
			 * window->dropped_file_list. */
			list = NULL;
			drag_drop_add_file_list (window);
		}
	} else {
		if (one_file && is_an_archive) 
			window_archive_open (window, list->data, GTK_WINDOW (window->app));
		else {
			GtkWidget *d;
			int        r;

			d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     _("Do you want to create a new archive with these files?"),
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Archive"), GTK_RESPONSE_YES,
						     NULL);
			
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			if (r == GTK_RESPONSE_YES) {
				window->add_after_creation = TRUE;

				/* this way we do not free the list saved in
				 * window->dropped_file_list. */
				list = NULL; 
				new_archive_cb (NULL, window);
			}
		}
	}

	if (list != NULL) {
		path_list_free (list);
		window->dropped_file_list = NULL;
	}

#ifdef DEBUG
	g_print ("::DragDataReceived <--\n");
#endif
}


static gchar *
make_url_list (GList    *list, 
	       gboolean  plain_text)
{
	char  *url_list;
	int    url_list_length;
	char  *prefix;
	int    prefix_length;
	char  *url_sep = "\r\n";
	int    url_sep_length;
	GList *scan;

	if (list == NULL)
		return NULL;
	
	prefix = (plain_text) ? g_strdup ("") : g_strdup ("file://");
	prefix_length = strlen (prefix);
	url_sep_length = strlen (url_sep);

	url_list_length = 0;
	for (scan = list; scan; scan = scan->next)
		url_list_length += (prefix_length 
				    + strlen (scan->data)
				    + url_sep_length);

	url_list = g_malloc (url_list_length + 1);
	*url_list = 0;

	for (scan = list; scan; scan = scan->next) {
		url_list = strncat (url_list, prefix, prefix_length);
		url_list = strncat (url_list, scan->data, strlen (scan->data));
		url_list = strncat (url_list, url_sep, url_sep_length);
	}

	g_free (prefix);

	return url_list;
}


static char *
_get_temp_dir_name ()
{
	static int count = 0;
	return g_strdup_printf ("%s%s.%d.%d",
				g_get_tmp_dir (),
				"/file-roller",
				getpid (),
				count++);
}


static void 
add_selected_name (GtkTreeModel *model,
		   GtkTreePath  *path,
		   GtkTreeIter  *iter,
		   gpointer      data)
{
	GList    **list = data;
	FileData  *fdata;
        
        gtk_tree_model_get (model, iter,
                            COLUMN_FILE_DATA, &fdata,
                            -1);
	*list = g_list_prepend (*list, g_strdup (fdata->list_name));
}


static GList *
_get_selection_as_names (FRWindow *window)
{
	GtkTreeSelection *selection;
	GList            *list = NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected_name, &list);
	return g_list_reverse (list);
}


static void  
file_list_drag_begin (GtkWidget          *widget,
		      GdkDragContext     *context,
		      gpointer            data)
{
	FRWindow *window = data;

#ifdef DEBUG
	g_print ("::DragBegin -->\n");
#endif

	if (window->activity_ref > 0) 
		return;

	window->drag_file_list = window_get_file_list_selection (window, TRUE, & (window->dragging_dirs));

	if (window->drag_file_list_names != NULL) {
		path_list_free (window->drag_file_list_names);
		window->drag_file_list_names = NULL;
	}

	if (window->dragging_dirs)
		window->drag_file_list_names = _get_selection_as_names (window);

	if (window->drag_file_list != NULL) {
		window->drag_temp_dir = _get_temp_dir_name ();
		window->drag_temp_dirs = g_list_prepend (window->drag_temp_dirs, window->drag_temp_dir);

		ensure_dir_exists (window->drag_temp_dir, 0700);
		fr_archive_extract (window->archive,
				    window->drag_file_list,
				    window->drag_temp_dir,
				    FALSE,
				    TRUE,
				    FALSE,
				    window->password);

		while (window->activity_ref > 0)
			while (gtk_events_pending ())
				gtk_main_iteration ();
	}

#ifdef DEBUG
	g_print ("::DragBegin <--\n");
#endif
}


static void  
file_list_drag_end (GtkWidget      *widget,
		    GdkDragContext *context,
		    gpointer        data)
{
	FRWindow *window = data;

#ifdef DEBUG
	g_print ("::DragEnd -->\n");
#endif

	if (window->activity_ref > 0) 
		return;

	if (window->drag_file_list != NULL) {
		path_list_free (window->drag_file_list);
		window->drag_file_list = NULL;
	}

#ifdef DEBUG
	g_print ("::DragEnd <--\n");
#endif
}


static void  
file_list_drag_data_get  (GtkWidget          *widget,
			  GdkDragContext     *context,
			  GtkSelectionData   *selection_data,
			  guint               info,
			  guint               time,
			  gpointer            data)
{
	FRWindow *window = data;
	GList    *list, *scan;
	char     *url_list;

#ifdef DEBUG
	g_print ("::DragDataGet -->\n"); 
#endif

	if (window->activity_ref > 0) 
		return;

	if (window->drag_file_list == NULL) 
		return;
	
	list = NULL;
	if (! window->dragging_dirs) 
		for (scan = window->drag_file_list; scan; scan = scan->next) {
			char *url;
			url = g_strconcat (window->drag_temp_dir, 
					   "/", 
					   scan->data, 
					   NULL);
			list = g_list_prepend (list, url);
		}
	else
		for (scan = window->drag_file_list_names; scan; scan = scan->next) {
			char *url;
			url = g_strconcat (window->drag_temp_dir, 
					   window->current_dir,
					   scan->data, 
					   NULL);
			list = g_list_prepend (list, url);
		}


	url_list = make_url_list (list, (info == TARGET_STRING));
	path_list_free (list);

	if (url_list == NULL) 
		return;

	gtk_selection_data_set (selection_data, 
				selection_data->target,
				8, 
				url_list, 
				strlen (url_list));

	g_free (url_list);

#ifdef DEBUG
	g_print ("::DragDataGet <--\n");
#endif
}


/* -- window_new -- */


static GtkWidget *
create_icon (const guint8 rgba_data [])
{
	GtkWidget *image;
	GdkPixbuf *pixbuf;
	int        max_size;

	pixbuf = gdk_pixbuf_new_from_inline (-1, rgba_data, FALSE, NULL);

	if (pixbuf == NULL)
		return NULL;

	max_size = MAX (gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf));
	if (icon_size != max_size) {
		double     scale = (double) icon_size / max_size;
		GdkPixbuf *scaled_pixbuf = scale_icon (pixbuf, &scale);
		g_object_unref (pixbuf);
		pixbuf = scaled_pixbuf;
	}

	image = gtk_image_new_from_pixbuf (pixbuf);
	g_object_unref (pixbuf);

	return image;
}


static gboolean
key_press_cb (GtkWidget   *widget, 
              GdkEventKey *event,
              gpointer     data)
{
        FRWindow *window = data;

	if (GTK_WIDGET_HAS_FOCUS (window->location_entry)) 
		return FALSE;
	
	switch (event->keyval) {
	case GDK_Escape:
		stop_cb (NULL, window);
		break;

	case GDK_Delete:
		if (window->activity_ref == 0)
			dlg_delete (NULL, window);
		break;

	case GDK_F10:
		if (event->state & GDK_SHIFT_MASK) {
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
			if (selection == NULL)
				return FALSE;

			gtk_menu_popup (GTK_MENU (window->file_popup_menu),
					NULL, NULL, NULL, 
					window, 
					3,
					GDK_CURRENT_TIME);
		}
		return FALSE;

	default:
		return FALSE;
	}

	return TRUE;
}


static void
selection_changed_cb (GtkTreeSelection *selection,
		      gpointer          user_data)
{
	FRWindow *window = user_data;
	_window_update_statusbar_list_info (window);
	_window_update_sensitivity (window);
}


static void
window_delete_event_cb (GtkWidget *caller, 
			GdkEvent  *event, 
			FRWindow  *window)
{
#ifdef DEBUG
	g_print ("DELETE\n");
#endif
	window_close (window);
}


static void
add_columns (GtkTreeView *treeview)
{
	static char       *titles[] = {N_("Size"), 
				       N_("Type"), 
				       N_("Date modified"), 
				       N_("Location")};
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	int                i, j;

	/* The Name column. */
	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, _("Name"));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", COLUMN_ICON,
                                             NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column,
					 renderer,
					 TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", COLUMN_NAME,
                                             NULL);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column, TRUE);

	gtk_tree_view_column_set_sort_column_id (column, COLUMN_NAME);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Other columns */
	for (j = 0, i = COLUMN_SIZE; i < NUMBER_OF_COLUMNS; i++, j++) {
		renderer = gtk_cell_renderer_text_new ();
		column = gtk_tree_view_column_new_with_attributes (_(titles[j]),
								   renderer,
								   "text", i,
								   NULL);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		gtk_tree_view_column_set_resizable (column, TRUE);

		gtk_tree_view_column_set_sort_column_id (column, i);

		gtk_tree_view_append_column (treeview, column);
	}
}


static int
name_column_sort_func (GtkTreeModel *model, 
		       GtkTreeIter  *a, 
		       GtkTreeIter  *b, 
		       gpointer      user_data)
{
	FileData *fdata1, *fdata2;
        gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
        gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);
	return sort_by_name (fdata1, fdata2);
}


static int
size_column_sort_func (GtkTreeModel *model, 
		       GtkTreeIter  *a, 
		       GtkTreeIter  *b, 
		       gpointer      user_data)
{
	FileData *fdata1, *fdata2;
        gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
        gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);
	return sort_by_size (fdata1, fdata2);
}


static int
type_column_sort_func (GtkTreeModel *model, 
		       GtkTreeIter  *a, 
		       GtkTreeIter  *b, 
		       gpointer      user_data)
{
	FileData *fdata1, *fdata2;
        gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
        gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);
	return sort_by_type (fdata1, fdata2);        
}


static int
time_column_sort_func (GtkTreeModel *model, 
		       GtkTreeIter  *a, 
		       GtkTreeIter  *b, 
		       gpointer      user_data)
{
	FileData *fdata1, *fdata2;
        gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
        gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);
	return sort_by_time (fdata1, fdata2);
}


static int
path_column_sort_func (GtkTreeModel *model, 
		       GtkTreeIter  *a, 
		       GtkTreeIter  *b, 
		       gpointer      user_data)
{
	FileData *fdata1, *fdata2;
        gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
        gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);
	return sort_by_path (fdata1, fdata2);
}


static int
default_sort_func (GtkTreeModel *model, 
		   GtkTreeIter  *a, 
		   GtkTreeIter  *b, 
		   gpointer      user_data)
{
	return 1;
}


static void
sort_column_changed_cb (GtkTreeSortable *sortable,
			gpointer         user_data)
{
	FRWindow    *window = user_data;
	GtkSortType  order;
	int          column_id;

	if (! gtk_tree_sortable_get_sort_column_id (sortable, 
						    &column_id, 
						    &order))
		return;

	window->sort_method = get_sort_method_from_column (column_id);
	window->sort_type = order;
	
	set_check_menu_item_state (window, window->mitem_sort[window->sort_method], TRUE);
	set_check_menu_item_state (window, window->mitem_sort_reversed, 
				   (window->sort_type == GTK_SORT_DESCENDING));
}


static gboolean 
window_show_cb (GtkWidget *widget,
		FRWindow  *window)
{
	gboolean view_foobar;

	_window_update_current_location (window);

	view_foobar = eel_gconf_get_boolean (PREF_UI_TOOLBAR);
	set_check_menu_item_state (window, 
				   window->mitem_view_toolbar, 
				   view_foobar);
	window_set_toolbar_visibility (window, view_foobar);

	view_foobar = eel_gconf_get_boolean (PREF_UI_STATUSBAR);
	set_check_menu_item_state (window, 
				   window->mitem_view_statusbar, 
				   view_foobar);
	window_set_statusbar_visibility (window, view_foobar);
	
	return TRUE;
}


/* preferences changes notification callbacks */


static void
pref_history_len_changed (GConfClient *client,
			  guint        cnxn_id,
			  GConfEntry  *entry,
			  gpointer     user_data)
{
	FRWindow *window = user_data;
	egg_recent_model_set_limit (window->recent_model, 
				    eel_gconf_get_integer (PREF_UI_HISTORY_LEN));
}


static void
pref_view_toolbar_changed (GConfClient *client,
			   guint        cnxn_id,
			   GConfEntry  *entry,
			   gpointer     user_data)
{
	FRWindow *window = user_data;
	window_set_toolbar_visibility (window, gconf_value_get_bool (gconf_entry_get_value (entry)));
}


static void
pref_view_statusbar_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
	FRWindow *window = user_data;
	window_set_statusbar_visibility (window, gconf_value_get_bool (gconf_entry_get_value (entry)));
}


static void
pref_show_field_changed (GConfClient *client,
			 guint        cnxn_id,
			 GConfEntry  *entry,
			 gpointer     user_data)
{
	FRWindow *window = user_data;
	window_update_columns_visibility (window);
}


static void gh_unref_pixbuf (gpointer  key,
			     gpointer  value,
			     gpointer  user_data);


static void
pref_use_mime_icons_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
	FRWindow *window = user_data;
	
	if (pixbuf_hash != NULL) {
		g_hash_table_foreach (pixbuf_hash, 
				      gh_unref_pixbuf,
				      NULL);
		g_hash_table_destroy (pixbuf_hash);
		pixbuf_hash = g_hash_table_new (g_str_hash, g_str_equal);
	}

	window_update_file_list (window);
}


static void
theme_changed_cb (GnomeIconTheme *theme, FRWindow *window)
{
	int icon_width, icon_height;
	
	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (window->app),
					   ICON_GTK_SIZE,
					   &icon_width, &icon_height);
	
	icon_size = MAX (icon_width, icon_height);
	
	if (pixbuf_hash != NULL) {
		g_hash_table_foreach (pixbuf_hash,
				      gh_unref_pixbuf,
				      NULL);
		g_hash_table_destroy (pixbuf_hash);
		pixbuf_hash = g_hash_table_new (g_str_hash, g_str_equal);
	}
	
	window_update_file_list (window);
}


static gboolean
window_progress_cb (FRCommand  *command,
		    double      fraction,
		    FRWindow   *window)
{
	if (fraction < 0.0) 
		window->progress_pulse = TRUE;

	else {
		window->progress_pulse = FALSE;
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->pd_progress_bar), fraction);
	}

	return TRUE;
}


static gboolean
window_message_cb  (FRCommand  *command,
		    const char *msg,
		    FRWindow   *window)		     
{
	ephy_ellipsizing_label_set_text (EPHY_ELLIPSIZING_LABEL (window->pd_message), msg);
	return TRUE;
}


static gboolean
window_stoppable_cb  (FRCommand  *command,
		      gboolean    stoppable,
		      FRWindow   *window)		     
{
	window->stoppable = stoppable;

	gtk_widget_set_sensitive (window->mitem_stop, stoppable);
	gtk_widget_set_sensitive (window->toolbar_stop, window->stoppable);
	if (window->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->progress_dialog),
						   GTK_RESPONSE_OK,
						   stoppable);

	return TRUE;
}


static gboolean
window_fake_load (FRArchive *archive,
		  gpointer   data)
{
	FRWindow *window = data;

	return (window->batch_mode 
		&& ! (window->add_after_opening && window->update_dropped_files && ! archive->command->propAddCanUpdate)
		&& ! (window->add_after_opening && ! window->update_dropped_files && ! archive->command->propAddCanReplace));
}



FRWindow *
window_new ()
{
	FRWindow         *window;
	GtkWidget        *toolbar;
	GtkWidget        *scrolled_window;
	GtkWidget        *vbox;
	GtkWidget        *location_box;
	GtkWidget        *up_box;
	GtkWidget        *up_image;
	GtkTreeSelection *selection;
	int               i;
	EggRecentModel   *model;
        EggRecentViewGtk *view;
	int               icon_width, icon_height;

	/* data common to all windows. */

	if (file_pixbuf == NULL) 
		file_pixbuf = gdk_pixbuf_new_from_inline (-1, file_rgba, FALSE, NULL);
	if (folder_pixbuf == NULL)
		folder_pixbuf = gdk_pixbuf_new_from_inline (-1, folder_rgba, FALSE, NULL);

	if (pixbuf_hash == NULL)
		pixbuf_hash = g_hash_table_new (g_str_hash, g_str_equal);

	if (icon_theme == NULL) {
		icon_theme = gnome_icon_theme_new ();
		gnome_icon_theme_set_allow_svg (icon_theme, TRUE);
	}

	/**/

	window = g_new0 (FRWindow, 1);

	/* Create the application. */

        window->app = gnome_app_new ("main", _("File Roller"));
        gnome_window_icon_set_from_default (GTK_WINDOW (window->app));

	g_signal_connect (G_OBJECT (window->app), 
			  "delete_event",
			  G_CALLBACK (window_delete_event_cb),
			  window);

	g_signal_connect (G_OBJECT (window->app), 
			  "show",
			  G_CALLBACK (window_show_cb),
			  window);

	window->theme_changed_handler_id =
		g_signal_connect (icon_theme,
				  "changed",
				  G_CALLBACK (theme_changed_cb),
				  window);
	
	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (window->app),
					   ICON_GTK_SIZE,
					   &icon_width, &icon_height);
	
	icon_size = MAX (icon_width, icon_height);

	gtk_window_set_default_size (GTK_WINDOW (window->app), 600, 480);

	gtk_drag_dest_set (window->app,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, n_targets,
			   GDK_ACTION_COPY);
	
	g_signal_connect (G_OBJECT (window->app), 
			  "drag_data_received",
			  G_CALLBACK (window_drag_data_received), 
			  window);

	g_signal_connect (G_OBJECT (window->app), 
			  "key_press_event",
			  G_CALLBACK (key_press_cb), 
			  window);

	/* Create the widgets. */

	/* * File list. */

	window->list_store = gtk_list_store_new (NUMBER_OF_COLUMNS, 
						 G_TYPE_POINTER,
						 GDK_TYPE_PIXBUF,
						 G_TYPE_STRING,
						 G_TYPE_STRING,
						 G_TYPE_STRING,
						 G_TYPE_STRING,
						 G_TYPE_STRING);
	window->empty_store = gtk_list_store_new (NUMBER_OF_COLUMNS, 
						  G_TYPE_POINTER,
						  GDK_TYPE_PIXBUF,
						  G_TYPE_STRING,
						  G_TYPE_STRING,
						  G_TYPE_STRING,
						  G_TYPE_STRING,
						  G_TYPE_STRING);
	window->list_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (window->list_store));

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (window->list_view), TRUE);
	add_columns (GTK_TREE_VIEW (window->list_view));
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (window->list_view), 
					 TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (window->list_view),
					 COLUMN_NAME);

	gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (window->list_store), default_sort_func,  NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->list_store),
					 COLUMN_NAME, name_column_sort_func,
					 NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->list_store),
                                         COLUMN_SIZE, size_column_sort_func,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->list_store),
                                         COLUMN_TYPE, type_column_sort_func,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->list_store),
                                         COLUMN_TIME, time_column_sort_func,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->list_store),
                                         COLUMN_PATH, path_column_sort_func,
                                         NULL, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
	egg_tree_multi_drag_add_drag_support (GTK_TREE_VIEW (window->list_view));

	g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (selection_changed_cb),
                          window);
	g_signal_connect (G_OBJECT (window->list_view),
                          "row_activated",
                          G_CALLBACK (row_activated_cb),
                          window);
	g_signal_connect (G_OBJECT (window->list_view), 
			  "button_press_event",
			  G_CALLBACK (file_button_press_cb), 
			  window);

	g_signal_connect (G_OBJECT (window->list_store), 
			  "sort_column_changed",
			  G_CALLBACK (sort_column_changed_cb), 
			  window);

        gtk_drag_source_set (window->list_view, 
			     GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			     target_table, n_targets, 
			     GDK_ACTION_COPY);

	g_signal_connect (G_OBJECT (window->list_view), 
			  "drag_data_get",
			  G_CALLBACK (file_list_drag_data_get), 
			  window);
	g_signal_connect (G_OBJECT (window->list_view), 
			  "drag_begin",
			  G_CALLBACK (file_list_drag_begin), 
			  window);
	g_signal_connect (G_OBJECT (window->list_view), 
			  "drag_end",
			  G_CALLBACK (file_list_drag_end), 
			  window);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled_window), window->list_view);

	/* * Location bar. */

	location_box = gtk_hbox_new (FALSE, 1);
	gtk_container_set_border_width (GTK_CONTAINER (location_box), 3);

	window->location_bar = gnome_app_add_docked (GNOME_APP (window->app),
						     location_box,
						     "LocationBar",
						     (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL 
						      | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE 
						      | (eel_gconf_get_boolean (PREF_DESKTOP_TOOLBAR_DETACHABLE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
						     BONOBO_DOCK_TOP,
						     2, 1, 0);

	/* up button. */

	window->up_button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (window->up_button),
			       GTK_RELIEF_NONE);

	up_box = gtk_hbox_new (FALSE, 1);
	up_image = gtk_image_new ();
	gtk_image_set_from_stock (GTK_IMAGE (up_image), 
				  GTK_STOCK_GO_UP, 
				  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_box_pack_start (GTK_BOX (up_box), 
			    up_image, FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (up_box), 
			    gtk_label_new_with_mnemonic (_("_Up")), TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (window->up_button), up_box);
	gtk_box_pack_start (GTK_BOX (location_box), 
			    window->up_button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (window->up_button), 
			  "clicked",
			  G_CALLBACK (go_up_one_level_cb),
			  window);

	/* separator */

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (location_box), 
			    vbox,
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), 
			    gtk_vseparator_new (),
			    TRUE, TRUE, 5);

	/* current location */

	window->location_label = gtk_label_new (_("Location:"));
	gtk_box_pack_start (GTK_BOX (location_box), 
			    window->location_label, FALSE, FALSE, 5);

	window->location_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (location_box), 
			    window->location_entry, TRUE, TRUE, 5);

	g_signal_connect (G_OBJECT (window->location_entry), 
			  "key_press_event",
			  G_CALLBACK (location_entry_key_press_event_cb),
			  window);

	gtk_widget_show_all (window->location_bar);

	gnome_app_set_contents (GNOME_APP (window->app), scrolled_window);
	gtk_widget_show_all (scrolled_window);

	/* Create the main menu. */

	gnome_app_create_menus_with_data (GNOME_APP (window->app), main_menu, 
					  window);

	/* Create the toolbar. */

	window->toolbar = toolbar = gtk_toolbar_new ();
        gnome_app_fill_toolbar_with_data (GTK_TOOLBAR (toolbar), 
                                          toolbar_data, 
                                          (GtkAccelGroup*) NULL,
                                          window);

	i = 1;

	window->toolbar_add = 
		gtk_toolbar_insert_element (GTK_TOOLBAR (toolbar),
					    GTK_TOOLBAR_CHILD_BUTTON,
					    NULL, 
					    _("Add"), 
					    _("Add files to the archive"),
					    NULL, 
					    create_icon (add_pixbuf),
					    GTK_SIGNAL_FUNC (add_cb), 
					    window,
					    TOOLBAR_SEP1 + i++);
	window->toolbar_extract = 
		gtk_toolbar_insert_element (GTK_TOOLBAR (toolbar),
					    GTK_TOOLBAR_CHILD_BUTTON,
					    NULL, 
					    _("Extract"), 
					    _("Extract files from the archive"),
					    NULL, 
					    create_icon (extract_pixbuf),
					    GTK_SIGNAL_FUNC (dlg_extract), 
					    window,
					    TOOLBAR_SEP1 + i++);
	window->toolbar_view = 
		gtk_toolbar_insert_element (GTK_TOOLBAR (toolbar),
					    GTK_TOOLBAR_CHILD_BUTTON,
					    NULL, 
					    _("View"), 
					    _("View selected file"),
					    NULL, 
					    create_icon (view_pixbuf),
					    GTK_SIGNAL_FUNC (view_or_open_cb), 
					    window,
					    TOOLBAR_SEP1 + i++);
	gnome_app_set_toolbar (GNOME_APP (window->app), GTK_TOOLBAR (toolbar));

	/* Create popup menus. */

	window->file_popup_menu = gtk_menu_new ();
	gnome_app_fill_menu_with_data (GTK_MENU_SHELL (window->file_popup_menu),
				       file_popup_menu_data,
				       (GtkAccelGroup*) NULL,
				       FALSE,
				       0,
				       window);

	/* Create a statusbar. */

        window->statusbar = gnome_appbar_new (FALSE, TRUE, 
                                              GNOME_PREFERENCES_USER);
        gnome_app_set_statusbar (GNOME_APP (window->app), window->statusbar);
        gnome_app_install_appbar_menu_hints (GNOME_APPBAR (window->statusbar),
                                             main_menu);

	/* Data. */

	window->archive = fr_archive_new ();
	g_signal_connect (G_OBJECT (window->archive),
			  "start",
			  G_CALLBACK (_action_started),
			  window);
	g_signal_connect (G_OBJECT (window->archive),
			  "done",
			  G_CALLBACK (_action_performed),
			  window);
	g_signal_connect (G_OBJECT (window->archive), 
			  "progress",
			  G_CALLBACK (window_progress_cb),
			  window);
	g_signal_connect (G_OBJECT (window->archive), 
			  "message",
			  G_CALLBACK (window_message_cb),
			  window);
	g_signal_connect (G_OBJECT (window->archive), 
			  "stoppable",
			  G_CALLBACK (window_stoppable_cb),
			  window);

	fr_archive_set_fake_load_func (window->archive,
				       window_fake_load,
				       window);

	window->sort_method = preferences_get_sort_method ();
	window->sort_type = preferences_get_sort_type ();

	window->list_mode = preferences_get_list_mode ();
	window->current_dir = g_strdup ("/");

	window->mitem_new_archive = file_menu[FILE_MENU_NEW_ARCHIVE].widget;
	window->mitem_open_archive = file_menu[FILE_MENU_OPEN_ARCHIVE].widget;
	window->mitem_save_as_archive = file_menu[FILE_MENU_SAVE_AS_ARCHIVE].widget;
	window->mitem_close = file_menu[FILE_MENU_CLOSE_ARCHIVE].widget;

	window->mitem_archive_prop = file_menu[FILE_MENU_ARCHIVE_PROP].widget;
	window->mitem_move_archive = file_menu[FILE_MENU_MOVE_ARCHIVE].widget;
	window->mitem_copy_archive = file_menu[FILE_MENU_COPY_ARCHIVE].widget;
	window->mitem_rename_archive = file_menu[FILE_MENU_RANAME_ARCHIVE].widget;
	window->mitem_delete_archive = file_menu[FILE_MENU_DELETE_ARCHIVE].widget;

	window->mitem_recents_menu = GTK_MENU_ITEM (file_menu[FILE_MENU_RECENTS_MENU].widget)->submenu;

	window->mitem_add = edit_menu[EDIT_MENU_ADD].widget;
	window->mitem_delete = edit_menu[EDIT_MENU_DELETE].widget;
	window->mitem_extract = edit_menu[EDIT_MENU_EXTRACT].widget;
	window->mitem_open = file_menu[FILE_MENU_OPEN].widget;
	window->mitem_test = file_menu[FILE_MENU_TEST].widget;
	window->mitem_view = file_menu[FILE_MENU_VIEW].widget;
	window->mitem_stop = view_menu[VIEW_MENU_STOP].widget;
	window->mitem_reload = view_menu[VIEW_MENU_RELOAD].widget;

	window->mitem_select_all = edit_menu[EDIT_MENU_SELECT_ALL].widget;
	window->mitem_unselect_all = edit_menu[EDIT_MENU_DESELECT_ALL].widget;

	window->mitem_view_toolbar = view_menu[VIEW_MENU_TOOLBAR].widget;
	window->mitem_view_statusbar = view_menu[VIEW_MENU_STATUSBAR].widget;
	window->mitem_view_flat   = view_list[VIEW_LIST_VIEW_ALL].widget;
	window->mitem_view_as_dir = view_list[VIEW_LIST_AS_DIR].widget;
	set_check_menu_item_state (window, window->mitem_view_as_dir, TRUE);
	for (i = 0; i < 5; i++)
		window->mitem_sort[i] = sort_by_radio_list[i].widget;
	window->mitem_sort_reversed = arrange_menu[ARRANGE_MENU_REVERSED_ORDER].widget;
	window->mitem_password = edit_menu[EDIT_MENU_PASSWORD].widget;

	for (i = 0; i < 8; i++) 
		window->popupmenu_file[i] = file_popup_menu_data[i].widget;

	window->toolbar_new = toolbar_data[TOOLBAR_NEW].widget;
	window->toolbar_open = toolbar_data[TOOLBAR_OPEN].widget;
	window->toolbar_stop = toolbar_data[TOOLBAR_STOP].widget;

	window->open_default_dir = g_strdup (g_get_home_dir ());
	window->add_default_dir = g_strdup (g_get_home_dir ());
	window->extract_default_dir = g_strdup (g_get_home_dir ());
	window->view_folder_after_extraction = FALSE;
	window->folder_to_view = NULL;

	window->give_focus_to_the_list = FALSE;

	window->activity_ref = 0;
	window->activity_timeout_handle = 0;
	window->vd_handle = NULL;

	window->archive_present = FALSE;
	window->archive_new = FALSE;
	window->archive_filename = NULL;

	window->drag_temp_dir = NULL;
	window->drag_temp_dirs = NULL;
	window->drag_file_list = NULL;
	window->drag_file_list_names = NULL;

	window->dropped_file_list = NULL;
	window->add_after_creation = FALSE;
	window->add_after_opening = FALSE;
	window->adding_dropped_files = FALSE;

	window->batch_mode = FALSE;
	window->batch_action_list = NULL;
	window->batch_action = NULL;
	window->extract_interact_use_default_dir = FALSE;
	window->non_interactive = FALSE;

	window->password = NULL;
	window->compression = preferences_get_compression_level ();

	window->convert_data.converting = FALSE;
	window->convert_data.temp_dir = NULL;
	window->convert_data.new_archive = NULL;

	window->stoppable = TRUE;

	/**/

	model = egg_recent_model_new (EGG_RECENT_MODEL_SORT_MRU);
	egg_recent_model_set_filter_mime_types (model, 
						"application/x-tar",
						"application/x-compressed-tar",
						"application/x-bzip-compressed-tar",
						"application/x-lzop-compressed-tar",
						"application/x-arj",
						"application/zip",
						"application/x-zip",
						"application/x-lha",
						"application/x-rar",
						"application/x-rar-compressed",
						"application/x-gzip",
						"application/x-bzip",
						"application/x-compress",
						"application/x-lzop",
						"application/x-zoo",
						NULL);
        egg_recent_model_set_filter_uri_schemes (model, "file", NULL);
	egg_recent_model_set_limit (model, eel_gconf_get_integer (PREF_UI_HISTORY_LEN));

	view = egg_recent_view_gtk_new (window->mitem_recents_menu, NULL);
	window->recent_view = view;
	egg_recent_view_set_model (EGG_RECENT_VIEW (view), model);

	g_object_unref (model);
	window->recent_model = egg_recent_view_get_model (EGG_RECENT_VIEW (view));

	g_signal_connect (G_OBJECT (view), 
			  "activate",
			  G_CALLBACK (open_recent_cb),
			  window);

	/* Update menu items */

	if (window->list_mode == WINDOW_LIST_MODE_FLAT)
		set_check_menu_item_state (window, window->mitem_view_flat, TRUE);
	else
		set_check_menu_item_state (window, window->mitem_view_as_dir, TRUE);
	set_check_menu_item_state (window, window->mitem_sort[window->sort_method], TRUE);
	if (window->sort_type == GTK_SORT_DESCENDING)
		set_check_menu_item_state (window, window->mitem_sort_reversed, TRUE);

	_window_update_title (window);
	_window_update_sensitivity (window);
	window_update_file_list (window);
	_window_update_current_location (window);
	window_update_columns_visibility (window);

	/* Add notification callbacks. */

	i = 0;

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_HISTORY_LEN,
					   pref_history_len_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_TOOLBAR,
					   pref_view_toolbar_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_STATUSBAR,
					   pref_view_statusbar_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_TYPE,
					   pref_show_field_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_SIZE,
					   pref_show_field_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_TIME,
					   pref_show_field_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_PATH,
					   pref_show_field_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_USE_MIME_ICONS,
					   pref_use_mime_icons_changed,
					   window);
	
	/* Give focus to the list. */

	gtk_widget_grab_focus (window->list_view);

	/* Add the window to the window list. */

	window_list = g_list_prepend (window_list, window);
	
	return window;
}


/* -- window_close -- */


static void
_window_remove_notifications (FRWindow *window)
{
	int i;

	for (i = 0; i < GCONF_NOTIFICATIONS; i++)
		if (window->cnxn_id[i] != -1)
			eel_gconf_notification_remove (window->cnxn_id[i]);
}


static void
_window_remove_drag_temp_dirs (FRWindow *window)
{
	GList *scan;

	for (scan = window->drag_temp_dirs; scan; scan = scan->next) 
		if (path_is_dir (scan->data)) {
			char *command;
			
			command = g_strconcat ("rm -rf ",
					       scan->data,
					       NULL);
			gnome_execute_shell (g_get_tmp_dir (), command); 
			g_free (command);
		}

	if (window->drag_temp_dirs != NULL) {
		path_list_free (window->drag_temp_dirs);
		window->drag_temp_dirs = NULL;
	}
}


static void
_window_free_batch_data (FRWindow *window)
{
	GList *scan;

	for (scan = window->batch_action_list; scan; scan = scan->next) {
		FRBatchActionDescription *adata = scan->data;
		if ((adata->data != NULL) && (adata->free_func != NULL))
			(*adata->free_func) (adata->data);
		g_free (adata);
	}

	g_list_free (window->batch_action_list);
	window->batch_action_list = NULL;
	window->batch_action = NULL;
}


static void
gh_unref_pixbuf (gpointer  key,
		 gpointer  value,
		 gpointer  user_data)
{
	g_object_unref (value);
}


void
window_close (FRWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->progress_timeout != 0) {
		g_source_remove (window->progress_timeout);
		window->progress_timeout = 0;
	}

	if (window->hide_progress_timeout != 0) {
		g_source_remove (window->hide_progress_timeout);
		window->hide_progress_timeout = 0;
	}

	if (window->theme_changed_handler_id != 0)
		g_signal_handler_disconnect (icon_theme,
					     window->theme_changed_handler_id);

	if (window->vd_handle != NULL) {
		visit_dir_async_interrupt (window->vd_handle, NULL, NULL);
		window->vd_handle = NULL;
	}

	if (window->current_dir != NULL)
		g_free (window->current_dir);
	if (window->open_default_dir != NULL)
		g_free (window->open_default_dir);
	if (window->add_default_dir != NULL)
		g_free (window->add_default_dir);
	if (window->extract_default_dir != NULL)
		g_free (window->extract_default_dir);
	if (window->archive_filename != NULL)
		g_free (window->archive_filename);

	if (window->password != NULL)
		g_free (window->password);

	g_object_unref (window->archive);
	g_object_unref (window->list_store);
	g_object_unref (window->empty_store);

	if (window->drag_file_list != NULL)
		path_list_free (window->drag_file_list);
	_window_remove_drag_temp_dirs (window);

	if (window->dropped_file_list != NULL) {
		path_list_free (window->dropped_file_list);
		window->dropped_file_list = NULL;
	}

	if (window->drag_file_list_names != NULL) {
		path_list_free (window->drag_file_list_names);
		window->drag_file_list_names = NULL;
	}

	if (window->file_popup_menu != NULL) {
		gtk_widget_destroy (window->file_popup_menu);
		window->file_popup_menu = NULL;
	}

	if (window->folder_to_view != NULL) {
		g_free (window->folder_to_view);
		window->folder_to_view = NULL;
	}

	if (window->recent_view != NULL) {
		g_object_unref (window->recent_view);
		window->recent_view = NULL;
	}

	_window_free_batch_data (window);
	_window_remove_notifications (window);

	/* save preferences. */

	preferences_set_sort_method (window->sort_method);
	preferences_set_sort_type (window->sort_type);
	preferences_set_list_mode (window->list_mode);

	gtk_widget_destroy (window->app);
	window_list = g_list_remove (window_list, window);
	g_free (window);

	if (window_list == NULL) {
		if (pixbuf_hash != NULL) {
			g_hash_table_foreach (pixbuf_hash, 
					      gh_unref_pixbuf,
					      NULL);
			g_hash_table_destroy (pixbuf_hash);
		}

		if (icon_theme != NULL)
			g_object_unref (icon_theme);

		if (file_pixbuf != NULL)
			g_object_unref (file_pixbuf);

		if (folder_pixbuf != NULL)
			g_object_unref (folder_pixbuf);

                gtk_main_quit ();
	}
}


void
window_archive_new (FRWindow   *window, 
		    const char *filename)
{
	g_return_if_fail (window != NULL);

	if (! fr_archive_new_file (window->archive, filename)) {
		GtkWidget *dialog;

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not create the archive"),
						  _("Archive type not supported."),
						  GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						  NULL);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (window->batch_mode) {
			window_archive_close (window);
			_window_update_sensitivity (window);
			_window_update_statusbar_list_info (window);
			window_batch_mode_stop (window);
		}
		
		return;
	}

	if (window->current_dir != NULL)
		g_free (window->current_dir);
	window->current_dir = g_strdup ("/");

	if (window->archive_filename != NULL)
		g_free (window->archive_filename);
	window->archive_filename = g_strdup (filename);

	window->archive_present = TRUE;
	window->archive_new = TRUE;

	window_set_password (window, NULL);

	window_update_file_list (window);
	_window_update_title (window);
	_window_update_sensitivity (window);
	_window_update_current_location (window);

	if (window->add_after_creation) {
		drag_drop_add_file_list (window);
		window->add_after_creation = FALSE;
	}
}


gboolean
window_archive_open (FRWindow   *window,
		     const char *filename,
		     GtkWindow  *parent)
{
	GError   *gerror;
	gboolean  success;

	g_return_val_if_fail (window != NULL, FALSE);

	window_archive_close (window);

	if (window->archive_filename != NULL)
		g_free (window->archive_filename);

	if (! g_path_is_absolute (filename)) {
		char *current_dir;
		current_dir = g_get_current_dir ();
		window->archive_filename = g_strconcat (current_dir, 
							"/", 
							filename, 
							NULL);
		g_free (current_dir);
	} else
		window->archive_filename = g_strdup (filename);

	window->archive_present = FALSE;	
	window->give_focus_to_the_list = TRUE;

	success = fr_archive_load (window->archive, window->archive_filename, &gerror);
	window->add_after_opening = FALSE;

	if (! success) {
		GtkWidget *dialog;
		char *utf8_name, *message;
		char *reason;

		utf8_name = g_locale_to_utf8 (file_name_from_path (window->archive_filename), -1, 0, 0, 0);
		message = g_strdup_printf (_("Could not open \"%s\""), utf8_name);
		g_free (utf8_name);
		reason = gerror != NULL ? gerror->message : "";

		dialog = _gtk_message_dialog_new (parent, 
						  GTK_DIALOG_DESTROY_WITH_PARENT, 
						  GTK_STOCK_DIALOG_ERROR,
						  message,
						  reason,
						  GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						  NULL);
		g_free (message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (window->batch_mode) {
			window_archive_close (window);
			_window_update_sensitivity (window);
			_window_update_statusbar_list_info (window);
			window_batch_mode_stop (window);
		}
	} 

	_window_add_to_recent (window, window->archive_filename, success);

	return success;
}


void
window_archive_save_as (FRWindow      *window, 
			const char    *filename)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (filename != NULL);
	g_return_if_fail (window->archive != NULL);

	g_return_if_fail (window->convert_data.temp_dir == NULL);
	g_return_if_fail (window->convert_data.new_archive == NULL);

	/* create the new archive */

	window->convert_data.new_archive = fr_archive_new ();
	if (! fr_archive_new_file (window->convert_data.new_archive, filename)) {
		GtkWidget *dialog;
		char *utf8_name;
		char *message;

		utf8_name = g_locale_to_utf8 (file_name_from_path (filename), -1, NULL, NULL, NULL);
		message = g_strdup_printf (_("Could not save the archive \"%s\""), file_name_from_path (filename));
		g_free (utf8_name);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  message,
						  _("Archive type not supported."),
						  NULL,
						  GTK_STOCK_OK, GTK_RESPONSE_OK,
						  NULL);
		g_free (message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		
		g_object_unref (window->convert_data.new_archive);
		window->convert_data.new_archive = NULL;

		return;
	}

	g_return_if_fail (window->convert_data.new_archive->command != NULL);

	g_signal_connect (G_OBJECT (window->convert_data.new_archive),
			  "start",
			  G_CALLBACK (_action_started),
			  window);
	g_signal_connect (G_OBJECT (window->convert_data.new_archive),
			  "done",
			  G_CALLBACK (convert__action_performed),
			  window);
	g_signal_connect (G_OBJECT (window->convert_data.new_archive), 
			  "progress",
			  G_CALLBACK (window_progress_cb),
			  window);
	g_signal_connect (G_OBJECT (window->convert_data.new_archive), 
			  "message",
			  G_CALLBACK (window_message_cb),
			  window);
	g_signal_connect (G_OBJECT (window->convert_data.new_archive), 
			  "stoppable",
			  G_CALLBACK (window_stoppable_cb),
			  window);

	window->convert_data.converting = TRUE;

	fr_process_clear (window->archive->process);

	window->convert_data.temp_dir = _get_temp_dir_name ();
	ensure_dir_exists (window->convert_data.temp_dir, 0700);

	fr_command_extract (window->archive->command,
			    NULL,
			    window->convert_data.temp_dir,
			    TRUE,
			    FALSE,
			    FALSE,
			    window->password);

	fr_process_start (window->archive->process);
}


void
window_archive_reload (FRWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->archive_new)
		return;

	fr_archive_reload (window->archive);
}


void
window_archive_rename (FRWindow   *window, 
		       const char *filename)
{
	g_return_if_fail (window != NULL);

	if (window->archive_new) 
		window_archive_new (window, filename);
	else {
		fr_archive_rename (window->archive, filename);

		if (window->archive_filename != NULL)
			g_free (window->archive_filename);
		window->archive_filename = g_strdup (filename);

		_window_update_title (window);
		_window_add_to_recent (window, window->archive_filename, TRUE);
	}
}


/**/


static void
add_files_done_cb (gpointer data)
{
	FRWindow *window = data;

	window_pop_message (window);
	window_stop_activity_mode (window);

	visit_dir_handle_free (window->vd_handle);
	window->vd_handle = NULL;
}


void
window_archive_add_with_wildcard (FRWindow      *window,
				  const char    *include_files,
				  const char    *exclude_files,
				  const char    *base_dir,
				  gboolean       update,
				  gboolean       recursive,
				  gboolean       follow_links,
				  gboolean       same_fs,
				  gboolean       no_backup_files,
				  gboolean       no_dot_files,
				  gboolean       ignore_case,
				  const char    *password,
				  FRCompression  compression)
{
	g_return_if_fail (window->vd_handle == NULL);

	_action_started (window->archive, FR_ACTION_GET_LIST, window);

	window->vd_handle = fr_archive_add_with_wildcard (window->archive,
							  include_files,
							  exclude_files,
							  base_dir,
							  update,
							  recursive,
							  follow_links,
							  same_fs,
							  no_backup_files,
							  no_dot_files,
							  ignore_case,
							  password,
							  compression,
							  add_files_done_cb,
							  window);
}


void
window_archive_add_directory (FRWindow      *window,
			      const char    *directory,
			      const char    *base_dir,
			      gboolean       update,
			      const char    *password,
			      FRCompression  compression)
{
	g_return_if_fail (window->vd_handle == NULL);

	_action_started (window->archive, FR_ACTION_GET_LIST, window);

	window->vd_handle = fr_archive_add_directory (window->archive, 
						      directory,
						      base_dir, 
						      update,
						      password,
						      compression,
						      add_files_done_cb,
						      window);
}


/* -- window_archive_extract -- */


void
window_archive_extract (FRWindow   *window,
			GList      *file_list,
			const char *extract_to_dir,
			gboolean    skip_older,
			gboolean    overwrite,
			gboolean    junk_paths,
			const char *password)
{
	gboolean do_not_extract = FALSE;

	if (! path_is_dir (extract_to_dir)) {
		if (! force_directory_creation) {
			GtkWidget *d;
			int        r;
		
			d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     _("Destination folder does not exist.  Do you want to create it?"),
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);
			
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			if (r != GTK_RESPONSE_YES) 
				do_not_extract = TRUE;
		}

		if (! do_not_extract && ! ensure_dir_exists (extract_to_dir, 0755)) {
			GtkWidget  *d;
			const char *error;
			char       *message;

			error = gnome_vfs_result_to_string (gnome_vfs_result_from_errno ());
			message = g_strdup_printf (_("Could not create the destination folder: %s."), error);
			d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_ERROR,
						     _("Extraction not performed"),
						     message,
						     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
						     NULL);
			g_free (message);
						   
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			return;
		}
	} 
	
	if (do_not_extract) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Extraction not performed"),
					     NULL,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		return;
	}

	fr_archive_extract (window->archive,
			    file_list,
			    extract_to_dir,
			    skip_older,
			    overwrite,
			    junk_paths,
			    password);
}


void
window_archive_close (FRWindow *window)
{
	g_return_if_fail (window != NULL);

	window_set_password (window, NULL);
	_window_remove_drag_temp_dirs (window);

	window->archive_new = FALSE;
	window->archive_present = FALSE;

	_window_update_title (window);
	_window_update_sensitivity (window);
	window_update_file_list (window);
	_window_update_current_location (window);
	_window_update_statusbar_list_info (window);
}


void
window_set_password (FRWindow   *window,
		     const char *password)
{
	g_return_if_fail (window != NULL);
	
	if (window->password != NULL) {
		g_free (window->password);
		window->password = NULL;
	}

	if ((password != NULL) && (password[0] != 0))
		window->password = g_strdup (password);
}


void
window_go_to_location (FRWindow *window, const char *path)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (path != NULL);

	if (window->current_dir)
		g_free (window->current_dir);

	if (path[strlen (path) - 1] != '/')
		window->current_dir = g_strconcat (path, "/", NULL);
	else
		window->current_dir = g_strdup (path);

	window_update_file_list (window);
	_window_update_current_location (window);
}


void
window_go_up_one_level (FRWindow *window)
{
	g_return_if_fail (window != NULL);

	_go_up_one_level (window);
	window_update_file_list (window);
	_window_update_current_location (window);
}


void
window_set_list_mode (FRWindow       *window, 
		      WindowListMode  list_mode)
{
	g_return_if_fail (window != NULL);

	window->list_mode = list_mode;
	window_update_file_list (window);
	_window_update_current_location (window);
}


/* -- window_get_file_list_selection -- */


static GList *
get_dir_list (FRWindow *window,
	      FileData *fdata)
{
	GList *list;
	GList *scan;
	char  *dirname;
	int    dirname_l;

	dirname = g_strconcat (window->current_dir,
			       fdata->list_name,
			       "/",
			       NULL);
	dirname_l = strlen (dirname);

	list = NULL;
	scan = window->archive->command->file_list;
	for (; scan; scan = scan->next) {
		FileData *fd = scan->data;

		if (strncmp (fd->full_path, dirname, dirname_l) == 0)
			list = g_list_prepend (list, 
					       g_strdup (fd->original_path));
	}
	g_free (dirname);

	return g_list_reverse (list);
}


static void 
add_selected (GtkTreeModel *model,
	      GtkTreePath  *path,
	      GtkTreeIter  *iter,
	      gpointer      data)
{
	GList    **list = data;
	FileData  *fdata;
        
        gtk_tree_model_get (model, iter,
                            COLUMN_FILE_DATA, &fdata,
                            -1);
	*list = g_list_prepend (*list, fdata);
}


GList *
window_get_file_list_selection (FRWindow *window,
				gboolean  recursive,
				gboolean *has_dirs)
{
	GtkTreeSelection *selection;
	GList            *selections = NULL, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected, &selections);

	list = NULL;
        for (scan = selections; scan; scan = scan->next) {
                FileData *fd = scan->data;

		if (!fd)
			continue;

		if (fd->is_dir) {
			if (has_dirs != NULL)
				*has_dirs = TRUE;

			if (recursive)
				list = g_list_concat (list, get_dir_list (window, fd));
		} else
			list = g_list_prepend (list, g_strdup (fd->original_path));
        }
	if (selections)
		g_list_free (selections);

        return g_list_reverse (list);
}


/* -- window_get_file_list_pattern -- */


GList *
window_get_file_list_pattern (FRWindow    *window,
			      const char  *pattern)
{
	GList  *list, *scan;
	char  **patterns;

	g_return_val_if_fail (window != NULL, NULL);

	patterns = search_util_get_patterns (pattern);

	list = NULL;
        scan = window->archive->command->file_list;
        for (; scan; scan = scan->next) {
                FileData *fd = scan->data;
		char     *utf8_name;

		/* FIXME: only files in the current location ? */

		if (!fd)
			continue;

		utf8_name = g_locale_to_utf8 (fd->name, -1, NULL, NULL, NULL);
		if (match_patterns (patterns, utf8_name, FNM_CASEFOLD))
			list = g_list_prepend (list, 
					       g_strdup (fd->original_path));
		g_free (utf8_name);
        }

	if (patterns != NULL) 
		g_strfreev (patterns);

        return g_list_reverse (list);
}


/* -- window_start/stop_activity_mode -- */


static int
activity_cb (gpointer data)
{
	FRWindow *window = data;
	
	if ((window->pd_progress_bar != NULL) && window->progress_pulse)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (window->pd_progress_bar)); 
	
        return TRUE;
}


void
window_start_activity_mode (FRWindow *window)
{
        g_return_if_fail (window != NULL);

        if (window->activity_ref++ > 0)
                return;

        window->activity_timeout_handle = gtk_timeout_add (ACTIVITY_DELAY,
							   activity_cb, 
							   window);
	_window_update_sensitivity (window);
}


void
window_stop_activity_mode (FRWindow *window)
{
        g_return_if_fail (window != NULL);

        if (--window->activity_ref > 0)
                return;

        if (window->activity_timeout_handle == 0)
                return;

        gtk_timeout_remove (window->activity_timeout_handle);
        window->activity_timeout_handle = 0;
	
	if (window->progress_dialog != NULL)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->pd_progress_bar), 0.0);
	_window_update_sensitivity (window);
}


void
window_view_last_output (FRWindow   *window,
			 const char *title)
{
	GtkWidget     *dialog;
	GtkWidget     *vbox;
	GtkWidget     *text_view;
	GtkWidget     *scrolled;
	GtkTextBuffer *text_buf;
	GtkTextIter    iter;
	GList         *scan;

	if (title == NULL)
		title = _("Last Output");

	dialog = gtk_dialog_new_with_buttons (title,
					      GTK_WINDOW (window->app),
					      GTK_DIALOG_DESTROY_WITH_PARENT, 
					      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
					      NULL);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
        gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 6);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 8);

	gtk_widget_set_size_request (dialog, 500, 300);

	/* Add text */

	scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_AUTOMATIC, 
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_ETCHED_IN);

	text_buf = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buf, "monospace",
				    "family", "monospace", NULL);
	gtk_text_buffer_get_iter_at_offset (text_buf, &iter, 0);
	scan = window->archive->process->raw_output;
	for (; scan; scan = scan->next) {
		char *line = scan->data;
		char *utf8_line;
		gsize bytes_written;

		utf8_line = g_locale_to_utf8 (line, -1, NULL, &bytes_written, NULL);
		gtk_text_buffer_insert_with_tags_by_name (text_buf,
							  &iter,
							  utf8_line,
							  bytes_written,
							  "monospace", NULL);
		g_free (utf8_line);
		gtk_text_buffer_insert (text_buf, &iter, "\n", 1);
	}
	text_view = gtk_text_view_new_with_buffer (text_buf);
	g_object_unref (text_buf);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);

	/**/

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

	gtk_container_add (GTK_CONTAINER (scrolled), text_view);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled,
			    TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    vbox,
			    TRUE, TRUE, 0);

	/* signals */

	g_signal_connect (G_OBJECT (dialog),
			  "response", 
			  G_CALLBACK (gtk_widget_destroy), 
			  NULL);
	
	gtk_widget_show_all (dialog);
}


/* -- window_view_file -- */


static void
view_file (FRArchive   *archive,
	   FRAction     action, 
	   FRProcError *error,
	   gpointer     callback_data)
{
	ViewerData         *vdata = callback_data;
	const char         *mime_type;
	GnomeVFSMimeAction *mime_action;

	g_signal_handlers_disconnect_matched (G_OBJECT (archive), 
					      G_SIGNAL_MATCH_DATA, 
					      0, 
					      0, NULL, 
					      0, vdata);

	if (error->type != FR_PROC_ERROR_NONE) {
		viewer_done (vdata);
		return;
	}

	mime_type = gnome_vfs_mime_type_from_name_or_default (vdata->filename, NULL);
	mime_action = gnome_vfs_mime_get_default_action (mime_type);

	if ((mime_type == NULL) || (mime_action == NULL)) {
		dlg_viewer (vdata->window, vdata->filename);
		viewer_list = g_list_prepend (viewer_list, vdata);
	} else {
		FRProcess  *proc;

		proc = fr_process_new ();
		proc->term_on_stop = FALSE;
		vdata->process = proc;
		
		fr_process_begin_command (proc, "nautilus");
		fr_process_add_arg (proc, "--sm-disable"); 
		fr_process_add_arg (proc, "--no-desktop");
		fr_process_add_arg (proc, "--no-default-window");
		fr_process_add_arg (proc, vdata->e_filename);
		fr_process_end_command (proc);

		viewer_list = g_list_prepend (viewer_list, vdata);
		fr_process_start (proc);
	}

	if (mime_action != NULL)
		gnome_vfs_mime_action_free (mime_action);
}


void 
window_view_file (FRWindow *window, 
		  char     *file)
{
	GList      *file_list;
	ViewerData *vdata;

        g_return_if_fail (window != NULL);

	vdata = g_new (ViewerData, 1);
	vdata->window = window;
	vdata->process = NULL;
	vdata->temp_dir = _get_temp_dir_name ();
	ensure_dir_exists (vdata->temp_dir, 0700);

	vdata->filename = g_strconcat (vdata->temp_dir,
				       "/",
				       file,
				       NULL);
	vdata->e_filename = shell_escape (vdata->filename);

	g_signal_connect (G_OBJECT (window->archive), 
			  "done",
			  G_CALLBACK (view_file),
			  vdata);

	file_list = g_list_prepend (NULL, file);
	fr_archive_extract (window->archive,
			    file_list,
			    vdata->temp_dir,
			    FALSE,
			    TRUE,
			    FALSE,
			    window->password);
	g_list_free (file_list);
}


/* -- window_open_files -- */


static void
open_files (FRArchive   *archive,
	    FRAction     action, 
	    FRProcError *error,
	    gpointer     callback_data)
{
	CommandData  *cdata = callback_data;
	FRProcess    *proc;
	GList        *scan;

	g_signal_handlers_disconnect_matched (G_OBJECT (archive), 
					      G_SIGNAL_MATCH_DATA, 
					      0, 
					      0, NULL, 
					      0,
					      cdata);

	if (error->type != FR_PROC_ERROR_NONE) {
		command_done (cdata);
		return;
	}

	proc = fr_process_new ();
	fr_process_use_standard_locale (proc, FALSE);
	proc->term_on_stop = FALSE;
	cdata->process = proc;

	fr_process_begin_command (proc, cdata->command);
	for (scan = cdata->file_list; scan; scan = scan->next) {
		char *filename = scan->data;
		fr_process_add_arg (proc, filename);
	}
	fr_process_end_command (proc);

	command_list = g_list_prepend (command_list, cdata);
	fr_process_start (proc);
}


void
window_open_files (FRWindow *window, 
		   char     *command,
		   GList    *file_list)
{
	CommandData *cdata;
	GList       *scan;

        g_return_if_fail (window != NULL);

	cdata = g_new (CommandData, 1);
	cdata->window = window;
	cdata->process = NULL;
	cdata->command = g_strdup (command);
	cdata->file_list = NULL;
	cdata->temp_dir = _get_temp_dir_name ();
	ensure_dir_exists (cdata->temp_dir, 0700);

	for (scan = file_list; scan; scan = scan->next) {
		gchar *file = scan->data;
		gchar *filename;

		filename = g_strconcat (cdata->temp_dir,
					"/",
					file,
					NULL);
		cdata->file_list = g_list_prepend (cdata->file_list,
						   shell_escape (filename));
		g_free (filename);
	}

	g_signal_connect (G_OBJECT (window->archive), 
			  "done",
			  G_CALLBACK (open_files),
			  cdata);

	fr_archive_extract (window->archive,
			    file_list,
			    cdata->temp_dir,
			    FALSE,
			    TRUE,
			    FALSE,
			    window->password);
}


void
window_view_or_open_file (FRWindow *window, 
			  gchar    *filename)
{
	GnomeVFSMimeAction *action;
	const char         *mime_type;
	char               *command;
	GList              *singleton;

	
	if (window->activity_ref > 0)
		return;
	
	mime_type = gnome_vfs_mime_type_from_name_or_default (filename, NULL);
	if (mime_type == NULL) {
		window_view_file (window, filename);
		return;
	}
		
	action = gnome_vfs_mime_get_default_action (mime_type);
		
	if (action == NULL) {
		dlg_viewer_or_app (window, filename);
		return;
	}
		
	switch (action->action_type) {
	case GNOME_VFS_MIME_ACTION_TYPE_NONE:
	case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
		window_view_file (window, filename);
		break;
		
	case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
		command = application_get_command (action->action.application);
		singleton = g_list_append (NULL, filename);
		window_open_files (window, command, singleton);
		g_list_free (singleton);
		g_free (command);
		break;
	}
	gnome_vfs_mime_action_free (action);
}


void
window_set_open_default_dir (FRWindow *window,
			     gchar    *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	if (window->open_default_dir != NULL) 
		g_free (window->open_default_dir);
	window->open_default_dir = g_strdup (default_dir);
}


void
window_set_add_default_dir (FRWindow *window,
			    gchar    *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	if (window->add_default_dir != NULL) 
		g_free (window->add_default_dir);
	window->add_default_dir = g_strdup (default_dir);
}


void
window_set_extract_default_dir (FRWindow *window,
				gchar    *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	/* do not change this dir while it's used by the non-interactive
	 * extraction operation. */
	if (window->extract_interact_use_default_dir)
		return;

	if (window->extract_default_dir != NULL) 
		g_free (window->extract_default_dir);
	window->extract_default_dir = g_strdup (default_dir);
}


void
window_set_default_dir (FRWindow *window,
			gchar    *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	window_set_open_default_dir    (window, default_dir);
	window_set_add_default_dir     (window, default_dir);
	window_set_extract_default_dir (window, default_dir);
}


void
window_update_columns_visibility (FRWindow *window)
{
	GtkTreeView       *tree_view = GTK_TREE_VIEW (window->list_view);
	GtkTreeViewColumn *column;

	column = gtk_tree_view_get_column (tree_view, 1);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_SIZE));

	column = gtk_tree_view_get_column (tree_view, 2);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_TYPE));

	column = gtk_tree_view_get_column (tree_view, 3);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_TIME));

	column = gtk_tree_view_get_column (tree_view, 4);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_PATH));
}


void
window_set_toolbar_visibility (FRWindow   *window,
			       gboolean    visible)
{
	if (visible)
		gtk_widget_show (window->toolbar->parent);
	else
		gtk_widget_hide (window->toolbar->parent);
}


void
window_set_statusbar_visibility  (FRWindow   *window,
				  gboolean    visible)
{
	if (visible) 
		gtk_widget_show (window->statusbar);
	else
		gtk_widget_hide (window->statusbar);
}


/* -- batch mode procedures -- */


void
window_batch_mode_clear (FRWindow *window)
{
	g_return_if_fail (window != NULL);
	_window_free_batch_data (window);
}


void
window_batch_mode_add_action (FRWindow      *window,
			      FRBatchAction  action,
			      void          *data,
			      GFreeFunc      free_func)
{
	FRBatchActionDescription *a_desc;

	g_return_if_fail (window != NULL);

	a_desc = g_new (FRBatchActionDescription, 1);
	a_desc->action    = action;
	a_desc->data      = data;
	a_desc->free_func = free_func;

	window->batch_action_list = g_list_append (window->batch_action_list,
						   a_desc);
}


void
window_batch_mode_add_next_action (FRWindow      *window,
				   FRBatchAction  action,
				   void          *data,
				   GFreeFunc      free_func)
{
	FRBatchActionDescription *a_desc;
	GList                    *list, *current;

	g_return_if_fail (window != NULL);

	a_desc = g_new (FRBatchActionDescription, 1);
	a_desc->action    = action;
	a_desc->data      = data;
	a_desc->free_func = free_func;

	list = window->batch_action_list;
	current = window->batch_action;

	/* insert after current */

	if (current == NULL)
		list = g_list_prepend (list, a_desc);

	else if (current->next == NULL)
		list = g_list_append (list, a_desc);

	else { 
		GList *node;
		
		node = g_list_append (NULL, a_desc);
		node->next = current->next;
		node->next->prev = node;
		node->prev = current;
		current->next = node;
	}

	window->batch_action_list = list;
}


typedef struct {
	char  *archive_name;
	GList *file_list;
} OpenAndAddData;


void
open_and_add_data_free (OpenAndAddData *adata)
{
	if (adata == NULL)
		return;

	if (adata->archive_name != NULL)
		g_free (adata->archive_name);
	g_free (adata);
}


static void
_window_batch_start_current_action (FRWindow *window)
{
	FRBatchActionDescription *action;
	OpenAndAddData           *adata;

	if (window->batch_action == NULL) {
		window->batch_mode = FALSE;
		return;
	}

	action = (FRBatchActionDescription *) window->batch_action->data;
	switch (action->action) {
	case FR_BATCH_ACTION_OPEN:
		window_archive_open (window, (char*) action->data, GTK_WINDOW (window->app));
		break;

	case FR_BATCH_ACTION_OPEN_AND_ADD:
		adata = (OpenAndAddData *) action->data;
		if (! path_is_file (adata->archive_name)) {
			if (window->dropped_file_list != NULL)
				path_list_free (window->dropped_file_list);
			window->dropped_file_list = path_list_dup (adata->file_list);
			window->add_after_creation = TRUE;
			window_archive_new (window, adata->archive_name);

		} else {
			window->add_after_opening = TRUE;
			window_batch_mode_add_next_action (window,
							   FR_BATCH_ACTION_ADD,
							   path_list_dup (adata->file_list),
							   (GFreeFunc) path_list_free);
			window_archive_open (window, adata->archive_name, GTK_WINDOW (window->app));
		}
		break;

	case FR_BATCH_ACTION_ADD:
		if (window->dropped_file_list != NULL)
			path_list_free (window->dropped_file_list);
		window->dropped_file_list = path_list_dup ((GList*) action->data);

		drag_drop_add_file_list (window);
		break;

	case FR_BATCH_ACTION_ADD_INTERACT:
		window_push_message (window, _("Add files to an archive"));
		dlg_batch_add_files (window, (GList*) action->data);
		break;

	case FR_BATCH_ACTION_EXTRACT:
		window_archive_extract (window,
					NULL,
					(char*) action->data,
					FALSE,
					TRUE,
					FALSE,
					window->password);
		break;

	case FR_BATCH_ACTION_EXTRACT_INTERACT:
		if (window->extract_interact_use_default_dir 
		    && (window->extract_default_dir != NULL))
			window_archive_extract (window,
						NULL,
						window->extract_default_dir,
						FALSE,
						TRUE,
						FALSE,
						window->password);
		else {
			window_push_message (window, _("Extract archive"));
			dlg_extract (NULL, window);
		}
		break;

	case FR_BATCH_ACTION_CLOSE:
		window_close (window);
		break;
	}
}


void
window_batch_mode_start (FRWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->batch_mode) 
		return;

	if (window->batch_action_list == NULL)
		return;

	gtk_widget_hide (window->app);

	window->batch_mode = TRUE;
	window->batch_action = window->batch_action_list;
	_window_batch_start_current_action (window);
}


void
window_batch_mode_stop (FRWindow *window)
{
	if (! window->batch_mode) 
		return;

	window->extract_interact_use_default_dir = FALSE;
	window->batch_mode = FALSE;

	if (window->non_interactive)
		window_close (window);
	else {
		gtk_widget_show (window->app); 
		window_archive_close (window);
	}
}


void
window_archive__open_extract (FRWindow   *window, 
			      const char *filename,
			      const char *dest_dir)
{
	window->non_interactive = TRUE;

	window_batch_mode_add_action (window,
				      FR_BATCH_ACTION_OPEN,
				      g_strdup (filename),
				      (GFreeFunc) g_free);
	if (dest_dir != NULL) 
		window_batch_mode_add_action (window,
					      FR_BATCH_ACTION_EXTRACT,
					      g_strdup (dest_dir),
					      (GFreeFunc) g_free);
	else
		window_batch_mode_add_action (window,
					      FR_BATCH_ACTION_EXTRACT_INTERACT,
					      NULL,
					      NULL);
}


void
window_archive__open_add (FRWindow   *window, 
			  const char *archive,
			  GList      *file_list)
{
	window->non_interactive = TRUE;

	if (archive != NULL) {
		OpenAndAddData *adata;

		adata = g_new (OpenAndAddData, 1);
		adata->archive_name = g_strdup (archive);
		adata->file_list = file_list;
		window_batch_mode_add_action (window,
					      FR_BATCH_ACTION_OPEN_AND_ADD,
					      adata,
					      (GFreeFunc) open_and_add_data_free);
	} else 
		window_batch_mode_add_action (window,
					      FR_BATCH_ACTION_ADD_INTERACT,
					      file_list,
					      NULL);
}


void
window_archive__close (FRWindow   *window)
{
	window_batch_mode_add_action (window,
				      FR_BATCH_ACTION_CLOSE,
				      NULL,
				      NULL);
}
