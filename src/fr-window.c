/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2007 Free Software Foundation, Inc.
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
#include <adwaita.h>
#include <math.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "dlg-batch-add.h"
#include "dlg-delete.h"
#include "dlg-extract.h"
#include "dlg-open-with.h"
#include "dlg-ask-password.h"
#include "dlg-package-installer.h"
#include "dlg-update.h"
#include "fr-marshal.h"
#include "fr-archive.h"
#include "fr-command.h"
#include "fr-error.h"
#include "fr-new-archive-dialog.h"
#include "fr-window.h"
#include "fr-window-actions-entries.h"
#include "fr-file-data.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "fr-init.h"
#include "gtk-utils.h"
#include "open-file.h"
#include "typedefs.h"

#define LAST_OUTPUT_SCHEMA_NAME "LastOutput"
#define MAX_HISTORY_LEN 5
#define ACTIVITY_DELAY 100
#define ACTIVITY_PULSE_STEP (0.033)
#define MAX_MESSAGE_LENGTH 50

#define PROGRESS_DIALOG_DEFAULT_WIDTH 600
#define SHOW_PROGRESS_DELAY 500
#undef  LOG_PROGRESS

#define HIDE_PROGRESS_DELAY 200
#define DEFAULT_NAME_COLUMN_WIDTH 250
#define SIZE_COLUMN_WIDTH 150
#define TIME_COLUMN_WIDTH 250
#define OTHER_COLUMN_WIDTH 150
#define RECENT_ITEM_MAX_WIDTH 25

#define DEF_WIN_WIDTH 600
#define DEF_WIN_HEIGHT 480
#define DEF_SIDEBAR_WIDTH 200

#define EMPTY_STATUS "empty-status"
#define ARCHIVE_CONTENT "archive-content"
#define LOADING_ARCHIVE "loading-archive"


typedef struct {
	FrBatchActionType type;
	void *            data;
	GFreeFunc         free_func;
} FrBatchAction;


typedef struct {
	FrWindow    *window;
	GList       *file_list;
	GFile       *destination;
	char        *base_dir;
	gboolean     skip_older;
	FrOverwrite  overwrite;
	gboolean     junk_paths;
	char        *password;
	gboolean     ask_to_open_destination;
	gboolean     force_directory_creation;
} ExtractData;


typedef enum {
	FR_WINDOW_AREA_MENUBAR,
	FR_WINDOW_AREA_TOOLBAR,
	FR_WINDOW_AREA_LOCATIONBAR,
	FR_WINDOW_AREA_CONTENTS,
	FR_WINDOW_AREA_FILTERBAR,
	FR_WINDOW_AREA_STATUSBAR,
} FrWindowArea;


typedef enum {
	DIALOG_RESPONSE_NONE = 1,
	DIALOG_RESPONSE_OPEN_ARCHIVE,
	DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER,
	DIALOG_RESPONSE_ADD_TO_ARCHIVE,
} DialogResponse;


/* -- FrClipboardData -- */


typedef struct {
	int            refs;
	GFile         *file;
	char          *password;
	FrClipboardOp  op;
	char          *base_dir;
	GList         *files;
	GFile         *tmp_dir;
	char          *current_dir;
} FrClipboardData;


static FrClipboardData*
fr_clipboard_data_new (void)
{
	FrClipboardData *data;

	data = g_new0 (FrClipboardData, 1);
	data->refs = 1;

	return data;
}


static FrClipboardData *
fr_clipboard_data_ref (FrClipboardData *clipboard_data)
{
	clipboard_data->refs++;
	return clipboard_data;
}


static void
fr_clipboard_data_unref (FrClipboardData *clipboard_data)
{
	if (clipboard_data == NULL)
		return;
	if (--clipboard_data->refs > 0)
		return;

	_g_object_unref (clipboard_data->file);
	g_free (clipboard_data->password);
	g_free (clipboard_data->base_dir);
	_g_object_unref (clipboard_data->tmp_dir);
	g_free (clipboard_data->current_dir);
	g_list_free_full (clipboard_data->files, g_free);
	g_free (clipboard_data);
}


static FrClipboardData *
fr_clipboard_data_dup (FrClipboardData *other)
{
	FrClipboardData *data;

	data = fr_clipboard_data_new ();
	data->file = _g_object_ref (other->file);
	data->files = _g_string_list_dup (other->files);
	data->op = other->op;
	data->base_dir = g_strdup (other->base_dir);

	return data;
}


static void
fr_clipboard_data_set_password (FrClipboardData *clipboard_data,
				const char      *password)
{
	if (clipboard_data->password != password)
		g_free (clipboard_data->password);
	if (password != NULL)
		clipboard_data->password = g_strdup (password);
}

/* Internal clipboard used to copy and paste files between File Roller windows. */
static FrClipboardData *app_clipboard = NULL;

/**/

enum {
	ARCHIVE_LOADED,
	PROGRESS,
	READY,
	LAST_SIGNAL
};

static guint fr_window_signals[LAST_SIGNAL] = { 0 };

typedef struct {
	GtkWidget         *layout;
	GtkWidget         *list_view;
	GtkListStore      *list_store;
	GtkWidget         *tree_view;
	GtkTreeStore      *tree_store;
	GtkWidget         *headerbar;
	GtkWidget         *location_bar;
	GtkWidget         *location_entry;
	GtkWidget         *location_label;
	GtkWidget         *filter_bar;
	GtkWidget         *filter_entry;
	GtkWidget         *paned;
	GtkWidget         *sidepane;
	GtkWidget         *content_stack;
	GtkTreePath       *list_hover_path;
	GtkTreeViewColumn *filename_column;
	GtkWindowGroup    *window_group;
	GHashTable        *named_dialogs;

	gboolean         filter_mode;
	gint             current_view_length;

	GtkWidget *      up_arrows[5];
	GtkWidget *      down_arrows[5];

	FrAction         action;
	gboolean         archive_present;
	gboolean         archive_new;        /* A new archive has been created
					      * but it doesn't contain any
					      * file yet.  The real file will
					      * be created only when the user
					      * adds some file to the
					      * archive.*/
	gboolean         reload_archive;

	GFile *          archive_file;
	GFile *          open_default_dir;    /* default directory to be used
					       * in the Open dialog. */
	GFile *          add_default_dir;     /* default directory to be used
					       * in the Add dialog. */
	GFile *          extract_default_dir; /* default directory to be used
					       * in the Extract dialog. */
	gboolean         freeze_default_dir;
	gboolean         destroy_with_error_dialog;
	gboolean         destroy_with_confirmation_dialog;

	FrBatchAction    current_action;

	gboolean         give_focus_to_the_list;
	gboolean         single_click;

	FrWindowSortMethod sort_method;
	GtkSortType      sort_type;

	char *           last_location;
	char *           location_before_filter;

	gboolean         view_sidebar;
	FrWindowListMode list_mode;
	FrWindowListMode last_list_mode;
	GList *          history;
	GList *          history_current;
	char *           password;
	char *           second_password;
	gboolean         encrypt_header;
	FrCompression    compression;
	guint            volume_size;

	guint            activity_timeout_handle;   /* activity timeout
						     * handle. */
	gint             activity_ref;              /* when > 0 some activity
						     * is present. */

	gboolean         stoppable;
	gboolean         closing;
	gboolean         notify;
	gboolean         populating_file_list;

	FrClipboardData *clipboard_data;

	FrArchive       *copy_from_archive;
	GFile           *saving_file;

	GtkWidget        *file_popup_menu;
	GtkWidget        *folder_popup_menu;
	GtkWidget        *sidebar_folder_popup_menu;

	/* dragged files data */

	GFile            *drag_destination_folder;
	char             *drag_base_dir;
	GError           *drag_error;
	GList            *drag_file_list;        /* the list of files we are
						  * dragging*/
	gboolean	  dnd_extract_is_running;
	gboolean	  dnd_extract_finished_with_error;

	/* progress dialog data */

	GtkWidget        *progress_dialog;
	GtkWidget        *pd_action;
	GtkWidget        *pd_message;
	GtkWidget        *pd_progress_bar;
	GtkWidget        *pd_progress_box;
	gboolean          progress_pulse;
	guint             progress_timeout;  /* Timeout to display the progress dialog. */
	guint             hide_progress_timeout;  /* Timeout to hide the progress dialog. */
	GFile            *pd_last_archive;
	GFile            *working_archive;
	double            pd_last_fraction;
	char             *pd_last_message;
	gboolean          use_progress_dialog;
	char             *custom_action_message;

	/* update dialog data */

	gpointer          update_dialog;
	GList            *open_files;

	/* batch mode data */

	gboolean          batch_mode;          /* whether we are in a non interactive
						* mode. */
	GList            *batch_action_list;   /* FrBatchAction * elements */
	GList            *batch_action;        /* current action. */
	char             *batch_title;

	/* misc */

	GCancellable     *cancellable;

	GSettings        *settings_listing;
	GSettings        *settings_ui;
	GSettings        *settings_general;
	GSettings        *settings_dialogs;
	GSettings        *settings_nautilus;

	gboolean          update_dropped_files;
	gboolean          batch_adding_one_file;

	GtkWindow        *load_error_parent_window;
	gboolean          showing_error_dialog;

	GFile            *last_extraction_destination;
	GList            *last_extraction_files_first_level; /* GFile list */
} FrWindowPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrWindow, fr_window, GTK_TYPE_APPLICATION_WINDOW)


/* -- fr_window_free_private_data -- */


static void
fr_batch_action_free (FrBatchAction *action) {
	if ((action->data != NULL) && (action->free_func != NULL))
		(*action->free_func) (action->data);
	g_free (action);
}


static void
fr_window_free_batch_data (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *scan;

	for (scan = private->batch_action_list; scan; scan = scan->next)
		fr_batch_action_free ((FrBatchAction *) scan->data);
	g_list_free (private->batch_action_list);
	private->batch_action_list = NULL;
	private->batch_action = NULL;

	fr_window_reset_current_action (window);
}


static void
fr_window_clipboard_remove_file_list (FrWindow *window,
				      GList    *file_list)
{
	GList *scan1;

	if (app_clipboard == NULL)
		return;

	if (file_list == NULL) {
		fr_clipboard_data_unref (app_clipboard);
		app_clipboard = NULL;
		return;
	}

	for (scan1 = file_list; scan1; scan1 = scan1->next) {
		const char *name1 = scan1->data;
		GList      *scan2;

		for (scan2 = app_clipboard->files; scan2;) {
			const char *name2 = scan2->data;

			if (strcmp (name1, name2) == 0) {
				GList *tmp = scan2->next;
				app_clipboard->files = g_list_remove_link (app_clipboard->files, scan2);
				g_free (scan2->data);
				g_list_free (scan2);
				scan2 = tmp;
			}
			else
				scan2 = scan2->next;
		}
	}

	if (app_clipboard->files == NULL) {
		fr_clipboard_data_unref (app_clipboard);
		app_clipboard = NULL;
	}
}


static void
fr_window_history_clear (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->history != NULL)
		_g_string_list_free (private->history);
	private->history = NULL;
	private->history_current = NULL;
	g_free (private->last_location);
	private->last_location = NULL;
}


static void
fr_window_free_open_files (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *scan;

	for (scan = private->open_files; scan; scan = scan->next) {
		OpenFile *file = scan->data;

		if (file->monitor != NULL)
			g_file_monitor_cancel (file->monitor);
		open_file_free (file);
	}
	g_list_free (private->open_files);
	private->open_files = NULL;
}


static void
fr_window_free_private_data (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->activity_timeout_handle != 0) {
		g_source_remove (private->activity_timeout_handle);
		private->activity_timeout_handle = 0;
	}

	if (private->progress_timeout != 0) {
		g_source_remove (private->progress_timeout);
		private->progress_timeout = 0;
	}

	if (private->hide_progress_timeout != 0) {
		g_source_remove (private->hide_progress_timeout);
		private->hide_progress_timeout = 0;
	}

	fr_window_history_clear (window);

	_g_object_unref (private->open_default_dir);
	_g_object_unref (private->add_default_dir);
	_g_object_unref (private->extract_default_dir);
	_g_object_unref (private->archive_file);
	_g_object_unref (private->last_extraction_destination);

	_g_object_list_unref (private->last_extraction_files_first_level);
	private->last_extraction_files_first_level = NULL;

	g_free (private->password);
	g_free (private->second_password);
	g_free (private->custom_action_message);

	g_object_unref (private->list_store);

	if (private->clipboard_data != NULL) {
		fr_clipboard_data_unref (private->clipboard_data);
		private->clipboard_data = NULL;
	}
	if (app_clipboard != NULL) {
		fr_clipboard_data_unref (app_clipboard);
		app_clipboard = NULL;
	}
	if (private->copy_from_archive != NULL) {
		g_object_unref (private->copy_from_archive);
		private->copy_from_archive = NULL;
	}

	_g_object_unref (private->saving_file);

	fr_window_free_open_files (window);

	g_clear_error (&private->drag_error);
	_g_string_list_free (private->drag_file_list);
	private->drag_file_list = NULL;

	g_free (private->last_location);
	g_free (private->location_before_filter);

	fr_window_free_batch_data (window);
	g_free (private->batch_title);

	_g_object_unref (private->pd_last_archive);
	g_free (private->pd_last_message);

	g_settings_set_enum (private->settings_listing, PREF_LISTING_LIST_MODE, private->last_list_mode);

	_g_object_unref (private->settings_listing);
	_g_object_unref (private->settings_ui);
	_g_object_unref (private->settings_general);
	_g_object_unref (private->settings_dialogs);
	_g_object_unref (private->settings_nautilus);

	_g_object_unref (private->cancellable);
	g_hash_table_unref (private->named_dialogs);
	g_object_unref (private->window_group);
}


void
fr_window_close (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->activity_ref > 0)
		return;

	if (private->closing)
		return;

	private->closing = TRUE;

	if (gtk_widget_get_realized (GTK_WIDGET (window))) {
		int width, height;

		gtk_window_get_default_size (GTK_WINDOW (window), &width, &height);
		if ((width > 0) && (height > 0)) {
			g_settings_set_int (private->settings_ui, PREF_UI_WINDOW_WIDTH, width);
			g_settings_set_int (private->settings_ui, PREF_UI_WINDOW_HEIGHT, height);
		}

		width = gtk_paned_get_position (GTK_PANED (private->paned));
		if (width > 0)
			g_settings_set_int (private->settings_ui, PREF_UI_SIDEBAR_WIDTH, width);

		width = gtk_tree_view_column_get_width (private->filename_column);
		if (width > 0)
			g_settings_set_int (private->settings_listing, PREF_LISTING_NAME_COLUMN_WIDTH, width);
	}

	gtk_window_destroy (GTK_WINDOW (window));
}


#define DIALOG_NAME_KEY "fr_dialog_name"


static void
unset_dialog (GtkWidget *object,
	      gpointer   user_data)
{
	FrWindow   *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	const char *dialog_name;

	dialog_name = g_object_get_data (G_OBJECT (object), DIALOG_NAME_KEY);
	if (dialog_name != NULL)
		g_hash_table_remove (private->named_dialogs, dialog_name);
}


void
fr_window_set_dialog (FrWindow   *window,
		      const char *dialog_name,
		      GtkWidget  *dialog)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_object_set_data (G_OBJECT (dialog), DIALOG_NAME_KEY, (gpointer) _g_str_get_static (dialog_name));
	g_hash_table_insert (private->named_dialogs, (gpointer) dialog_name, dialog);
	g_signal_connect (GTK_DIALOG (dialog),
			  "destroy",
			  G_CALLBACK (unset_dialog),
			  window);
}


gboolean
fr_window_present_dialog_if_created (FrWindow   *window,
				     const char *dialog_name)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWidget *dialog;

	dialog = g_hash_table_lookup (private->named_dialogs, dialog_name);
	if (dialog != NULL) {
		gtk_window_present (GTK_WINDOW (dialog));
		return TRUE;
	}

	return FALSE;
}


static void
fr_window_finalize (GObject *object)
{
	FrWindow *window = FR_WINDOW (object);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	fr_window_free_open_files (window);

	if (window->archive != NULL) {
		g_object_unref (window->archive);
		window->archive = NULL;
	}

	if (private != NULL) {
		fr_window_free_private_data (window);
	}

	G_OBJECT_CLASS (fr_window_parent_class)->finalize (object);
}



static void fr_window_update_file_list (FrWindow *window,
					gboolean  update_view);
static void fr_window_update_dir_tree  (FrWindow *window);


static void
fr_window_enable_action (FrWindow   *window,
			 const char *action_name,
			 gboolean    enabled)
{
	GAction *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);
	if (action != NULL)
		g_object_set (action, "enabled", enabled, NULL);
}


static void
fr_window_set_action_state (FrWindow   *window,
			    const char *action_name,
			    gboolean    active)
{
	GAction *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);
	if (action != NULL)
		g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (active));
}


static void
fr_window_change_action_state (FrWindow   *window,
			       const char *action_name,
			       gboolean    value)
{
	GAction  *action;
	GVariant *old_state;
	GVariant *new_state;

	action = g_action_map_lookup_action (G_ACTION_MAP (window), action_name);
	g_return_if_fail (action != NULL);

	old_state = g_action_get_state (action);
	new_state = g_variant_new_boolean (value);
	if ((old_state == NULL) || ! g_variant_equal (old_state, new_state))
		g_action_change_state (action, new_state);
	else
		g_variant_unref (new_state);

	if (old_state != NULL)
		g_variant_unref (old_state);
}


static void
fr_window_update_paste_command_sensitivity (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean running;
	gboolean no_archive;
	gboolean ro;

	if (private->closing)
		return;

	running    = private->activity_ref > 0;
	no_archive = (window->archive == NULL) || ! private->archive_present;
	ro         = ! no_archive && window->archive->read_only;

	fr_window_enable_action (window, "edit-paste",
				 ! no_archive
				 && ! ro
				 && ! running
				 && fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_STORE_MANY_FILES)
				 && (private->list_mode != FR_WINDOW_LIST_MODE_FLAT)
				 && (app_clipboard != NULL));
}


static void
fr_window_realize (GtkWidget *widget)
{
	FrWindow        *window = FR_WINDOW (widget);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	GTK_WIDGET_CLASS (fr_window_parent_class)->realize (widget);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (private->list_store),
					      g_settings_get_enum (private->settings_listing, PREF_LISTING_SORT_METHOD),
					      g_settings_get_enum (private->settings_listing, PREF_LISTING_SORT_TYPE));

	fr_window_update_dir_tree (window);
	fr_window_update_file_list (window, TRUE);
}


static void
fr_window_unmap (GtkWidget *widget)
{
	FrWindow    *window = FR_WINDOW (widget);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkSortType  order;
	int          column_id;

	if (gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (private->list_store),
						  &column_id,
						  &order))
	{
		g_settings_set_enum (private->settings_listing, PREF_LISTING_SORT_METHOD, column_id);
		g_settings_set_enum (private->settings_listing, PREF_LISTING_SORT_TYPE, order);
	}

	GTK_WIDGET_CLASS (fr_window_parent_class)->unmap (widget);
}


static void
fr_window_class_init (FrWindowClass *klass)
{
	GObjectClass   *gobject_class;
	GtkWidgetClass *widget_class;

	fr_window_parent_class = g_type_class_peek_parent (klass);

	GType archive_loaded_types[] = { G_TYPE_BOOLEAN };
	GType progress_types[] = { G_TYPE_DOUBLE, G_TYPE_STRING };
	GType ready_types[] = { G_TYPE_POINTER };
	fr_window_signals[ARCHIVE_LOADED] =
		g_signal_newv ("archive-loaded",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      /* class_closure = */ NULL,
			      NULL, NULL,
			      fr_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1,
			      archive_loaded_types);
	fr_window_signals[PROGRESS] =
		g_signal_newv ("progress",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      /* class_closure = */ NULL,
			      NULL, NULL,
			      fr_marshal_VOID__DOUBLE_STRING,
			      G_TYPE_NONE, 2,
			      progress_types);
	fr_window_signals[READY] =
		g_signal_newv ("ready",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      /* class_closure = */ NULL,
			      NULL, NULL,
			      fr_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      ready_types);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_window_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->realize = fr_window_realize;
	widget_class->unmap = fr_window_unmap;
}


static void
fr_window_init (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->update_dropped_files = FALSE;
	private->dnd_extract_is_running = FALSE;
	private->dnd_extract_finished_with_error = FALSE;
	private->filter_mode = FALSE;
	private->use_progress_dialog = TRUE;
	private->batch_title = NULL;
	private->cancellable = g_cancellable_new ();
	private->compression = FR_COMPRESSION_NORMAL;
	private->window_group = gtk_window_group_new ();
	private->populating_file_list = FALSE;
	private->named_dialogs = g_hash_table_new (g_str_hash, g_str_equal);

	gtk_window_group_add_window (private->window_group, GTK_WINDOW (window));

	window->archive = NULL;
}


/* -- window history -- */


#if 0
static void
fr_window_history_print (FrWindow *window)
{
	GList *list;

	debug (DEBUG_INFO, "history:\n");
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	for (list = private->history; list; list = list->next)
		g_print ("\t%s %s\n",
			 (char*) list->data,
			 (list == private->history_current)? "<-": "");
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_print ("\n");
}
#endif


static gboolean
fr_window_dir_exists_in_archive (FrWindow   *window,
				 const char *dir_name)
{
	size_t dir_name_len;

	if (dir_name == NULL)
		return FALSE;

	dir_name_len = strlen (dir_name);
	if (dir_name_len == 0)
		return TRUE;

	if (strcmp (dir_name, "/") == 0)
		return TRUE;

	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (window->archive->files, i);

		if (strncmp (dir_name, fdata->full_path, dir_name_len) == 0) {
			return TRUE;
		}
		else if (fdata->dir
			 && (fdata->full_path[strlen (fdata->full_path) - 1] != '/')
			 && (strncmp (dir_name, fdata->full_path, dir_name_len - 1) == 0))
		{
			return TRUE;
		}
	}

	return FALSE;
}


static void
fr_window_update_history (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *scan;

	/* remove the paths not present in the archive */

	for (scan = private->history; scan; /* void */) {
		GList *next = scan->next;
		char  *path = scan->data;

		if (! fr_window_dir_exists_in_archive (window, path)) {
			if (scan == private->history_current)
				private->history_current = NULL;
			private->history = g_list_remove_link (private->history, scan);
			_g_string_list_free (scan);
		}

		scan = next;
	}

	if (private->history_current == NULL)
		private->history_current = private->history;
}


static void
fr_window_history_add (FrWindow   *window,
		       const char *path)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if ((private->history_current == NULL) || (g_strcmp0 (path, private->history_current->data) != 0)) {
		GList *scan;
		GList *new_current = NULL;

		/* search the path in the history */
		for (scan = private->history_current; scan; scan = scan->next) {
			char *path_in_history = scan->data;

			if (g_strcmp0 (path, path_in_history) == 0) {
				new_current = scan;
				break;
			}
		}

		if (new_current != NULL) {
			private->history_current = new_current;
		}
		else {
			/* remove all the paths after the current position */
			for (scan = private->history; scan && (scan != private->history_current); /* void */) {
				GList *next = scan->next;

				private->history = g_list_remove_link (private->history, scan);
				_g_string_list_free (scan);

				scan = next;
			}

			private->history = g_list_prepend (private->history, g_strdup (path));
			private->history_current = private->history;
		}
	}
}


static void
fr_window_history_pop (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *first;

	if (private->history == NULL)
		return;

	first = private->history;
	private->history = g_list_remove_link (private->history, first);
	if (private->history_current == first)
		private->history_current = private->history;
	g_free (first->data);
	g_list_free (first);
}


/* -- activity mode -- */



static void
check_whether_has_a_dir (GtkTreeModel *model,
			 GtkTreePath  *path,
			 GtkTreeIter  *iter,
			 gpointer      data)
{
	gboolean *has_a_dir = data;
	FrFileData *fdata;

	gtk_tree_model_get (model, iter,
			    COLUMN_FILE_DATA, &fdata,
			    -1);
	if (fr_file_data_is_dir (fdata))
		*has_a_dir = TRUE;
}


static gboolean
selection_has_a_dir (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeSelection *selection;
	gboolean          has_a_dir = FALSE;

	if (! gtk_widget_get_realized (private->list_view))
		return FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
	if (selection == NULL)
		return FALSE;

	gtk_tree_selection_selected_foreach (selection,
					     check_whether_has_a_dir,
					     &has_a_dir);

	return has_a_dir;
}


static void
fr_window_update_sensitivity (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean no_archive;
	gboolean ro;
	gboolean file_op;
	gboolean running;
	gboolean can_store_many_files;
	gboolean sel_not_null;
	gboolean one_file_selected;
	gboolean dir_selected;
	int      n_selected;

	if (private->batch_mode)
		return;

	running              = private->activity_ref > 0;
	no_archive           = (window->archive == NULL) || ! private->archive_present;
	ro                   = ! no_archive && window->archive->read_only;
	file_op              = ! no_archive && ! private->archive_new  && ! running;
	can_store_many_files = (window->archive != NULL) && fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_STORE_MANY_FILES);
	n_selected           = fr_window_get_n_selected_files (window);
	sel_not_null         = n_selected > 0;
	one_file_selected    = n_selected == 1;
	dir_selected         = selection_has_a_dir (window);

	if (private->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (private->progress_dialog),
						   GTK_RESPONSE_OK,
						   running && private->stoppable);

	fr_window_update_paste_command_sensitivity (window);

	fr_window_enable_action (window, "add-files", ! no_archive && ! ro && ! running && can_store_many_files);
	fr_window_enable_action (window, "close", ! running || private->stoppable);
	fr_window_enable_action (window, "delete", ! no_archive && ! ro && ! private->archive_new && ! running && can_store_many_files);
	fr_window_enable_action (window, "edit-copy", ! no_archive && ! ro && ! running && can_store_many_files && sel_not_null && (private->list_mode != FR_WINDOW_LIST_MODE_FLAT));
	fr_window_enable_action (window, "edit-cut", ! no_archive && ! ro && ! running && can_store_many_files && sel_not_null && (private->list_mode != FR_WINDOW_LIST_MODE_FLAT));
	fr_window_enable_action (window, "edit-password", ! running && (! no_archive && window->archive->propPassword));
	fr_window_enable_action (window, "extract-files", file_op);
	fr_window_enable_action (window, "extract-all-by-default", file_op);
	fr_window_enable_action (window, "find", ! no_archive);
	fr_window_enable_action (window, "navigate-to", ! no_archive && private->filter_mode && one_file_selected);
	fr_window_enable_action (window, "open-folder", file_op && one_file_selected && dir_selected);
	fr_window_enable_action (window, "open-with", file_op && sel_not_null && ! dir_selected);
	fr_window_enable_action (window, "rename", ! no_archive && ! ro && ! running && can_store_many_files && one_file_selected);
	fr_window_enable_action (window, "save-as", ! no_archive && can_store_many_files && ! running);
	fr_window_enable_action (window, "test-archive", ! no_archive && ! running && window->archive->propTest);
	fr_window_enable_action (window, "view-properties", file_op);
	fr_window_enable_action (window, "view-selection", file_op && one_file_selected && ! dir_selected);
}


static int
activity_cb (gpointer data)
{
	FrWindow *window = data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if ((private->pd_progress_bar != NULL) && private->progress_pulse)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (private->pd_progress_bar));

	return TRUE;
}


static void
_fr_window_start_activity_mode (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->activity_ref++ > 0)
		return;

	private->activity_timeout_handle = g_timeout_add (ACTIVITY_DELAY,
							       activity_cb,
							       window);
	fr_window_update_sensitivity (window);
}


static void
fr_window_pop_message (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (! gtk_widget_get_mapped (GTK_WIDGET (window)))
		return;

	if (private->progress_dialog != NULL)
		gtk_label_set_text (GTK_LABEL (private->pd_message), _("Operation completed"));
}


static GPtrArray *
fr_window_get_current_dir_list (FrWindow *window)
{
	GPtrArray *files;

	files = g_ptr_array_sized_new (128);

	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (window->archive->files, i);

		if (fdata->list_name == NULL)
			continue;
		g_ptr_array_add (files, fdata);
	}

	return files;
}


static void
_fr_window_stop_activity_mode (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->activity_ref == 0)
		return;

	fr_window_pop_message (window);

	if (--private->activity_ref > 0)
		return;

	if (private->activity_timeout_handle != 0) {
		g_source_remove (private->activity_timeout_handle);
		private->activity_timeout_handle = 0;
	}

	if (private->progress_dialog != NULL)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (private->pd_progress_bar), 0.0);

	fr_window_update_sensitivity (window);
}


/* -- window_update_file_list -- */


static guint64
get_dir_size (FrWindow   *window,
	      const char *current_dir,
	      const char *name)
{
	guint64  size;
	char    *dirname;
	size_t   dirname_l;

	dirname = g_strconcat (current_dir, name, "/", NULL);
	dirname_l = strlen (dirname);

	size = 0;
	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fd = g_ptr_array_index (window->archive->files, i);

		if (strncmp (dirname, fd->full_path, dirname_l) == 0)
			size += fd->size;
	}

	g_free (dirname);

	return size;
}


static gboolean
file_data_respects_filter (FrWindow *window,
			   GRegex   *filter,
			   FrFileData *fdata)
{
	if ((fdata == NULL) || (filter == NULL))
		return TRUE;

	if (fdata->dir || (fdata->name == NULL))
		return FALSE;

	return g_regex_match (filter, fdata->name, 0, NULL);
}


static gboolean
compute_file_list_name (FrWindow   *window,
			GRegex     *filter,
			FrFileData *fdata,
			const char *current_dir,
			size_t      current_dir_len,
			GHashTable *names_hash,
			gboolean   *different_name)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	register char *scan, *end;

	*different_name = FALSE;

	if (! file_data_respects_filter (window, filter, fdata))
		return FALSE;

	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fr_file_data_set_list_name (fdata, fdata->name);
		if (fdata->dir)
			fdata->dir_size = 0;
		return FALSE;
	}

	if (strncmp (fdata->full_path, current_dir, current_dir_len) != 0) {
		*different_name = TRUE;
		return FALSE;
	}

	if (strlen (fdata->full_path) == current_dir_len)
		return FALSE;

	scan = fdata->full_path + current_dir_len;
	end = strchr (scan, '/');
	if ((end == NULL) && ! fdata->dir) { /* file */
		fr_file_data_set_list_name (fdata, scan);
	}
	else { /* folder */
		char *dir_name;

		if (end != NULL)
			dir_name = g_strndup (scan, end - scan);
		else
			dir_name = g_strdup (scan);

		/* avoid to insert duplicated folders */
		if (g_hash_table_lookup (names_hash, dir_name) != NULL) {
			g_free (dir_name);
			return FALSE;
		}
		g_hash_table_insert (names_hash, dir_name, GINT_TO_POINTER (1));

		if ((end != NULL) && (*(end + 1) != '\0'))
			fdata->list_dir = TRUE;
		fr_file_data_set_list_name (fdata, dir_name);
		fdata->dir_size = get_dir_size (window, current_dir, dir_name);
	}

	return TRUE;
}


static GRegex *
_fr_window_create_filter (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GRegex     *filter;
	const char *filter_str;

	filter = NULL;
	filter_str = gtk_editable_get_text (GTK_EDITABLE (private->filter_entry));
	if ((filter_str != NULL) && (*filter_str != '\0')) {
		char *escaped;
		char *pattern;

		escaped = g_regex_escape_string (filter_str, -1);
		pattern = g_strdup_printf (".*%s.*", escaped);
		filter = g_regex_new (pattern, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, G_REGEX_MATCH_NOTEMPTY, NULL);

		g_free (pattern);
		g_free (escaped);
	}

	return filter;
}


static void
fr_window_compute_list_names (FrWindow  *window,
			      GPtrArray *files)
{
	const char *current_dir;
	size_t      current_dir_len;
	GHashTable *names_hash;
	GRegex     *filter;
	gboolean    visible_list_started = FALSE;
	gboolean    visible_list_completed = FALSE;
	gboolean    different_name;

	current_dir = fr_window_get_current_location (window);
	current_dir_len = strlen (current_dir);
	names_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	filter = _fr_window_create_filter (window);
	for (guint i = 0; i < files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (files, i);

		fr_file_data_set_list_name (fdata, NULL);
		fdata->list_dir = FALSE;

		/* the files array is sorted by path, when the visible list
		 * is started and we find a path that doesn't match the
		 * current_dir path, the following files can't match
		 * the current_dir path. */

		if (visible_list_completed)
			continue;

		if (compute_file_list_name (window, filter, fdata, current_dir, current_dir_len, names_hash, &different_name)) {
			visible_list_started = TRUE;
		}
		else if (visible_list_started && different_name)
			visible_list_completed = TRUE;
	}

	if (filter != NULL)
		g_regex_unref (filter);
	g_hash_table_destroy (names_hash);
}


static char *
get_parent_dir (const char *current_dir)
{
	char *dir;
	char *new_dir;
	char *retval;

	if (current_dir == NULL)
		return NULL;
	if (strcmp (current_dir, "/") == 0)
		return g_strdup ("/");

	dir = g_strdup (current_dir);
	dir[strlen (dir) - 1] = 0;
	new_dir = _g_path_remove_level (dir);
	g_free (dir);

	if (new_dir[strlen (new_dir) - 1] == '/')
		retval = new_dir;
	else {
		retval = g_strconcat (new_dir, "/", NULL);
		g_free (new_dir);
	}

	return retval;
}


static GIcon *
fr_file_data_get_icon (FrFileData *fdata)
{
	GIcon *icon = NULL;

	if (fdata->link != NULL) {
		icon = g_themed_icon_new ("emblem-symbolic-link");
	}
	else {
		const char *content_type;

		if (fr_file_data_is_dir (fdata))
			content_type = MIME_TYPE_DIRECTORY;
		else
			content_type = fdata->content_type;
		icon = g_content_type_get_icon (content_type);
	}

	return icon;
}


static GIcon *
fr_file_data_get_emblem (FrFileData *fdata)
{
	return fdata->encrypted ? g_themed_icon_new ("emblem-readonly") : NULL;
}


static void
add_selected_from_list_view (GtkTreeModel *model,
			     GtkTreePath  *path,
			     GtkTreeIter  *iter,
			     gpointer      data)
{
	GList    **list = data;
	FrFileData *fdata;

	gtk_tree_model_get (model, iter,
			    COLUMN_FILE_DATA, &fdata,
			    -1);
	*list = g_list_prepend (*list, fdata);
}


static void
add_selected_from_tree_view (GtkTreeModel *model,
			     GtkTreePath  *path,
			     GtkTreeIter  *iter,
			     gpointer      data)
{
	GList **list = data;
	char   *dir_path;

	gtk_tree_model_get (model, iter,
			    TREE_COLUMN_PATH, &dir_path,
			    -1);
	*list = g_list_prepend (*list, dir_path);
}


static void
fr_window_populate_file_list (FrWindow  *window,
			      GPtrArray *files)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	int         sort_column_id;
	GtkSortType order;

	if (! gtk_widget_get_realized (GTK_WIDGET (window))) {
		_fr_window_stop_activity_mode (window);
		return;
	}

	private->populating_file_list = TRUE;
	gtk_list_store_clear (private->list_store);

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (private->list_store),
					      &sort_column_id,
					      &order);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (private->list_store),
					      GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
					      0);

	for (guint i = 0; i < files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (files, i);
		GtkTreeIter  iter;
		GIcon       *icon, *emblem;
		char        *utf8_name;

		if (fdata->list_name == NULL)
			continue;

		gtk_list_store_append (private->list_store, &iter);

		icon = fr_file_data_get_icon (fdata);
		emblem = fr_file_data_get_emblem (fdata);
		utf8_name = g_filename_display_name (fdata->list_name);

		if (fr_file_data_is_dir (fdata)) {
			char *utf8_path;
			char *tmp;
			char *s_size;
			char *s_time;

			if (fdata->list_dir)
				tmp = _g_path_remove_ending_separator (fr_window_get_current_location (window));
			else
				tmp = _g_path_remove_level (fdata->path);
			utf8_path = g_filename_display_name (tmp);
			g_free (tmp);

			s_size = g_format_size (fdata->dir_size);

			if (fdata->list_dir) {
				s_time = g_strdup ("");
			} else {
				g_autoptr (GDateTime) date_time;
				date_time = g_date_time_new_from_unix_local (fdata->modified);
				s_time = g_date_time_format (date_time, _("%d %B %Y, %H:%M"));
			}

			gtk_list_store_set (private->list_store, &iter,
					    COLUMN_FILE_DATA, fdata,
					    COLUMN_ICON, icon,
					    COLUMN_NAME, utf8_name,
					    COLUMN_EMBLEM, emblem,
					    COLUMN_TYPE, _("Folder"),
					    COLUMN_SIZE, s_size,
					    COLUMN_TIME, s_time,
					    COLUMN_PATH, utf8_path,
					    -1);
			g_free (utf8_path);
			g_free (s_size);
			g_free (s_time);
		}
		else {
			g_autoptr (GDateTime) date_time;
			char *utf8_path;
			char *s_size;
			char *s_time;
			char *desc;

			utf8_path = g_filename_display_name (fdata->path);

			s_size = g_format_size (fdata->size);
			date_time = g_date_time_new_from_unix_local (fdata->modified);
			s_time = g_date_time_format (date_time, _("%d %B %Y, %H:%M"));
			desc = g_content_type_get_description (fdata->content_type);

			gtk_list_store_set (private->list_store, &iter,
					    COLUMN_FILE_DATA, fdata,
					    COLUMN_ICON, icon,
					    COLUMN_NAME, utf8_name,
					    COLUMN_EMBLEM, emblem,
					    COLUMN_TYPE, desc,
					    COLUMN_SIZE, s_size,
					    COLUMN_TIME, s_time,
					    COLUMN_PATH, utf8_path,
					    -1);
			g_free (utf8_path);
			g_free (s_size);
			g_free (s_time);
			g_free (desc);
		}

		g_free (utf8_name);
		_g_object_unref (icon);
		_g_object_unref (emblem);
	}

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (private->list_store),
					      sort_column_id,
					      order);

	private->populating_file_list = FALSE;

	_fr_window_stop_activity_mode (window);
}


static int
path_compare (gconstpointer a,
	      gconstpointer b)
{
	char *path_a = *((char**) a);
	char *path_b = *((char**) b);

	return strcmp (path_a, path_b);
}


static gboolean
get_tree_iter_from_path (FrWindow    *window,
			 const char  *path,
			 GtkTreeIter *parent,
			 GtkTreeIter *iter)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean    result = FALSE;

	if (! gtk_tree_model_iter_children (GTK_TREE_MODEL (private->tree_store), iter, parent))
		return FALSE;

	do {
		GtkTreeIter  tmp;
		char        *iter_path;

		if (get_tree_iter_from_path (window, path, iter, &tmp)) {
			*iter = tmp;
			return TRUE;
		}

		gtk_tree_model_get (GTK_TREE_MODEL (private->tree_store),
				    iter,
				    TREE_COLUMN_PATH, &iter_path,
				    -1);

		if ((iter_path != NULL) && (strcmp (path, iter_path) == 0)) {
			result = TRUE;
			g_free (iter_path);
			break;
		}
		g_free (iter_path);
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (private->tree_store), iter));

	return result;
}


static void
fr_window_deactivate_filter (FrWindow *window)
{
	fr_window_change_action_state (window, "find", FALSE);
}


static void
fr_window_update_current_location (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	const char *current_dir = fr_window_get_current_location (window);
	char       *path;
	GtkTreeIter iter;

	gtk_widget_set_visible (private->location_bar, private->archive_present && (private->list_mode == FR_WINDOW_LIST_MODE_AS_DIR));

	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT)
		return;

	gtk_editable_set_text (GTK_EDITABLE (private->location_entry), private->archive_present? current_dir: "");

	fr_window_enable_action (window, "go-back", private->archive_present && (current_dir != NULL) && (private->history_current != NULL) && (private->history_current->next != NULL));
	fr_window_enable_action (window, "go-forward", private->archive_present && (current_dir != NULL) && (private->history_current != NULL) && (private->history_current->prev != NULL));
	fr_window_enable_action (window, "go-home", private->archive_present);
	fr_window_enable_action (window, "go-up-one-level", private->archive_present && (current_dir != NULL) && (g_strcmp0 (current_dir, "/") != 0));

	gtk_widget_set_sensitive (private->location_entry, private->archive_present);
	gtk_widget_set_sensitive (private->location_label, private->archive_present);
	gtk_widget_set_sensitive (private->filter_entry, private->archive_present);

	fr_window_deactivate_filter (window);

#if 0
	fr_window_history_print (window);
#endif

	path = _g_path_remove_ending_separator (current_dir);
	if (get_tree_iter_from_path (window, path, NULL, &iter)) {
		GtkTreeSelection *selection;
		GtkTreePath      *t_path;

		t_path = gtk_tree_model_get_path (GTK_TREE_MODEL (private->tree_store), &iter);
		gtk_tree_view_expand_to_path (GTK_TREE_VIEW (private->tree_view), t_path);
		gtk_tree_path_free (t_path);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->tree_view));
		gtk_tree_selection_select_iter (selection, &iter);
	}
	g_free (path);
}


static void
fr_window_update_dir_tree (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GPtrArray  *dirs;
	GRegex     *filter;
	GHashTable *dir_cache;
	GIcon      *icon;

	if (! gtk_widget_get_realized (GTK_WIDGET (window)))
		return;

	if (private->filter_mode
	    || ! private->view_sidebar
	    || ! private->archive_present
	    || (private->list_mode == FR_WINDOW_LIST_MODE_FLAT))
	{
		gtk_widget_set_sensitive (private->tree_view, FALSE);
		gtk_widget_hide (private->sidepane);
		return;
	}
	else {
		gtk_tree_store_clear (private->tree_store);
		gtk_widget_set_sensitive (private->tree_view, TRUE);
		if (! gtk_widget_get_visible (private->sidepane))
			gtk_widget_show (private->sidepane);
	}

	if (gtk_widget_get_realized (private->tree_view))
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (private->tree_view), 0, 0);

	/**/

	dirs = g_ptr_array_sized_new (128);

	filter = _fr_window_create_filter (window);
	dir_cache = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (window->archive->files, i);
		char     *dir;

		if (! file_data_respects_filter (window, filter, fdata))
			continue;

		if (fdata->dir)
			dir = _g_path_remove_ending_separator (fdata->full_path);
		else
			dir = _g_path_remove_level (fdata->full_path);

		while ((dir != NULL) && (strcmp (dir, "/") != 0)) {
			char *new_dir;

			if (g_hash_table_lookup (dir_cache, dir) != NULL)
				break;

			new_dir = dir;
			g_ptr_array_add (dirs, new_dir);
			g_hash_table_replace (dir_cache, new_dir, "1");

			dir = _g_path_remove_level (new_dir);
		}

		g_free (dir);
	}
	g_hash_table_destroy (dir_cache);
	if (filter != NULL)
		g_regex_unref (filter);

	g_ptr_array_sort (dirs, path_compare);
	dir_cache = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) gtk_tree_path_free);

	/**/

	icon = g_content_type_get_symbolic_icon (MIME_TYPE_ARCHIVE);
	{
		GtkTreeIter  node;
		char        *name;

		name = _g_file_get_display_name (fr_archive_get_file (window->archive));

		gtk_tree_store_append (private->tree_store, &node, NULL);
		gtk_tree_store_set (private->tree_store, &node,
				    TREE_COLUMN_ICON, icon,
				    TREE_COLUMN_NAME, name,
				    TREE_COLUMN_PATH, "/",
				    TREE_COLUMN_WEIGHT, PANGO_WEIGHT_BOLD,
				    -1);
		g_hash_table_replace (dir_cache, "/", gtk_tree_model_get_path (GTK_TREE_MODEL (private->tree_store), &node));

		g_free (name);
	}
	g_object_unref (icon);

	/**/

	icon = g_content_type_get_symbolic_icon (MIME_TYPE_DIRECTORY);
	for (guint i = 0; i < dirs->len; i++) {
		char        *dir = g_ptr_array_index (dirs, i);
		char        *parent_dir;
		GtkTreePath *parent_path;
		GtkTreeIter  parent;
		GtkTreeIter  node;

		parent_dir = _g_path_remove_level (dir);
		if (parent_dir == NULL)
			continue;

		parent_path = g_hash_table_lookup (dir_cache, parent_dir);
		gtk_tree_model_get_iter (GTK_TREE_MODEL (private->tree_store),
					 &parent,
					 parent_path);
		gtk_tree_store_append (private->tree_store, &node, &parent);
		gtk_tree_store_set (private->tree_store, &node,
				    TREE_COLUMN_ICON, icon,
				    TREE_COLUMN_NAME, _g_path_get_basename (dir),
				    TREE_COLUMN_PATH, dir,
				    TREE_COLUMN_WEIGHT, PANGO_WEIGHT_NORMAL,
				    -1);
		g_hash_table_replace (dir_cache, dir, gtk_tree_model_get_path (GTK_TREE_MODEL (private->tree_store), &node));

		g_free (parent_dir);
	}
	g_hash_table_destroy (dir_cache);
	if (icon != NULL)
		g_object_unref (icon);

	g_ptr_array_free (dirs, TRUE);

	fr_window_update_current_location (window);
}


static void
fr_window_update_file_list (FrWindow *window,
			    gboolean  update_view)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GPtrArray  *files;
	gboolean    free_files = FALSE;

	if (! gtk_widget_get_realized (GTK_WIDGET (window)))
		return;

	if (gtk_widget_get_realized (private->list_view))
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (private->list_view), 0, 0);

	if (! private->archive_present || private->archive_new) {
		if (update_view)
			gtk_list_store_clear (private->list_store);

		private->current_view_length = 0;

		if (private->archive_new) {
			gtk_widget_set_sensitive (private->list_view, TRUE);
			gtk_widget_show (gtk_widget_get_parent (private->list_view));
		}
		else {
			gtk_widget_set_sensitive (private->list_view, FALSE);
			gtk_widget_hide (gtk_widget_get_parent (private->list_view));
		}

		return;
	}
	else {
		gtk_widget_set_sensitive (private->list_view, TRUE);
		gtk_widget_show (gtk_widget_get_parent (private->list_view));
	}

	if (private->give_focus_to_the_list) {
		gtk_widget_grab_focus (private->list_view);
		private->give_focus_to_the_list = FALSE;
	}

	/**/

	_fr_window_start_activity_mode (window);

	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fr_window_compute_list_names (window, window->archive->files);
		files = window->archive->files;
		free_files = FALSE;
	}
	else {
		char *current_dir = g_strdup (fr_window_get_current_location (window));

		while (! fr_window_dir_exists_in_archive (window, current_dir)) {
			char *tmp;

			fr_window_history_pop (window);

			tmp = get_parent_dir (current_dir);
			g_free (current_dir);
			current_dir = tmp;

			fr_window_history_add (window, current_dir);
		}
		g_free (current_dir);

		fr_window_compute_list_names (window, window->archive->files);
		files = fr_window_get_current_dir_list (window);
		free_files = TRUE;
	}

	if (files != NULL)
		private->current_view_length = files->len;
	else
		private->current_view_length = 0;

	if (update_view)
		fr_window_populate_file_list (window, files);

	if (free_files)
		g_ptr_array_free (files, TRUE);
}


static void
fr_window_update_title (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char *title;
	char *name;

	if (! private->archive_present) {
		gtk_window_set_title (GTK_WINDOW (window), _("File Roller"));
		return;
	}

	name = _g_file_get_display_name (fr_window_get_archive_file (window));
	title = g_strdup_printf ("%s %s",
				 name,
				 window->archive->read_only ? _("[read only]") : "");
	gtk_window_set_title (GTK_WINDOW (window), title);

	g_free (title);
	g_free (name);
}


static gboolean
location_entry_activate_cb (GtkWidget   *widget,
			    FrWindow    *window)
{
	fr_window_go_to_location (window, gtk_editable_get_text (GTK_EDITABLE (widget)), FALSE);
	return FALSE;
}


static void
_fr_window_close_after_notification (FrWindow *window)
{
	fr_window_set_current_action (window, FR_BATCH_ACTION_QUIT, NULL, NULL);
	fr_window_restart_current_action (window);
}


static gboolean
real_close_progress_dialog (gpointer data)
{
	FrWindow *window = data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->hide_progress_timeout != 0) {
		g_source_remove (private->hide_progress_timeout);
		private->hide_progress_timeout = 0;
	}

	if (private->progress_dialog != NULL)
		gtk_widget_hide (private->progress_dialog);

	return FALSE;
}


static void
close_progress_dialog (FrWindow *window,
		       gboolean  close_now)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->progress_timeout != 0) {
		g_source_remove (private->progress_timeout);
		private->progress_timeout = 0;
	}

	if (private->progress_dialog == NULL)
		return;

	if (close_now) {
		if (private->hide_progress_timeout != 0) {
			g_source_remove (private->hide_progress_timeout);
			private->hide_progress_timeout = 0;
		}
		real_close_progress_dialog (window);
	}
	else {
		if (private->hide_progress_timeout != 0)
			return;
		private->hide_progress_timeout = g_timeout_add (HIDE_PROGRESS_DELAY,
								     real_close_progress_dialog,
								     window);
	}
}


/* -- open_folder -- */


static void
open_folder_async (FrWindow   *window,
		   GFile      *folder,
		   GList      *files,
		   GFunc       callback,
		   gpointer    user_data)
{
	if (folder == NULL) {
		callback (window, user_data);
		return;
	}

	/* Only use ShowItems if its a single file to avoid Nautilus to open
	 * multiple windows */

	if ((files == NULL) || (files->next != NULL)) {
		_gtk_show_folder (GTK_WINDOW (window), folder, callback, user_data);
	}
	else {
		_gtk_show_file_in_container (GTK_WINDOW (window), (GFile *) files->data, callback, user_data);
	}
}


static void
fr_window_view_extraction_destination_folder (FrWindow *window, GFunc callback, gpointer user_data)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	open_folder_async (
		window,
		private->last_extraction_destination,
		private->last_extraction_files_first_level,
		callback,
		user_data
	);
}


static void
progress_dialog_response__open_folder_cb (FrWindow  *window) {
	close_progress_dialog (window, TRUE);
}


static void
progress_dialog_response (GtkDialog *dialog,
			  int        response_id,
			  FrWindow  *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWidget *new_window;
	GFile     *saved_file;

	saved_file = private->saving_file;
	private->saving_file = NULL;

	switch (response_id) {
	case GTK_RESPONSE_CANCEL:
		if (private->stoppable) {
			fr_window_stop (window);
			close_progress_dialog (window, TRUE);
		}
		break;
	case GTK_RESPONSE_CLOSE:
		close_progress_dialog (window, TRUE);
		break;
	case DIALOG_RESPONSE_OPEN_ARCHIVE:
		new_window = fr_window_new ();
		gtk_window_present (GTK_WINDOW (new_window));
		fr_window_archive_open (FR_WINDOW (new_window), saved_file, GTK_WINDOW (new_window));
		close_progress_dialog (window, TRUE);
		break;
	case DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER:
		fr_window_view_extraction_destination_folder (window, (GFunc) progress_dialog_response__open_folder_cb, NULL);
		break;
	default:
		break;
	}

	_g_object_unref (saved_file);
}


static char *
get_action_description (FrWindow *window,
			FrAction  action,
			GFile    *file)
{
	char *basename;
	char *message;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	basename = _g_file_get_display_name (file);

	message = NULL;
	switch (action) {
	case FR_ACTION_CREATING_NEW_ARCHIVE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Creating “%s”"), basename);
		break;
	case FR_ACTION_LOADING_ARCHIVE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Loading “%s”"), basename);
		break;
	case FR_ACTION_LISTING_CONTENT:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Reading “%s”"), basename);
		break;
	case FR_ACTION_DELETING_FILES:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Deleting the files from “%s”"), basename);
		break;
	case FR_ACTION_TESTING_ARCHIVE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Testing “%s”"), basename);
		break;
	case FR_ACTION_GETTING_FILE_LIST:
		message = g_strdup (_("Getting the file list"));
		break;
	case FR_ACTION_COPYING_FILES_FROM_REMOTE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Copying the files to add to “%s”"), basename);
		break;
	case FR_ACTION_ADDING_FILES:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Adding the files to “%s”"), basename);
		break;
	case FR_ACTION_EXTRACTING_FILES:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Extracting the files from “%s”"), basename);
		break;
	case FR_ACTION_COPYING_FILES_TO_REMOTE:
		message = g_strdup (_("Copying the extracted files to the destination"));
		break;
	case FR_ACTION_CREATING_ARCHIVE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Creating “%s”"), basename);
		break;
	case FR_ACTION_SAVING_REMOTE_ARCHIVE:
	case FR_ACTION_ENCRYPTING_ARCHIVE:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Saving “%s”"), basename);
		break;
	case FR_ACTION_PASTING_FILES:
		message = g_strdup (private->custom_action_message);
		break;
	case FR_ACTION_RENAMING_FILES:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Renaming the files in “%s”"), basename);
		break;
	case FR_ACTION_UPDATING_FILES:
		/* Translators: %s is a filename */
		message = g_strdup_printf (_("Updating the files in “%s”"), basename);
		break;
	case FR_ACTION_NONE:
		break;
	}

	g_free (basename);

	return message;
}


static void
progress_dialog_set_action_description (FrWindow   *window,
					const char *description)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char *description_markup;

	description_markup = g_markup_printf_escaped ("<span weight=\"bold\" size=\"larger\">%s</span>", description);
	gtk_label_set_markup (GTK_LABEL (private->pd_action), description_markup);

	g_free (description_markup);
}


static void
progress_dialog_update_action_description (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GFile *current_archive;
	char  *description;


	if (private->progress_dialog == NULL)
		return;

	if (private->saving_file != NULL)
		current_archive = private->saving_file;
	else if (private->working_archive != NULL)
		current_archive = private->working_archive;
	else
		current_archive = private->archive_file;

	_g_clear_object (&private->pd_last_archive);
	if (current_archive != NULL)
		private->pd_last_archive = g_object_ref (current_archive);

	description = get_action_description (window, private->action, private->pd_last_archive);
	progress_dialog_set_action_description (window, description);

	g_free (description);
}


static void
fr_window_working_archive_cb (FrArchive  *archive,
			      const char *archive_uri,
			      FrWindow   *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_g_clear_object (&private->working_archive);
	if (archive_uri != NULL)
		private->working_archive = g_file_new_for_uri (archive_uri);
	progress_dialog_update_action_description (window);

	return;
}


static void
fr_archive_message_cb (FrArchive  *archive,
		       const char *msg,
		       FrWindow   *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->pd_last_message != msg) {
		g_free (private->pd_last_message);
		private->pd_last_message = g_strdup (msg);
	}

	if (private->progress_dialog == NULL)
		return;

	if (msg != NULL) {
		while (*msg == ' ')
			msg++;
		if (*msg == 0)
			msg = NULL;
	}

	if (msg != NULL) {
		char *utf8_msg;

		if (! g_utf8_validate (msg, -1, NULL))
			utf8_msg = g_locale_to_utf8 (msg, -1 , 0, 0, 0);
		else
			utf8_msg = g_strdup (msg);
		if (utf8_msg == NULL)
			return;

		if (g_utf8_validate (utf8_msg, -1, NULL)) {
			gtk_label_set_text (GTK_LABEL (private->pd_message), utf8_msg);
			gtk_widget_show (private->pd_message);
		}

		g_free (private->pd_last_message);
		private->pd_last_message = g_strdup (utf8_msg);

		g_signal_emit (G_OBJECT (window),
			       fr_window_signals[PROGRESS],
			       0,
			       private->pd_last_fraction,
			       private->pd_last_message);

#ifdef LOG_PROGRESS
		g_print ("message > %s\n", utf8_msg);
#endif

		g_free (utf8_msg);
	}
	else
		gtk_widget_hide (private->pd_message);

	progress_dialog_update_action_description (window);

	return;
}


static void
fr_archive_start_cb (FrArchive *archive,
		     FrAction   action,
		     FrWindow  *window)
{
	char *description;

	description = get_action_description (window, action, fr_archive_get_file (archive));
	fr_archive_message_cb (archive, description, window);

	g_free (description);
}


static void
create_the_progress_dialog (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWindow      *parent;
	GtkDialogFlags  flags;
	const char     *title;
	GtkBuilder     *builder;
	gboolean        use_header_bar;
	GtkWidget      *dialog;

	if (private->progress_dialog != NULL)
		return;

	if (private->batch_mode) {
		parent = NULL;
		flags = 0;
		title = private->batch_title;
	}
	else {
		parent = GTK_WINDOW (window);
		flags = GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL;
		title = NULL;
	}

	builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "progress-dialog.ui");
	use_header_bar = _gtk_settings_get_dialogs_use_header ();
	dialog = g_object_new (GTK_TYPE_DIALOG,
			       "use-header-bar", use_header_bar,
			       "default-width", PROGRESS_DIALOG_DEFAULT_WIDTH,
			       NULL);
	_gtk_box_pack_end (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			   _gtk_builder_get_widget (builder, "progress_dialog_content"),
			   TRUE,
			   TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), (flags & GTK_DIALOG_MODAL));
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	g_object_weak_ref (G_OBJECT (dialog), _g_object_unref_on_weak_notify, builder);

	_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				NULL);

	private->progress_dialog = dialog;
	private->pd_action = _gtk_builder_get_widget (builder, "action_label");
	private->pd_progress_bar = _gtk_builder_get_widget (builder, "progress_progressbar");
	private->pd_message = _gtk_builder_get_widget (builder, "message_label");
	private->pd_progress_box = _gtk_builder_get_widget (builder, "progress_box");

	_g_clear_object (&private->pd_last_archive);
	private->pd_last_archive = _g_object_ref (private->archive_file);

	progress_dialog_update_action_description (window);

	/* signals */

	g_signal_connect (GTK_DIALOG (private->progress_dialog),
			  "response",
			  G_CALLBACK (progress_dialog_response),
			  window);
}


static gboolean
display_progress_dialog (gpointer data)
{
	FrWindow *window = data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->progress_timeout != 0)
		g_source_remove (private->progress_timeout);

	if (private->use_progress_dialog && (private->progress_dialog != NULL)) {
		gtk_dialog_set_response_sensitive (GTK_DIALOG (private->progress_dialog),
						   GTK_RESPONSE_OK,
						   private->stoppable);
		if (! private->batch_mode)
			gtk_window_present (GTK_WINDOW (window));
		gtk_window_present (GTK_WINDOW (private->progress_dialog));
		fr_archive_message_cb (NULL, private->pd_last_message, window);
	}

	private->progress_timeout = 0;

	return FALSE;
}


static void
open_progress_dialog (FrWindow *window,
		      gboolean  open_now)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->hide_progress_timeout != 0) {
		g_source_remove (private->hide_progress_timeout);
		private->hide_progress_timeout = 0;
	}

	if (open_now) {
		if (private->progress_timeout != 0)
			g_source_remove (private->progress_timeout);
		private->progress_timeout = 0;
	}

	if ((private->progress_timeout != 0)
	    || ((private->progress_dialog != NULL) && gtk_widget_get_visible (private->progress_dialog)))
		return;

	create_the_progress_dialog (window);
	if (open_now) {
		display_progress_dialog (window);
	}
	else {
		private->progress_timeout = g_timeout_add (
			SHOW_PROGRESS_DELAY,
			display_progress_dialog,
			window
		);
	}
}


static void
fr_archive_progress_cb (FrArchive *archive,
			double     fraction,
			FrWindow  *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->progress_pulse = (fraction < 0.0);
	if (! private->progress_pulse) {
		fraction = CLAMP (fraction, 0.0, 1.0);
		if (private->progress_dialog != NULL)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (private->pd_progress_bar), fraction);

		if ((archive != NULL) && (fr_archive_progress_get_total_files (archive) > 0)) {
			char *message = NULL;
			int   remaining_files;

			remaining_files = fr_archive_progress_get_total_files (archive) - fr_archive_progress_get_completed_files (archive);

			switch (private->action) {
			case FR_ACTION_ADDING_FILES:
			case FR_ACTION_EXTRACTING_FILES:
			case FR_ACTION_DELETING_FILES:
			case FR_ACTION_UPDATING_FILES:
				if (remaining_files > 0)
					message = g_strdup_printf (ngettext ("%d file remaining",
									     "%'d files remaining",
									     remaining_files), remaining_files);
				else
					message = g_strdup (_("Please wait…"));
				break;
			default:
				break;
			}

			if (message != NULL) {
				fr_archive_message (archive, message);
				g_free (message);
			}
		}

		if (fraction == 1.0)
			gtk_widget_hide (private->pd_progress_box);
		else
			gtk_widget_show (private->pd_progress_box);

		private->pd_last_fraction = fraction;

		g_signal_emit (G_OBJECT (window),
			       fr_window_signals[PROGRESS],
			       0,
			       private->pd_last_fraction,
			       private->pd_last_message);

#ifdef LOG_PROGRESS
		g_print ("progress > %2.2f\n", fraction);
#endif
	}
	return;
}


/* -- confirmation dialog -- */


static void
fr_window_close_confirmation_dialog (FrWindow  *window,
				     GtkDialog *dialog)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gtk_window_destroy (GTK_WINDOW (dialog));
	if (private->batch_mode && private->destroy_with_confirmation_dialog)
		_fr_window_close_after_notification (window);
}


static void
confirmation_response__open_folder_cb (FrWindow  *window,
				       gpointer   user_data)
{
	GtkDialog *dialog = user_data;
	fr_window_close_confirmation_dialog (window, dialog);
}


static void
confirmation_dialog_response (GtkDialog *dialog,
			      int        response_id,
			      FrWindow  *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWidget *new_window;
	GFile     *saved_file;

	saved_file = private->saving_file;
	private->saving_file = NULL;

	switch (response_id) {
	case GTK_RESPONSE_CLOSE:
		fr_window_close_confirmation_dialog (window, dialog);
		break;

	case DIALOG_RESPONSE_OPEN_ARCHIVE:
		new_window = fr_window_new ();
		gtk_window_present (GTK_WINDOW (new_window));
		fr_window_archive_open (FR_WINDOW (new_window), saved_file, GTK_WINDOW (new_window));
		fr_window_close_confirmation_dialog (window, dialog);
		break;

	case DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER:
		fr_window_view_extraction_destination_folder (window, (GFunc) confirmation_response__open_folder_cb, dialog);
		break;

	default:
		break;
	}

	_g_object_unref (saved_file);
}


static void
fr_window_show_confirmation_dialog (FrWindow  *window,
				    GtkDialog *dialog)
{
	close_progress_dialog (window, TRUE);
	g_signal_connect (dialog,
			  "response",
			  G_CALLBACK (confirmation_dialog_response),
			  window);
	gtk_window_present (GTK_WINDOW (dialog));
}


static void
fr_window_show_confirmation_dialog_with_open_destination (FrWindow *window)
{
	GtkDialog *dialog = GTK_DIALOG (_gtk_message_dialog_new (GTK_WINDOW (window),
					  GTK_DIALOG_MODAL,
					  _("Extraction completed successfully"),
					  NULL,
					  _GTK_LABEL_CLOSE, GTK_RESPONSE_CLOSE,
					  _("_Show the Files"), DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER,
					  NULL));

	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_CLOSE);
	fr_window_show_confirmation_dialog (window, dialog);
}


static void
fr_window_show_confirmation_dialog_with_open_archive (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWidget *dialog;
	char      *basename;
	char      *message;

	basename = _g_file_get_display_name (private->saving_file);
	/* Translators: %s is a filename */
	message = g_strdup_printf (_("“%s” created successfully"), basename);

	dialog = _gtk_message_dialog_new (GTK_WINDOW (window),
					  GTK_DIALOG_MODAL,
					  message,
					  NULL,
					  _GTK_LABEL_CLOSE, GTK_RESPONSE_CLOSE,
					  _("_Open the Archive"), DIALOG_RESPONSE_OPEN_ARCHIVE,
					  NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
	fr_window_show_confirmation_dialog (window, GTK_DIALOG (dialog));

	g_free (message);
	g_free (basename);
}


static void
fr_window_add_to_recent_list (FrWindow *window,
			      GFile    *file)
{
	char *uri;

	if (_g_file_is_temp_dir (file))
		return;

	uri = g_file_get_uri (file);

	if (window->archive->mime_type != NULL) {
		GtkRecentData *recent_data;

		recent_data = g_new0 (GtkRecentData, 1);
		recent_data->mime_type = g_content_type_get_mime_type (window->archive->mime_type);
		recent_data->app_name = "File Roller";
		recent_data->app_exec = "file-roller";
		gtk_recent_manager_add_full (gtk_recent_manager_get_default (), uri, recent_data);

		g_free (recent_data->mime_type);
		g_free (recent_data);
	}
	else
		gtk_recent_manager_add_item (gtk_recent_manager_get_default (), uri);

	g_free (uri);
}


static void
fr_window_remove_from_recent_list (FrWindow *window,
				   GFile    *file)
{
	char *uri;

	if (file == NULL)
		return;

	uri = g_file_get_uri (file);
	gtk_recent_manager_remove_item (gtk_recent_manager_get_default (), uri, NULL);

	g_free (uri);
}


static void
error_dialog_response_cb (GtkDialog *dialog,
			  gint       arg1,
			  gpointer   user_data)
{
	FrWindow  *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->showing_error_dialog = FALSE;
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (private->destroy_with_error_dialog) {
		if (private->batch_mode) {
			g_signal_emit (window,
				       fr_window_signals[READY],
				       0,
				       NULL);
		}
		gtk_window_destroy (GTK_WINDOW (window));
	}
}


static void
fr_window_show_error_dialog (FrWindow   *window,
			     GtkWidget  *dialog,
			     GtkWindow  *dialog_parent,
			     const char *details)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->batch_mode && ! private->use_progress_dialog) {
		GError *error;

		error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, details ? details : _("Command exited abnormally."));
		g_signal_emit (window,
			       fr_window_signals[READY],
			       0,
			       error);

		gtk_window_destroy (GTK_WINDOW (window));

		return;
	}

	close_progress_dialog (window, TRUE);

	if (private->batch_mode)
		fr_window_destroy_with_error_dialog (window);

	g_signal_connect (GTK_DIALOG (dialog),
			  "response",
			  G_CALLBACK (error_dialog_response_cb),
			  window);
	if (dialog_parent != NULL)
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_present (GTK_WINDOW (dialog));

	private->showing_error_dialog = TRUE;
}


void
fr_window_destroy_with_error_dialog (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->destroy_with_error_dialog = TRUE;
}


void
fr_window_set_notify (FrWindow   *window,
		      gboolean    notify)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->notify = notify;
}


static void
_handle_archive_operation_error (FrWindow  *window,
				 FrArchive *archive,
				 FrAction   action,
				 GError    *error,
				 gboolean  *continue_batch,
				 gboolean  *opens_dialog)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWindow *dialog_parent;
	char      *msg;
	char      *utf8_name;
	char      *details;
	GList     *output;
	GtkWidget *dialog;

	if (continue_batch) *continue_batch = (error == NULL);
	if (opens_dialog) *opens_dialog = FALSE;

	if (error == NULL)
		return;

	if ((error != NULL) && (error->code == FR_ERROR_STOPPED))
		g_cancellable_reset (private->cancellable);

	switch (error->code) {
	case FR_ERROR_ASK_PASSWORD:
		close_progress_dialog (window, TRUE);
		dlg_ask_password (window);
		if (opens_dialog) *opens_dialog = TRUE;
		break;

	case FR_ERROR_UNSUPPORTED_FORMAT:
		close_progress_dialog (window, TRUE);
		dlg_package_installer (window,
				       private->archive_file,
				       action,
				       private->cancellable);
		if (opens_dialog) *opens_dialog = TRUE;
		break;

#if 0
	case FR_PROC_ERROR_BAD_CHARSET:
		close_progress_dialog (window, TRUE);
		/* dlg_ask_archive_charset (window); FIXME: implement after feature freeze */
		break;
#endif

	case FR_ERROR_STOPPED:
		/* nothing */
		break;

	default:
		/* generic error => show an error dialog */

		msg = NULL;
		details = NULL;
		output = NULL;

		if (private->batch_mode) {
			dialog_parent = NULL;
			private->load_error_parent_window = NULL;
		}
		else {
			dialog_parent = (GtkWindow *) window;
			if (private->load_error_parent_window == NULL)
				private->load_error_parent_window = (GtkWindow *) window;
		}

		switch (action) {
		case FR_ACTION_CREATING_NEW_ARCHIVE:
			dialog_parent = private->load_error_parent_window;
			msg = _("Could not create the archive");
			break;

		case FR_ACTION_EXTRACTING_FILES:
		case FR_ACTION_COPYING_FILES_TO_REMOTE:
			msg = _("An error occurred while extracting files.");
			break;

		case FR_ACTION_LOADING_ARCHIVE:
			dialog_parent = private->load_error_parent_window;
			utf8_name = _g_file_get_display_name (private->archive_file);
			msg = g_strdup_printf (_("Could not open “%s”"), utf8_name);
			g_free (utf8_name);
			break;

		case FR_ACTION_LISTING_CONTENT:
			msg = _("An error occurred while loading the archive.");
			break;

		case FR_ACTION_DELETING_FILES:
			msg = _("An error occurred while deleting files from the archive.");
			break;

		case FR_ACTION_ADDING_FILES:
		case FR_ACTION_GETTING_FILE_LIST:
		case FR_ACTION_COPYING_FILES_FROM_REMOTE:
			msg = _("An error occurred while adding files to the archive.");
			break;

		case FR_ACTION_TESTING_ARCHIVE:
			msg = _("An error occurred while testing archive.");
			break;

		case FR_ACTION_SAVING_REMOTE_ARCHIVE:
		case FR_ACTION_ENCRYPTING_ARCHIVE:
			msg = _("An error occurred while saving the archive.");
			break;

		case FR_ACTION_RENAMING_FILES:
			msg = _("An error occurred while renaming the files.");
			break;

		case FR_ACTION_UPDATING_FILES:
			msg = _("An error occurred while updating the files.");
			break;

		default:
			msg = _("An error occurred.");
			break;
		}

		switch (error->code) {
		case FR_ERROR_COMMAND_NOT_FOUND:
			details = _("Command not found.");
			break;
		case FR_ERROR_EXITED_ABNORMALLY:
		case FR_ERROR_COMMAND_ERROR:
			details = _("Command exited abnormally.");
			break;
		case FR_ERROR_SPAWN:
			details = error->message;
			break;
		default:
			details = error->message;
			break;
		}

		if ((error->code != FR_ERROR_GENERIC) && FR_IS_COMMAND (archive))
			output = fr_command_get_last_output (FR_COMMAND (archive));

		dialog = _gtk_error_dialog_new (dialog_parent,
						0,
						output,
						msg,
						((details != NULL) ? "%s" : NULL),
						details);
		fr_window_show_error_dialog (window, dialog, dialog_parent, details);
		break;
	}
}


static void fr_window_batch_exec_next_action (FrWindow *window);
static void fr_window_archive_list (FrWindow *window);


static void
fr_window_update_stack_content (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean no_archive = (window->archive == NULL) || ! private->archive_present;
	gboolean loading_archive = private->action == FR_ACTION_LOADING_ARCHIVE;
	gtk_stack_set_visible_child_name (GTK_STACK (private->content_stack),
		no_archive ? (loading_archive ? LOADING_ARCHIVE : EMPTY_STATUS)
		: ARCHIVE_CONTENT
	);
	if (no_archive)
		gtk_widget_add_css_class (private->headerbar, "flat");
	else
		gtk_widget_remove_css_class (private->headerbar, "flat");
}


static void
_archive_operation_completed (FrWindow *window,
			      FrAction  action,
			      GError   *error)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean  continue_batch = FALSE;
	gboolean  opens_dialog;
	gboolean  operation_canceled;
	GFile    *archive_dir;
	gboolean  is_temp_dir;

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	_fr_window_stop_activity_mode (window);
	_handle_archive_operation_error (window, window->archive, action, error, &continue_batch, &opens_dialog);
	if (opens_dialog)
		return;

	operation_canceled = g_error_matches (error, FR_ERROR, FR_ERROR_STOPPED);
	private->action = FR_ACTION_NONE;

	switch (action) {
	case FR_ACTION_CREATING_NEW_ARCHIVE:
	case FR_ACTION_CREATING_ARCHIVE:
		close_progress_dialog (window, FALSE);
		if (! operation_canceled) {
			fr_window_history_clear (window);
			fr_window_go_to_location (window, "/", TRUE);
			fr_window_update_dir_tree (window);
			fr_window_update_title (window);
			fr_window_update_sensitivity (window);
		}
		fr_window_update_stack_content (window);
		break;

	case FR_ACTION_LOADING_ARCHIVE:
		close_progress_dialog (window, FALSE);
		if (error != NULL) {
			fr_window_remove_from_recent_list (window, private->archive_file);
			fr_window_archive_close (window);
		}
		else {
			fr_window_archive_list (window);
			return;
		}
		break;

	case FR_ACTION_LISTING_CONTENT:
		/* update the file because multi-volume archives can have
		 * a different name after loading. */
		_g_object_unref (private->archive_file);
		private->archive_file = _g_object_ref (fr_archive_get_file (window->archive));

		private->reload_archive = FALSE;

		close_progress_dialog (window, FALSE);
		if (error != NULL) {
			fr_window_remove_from_recent_list (window, private->archive_file);
			fr_window_archive_close (window);
			fr_window_set_password (window, NULL);
			break;
		}

		/* error == NULL */

		archive_dir = g_file_get_parent (private->archive_file);
		is_temp_dir = _g_file_is_temp_dir (archive_dir);
		if (! private->archive_present) {
			private->archive_present = TRUE;

			fr_window_history_clear (window);
			fr_window_history_add (window, "/");

			if (! is_temp_dir) {
				fr_window_set_open_default_dir (window, archive_dir);
				fr_window_set_add_default_dir (window, archive_dir);
				if (! private->freeze_default_dir)
					fr_window_set_extract_default_dir (window, archive_dir);
			}

			private->archive_new = FALSE;
		}
		g_object_unref (archive_dir);

		if (! is_temp_dir)
			fr_window_add_to_recent_list (window, private->archive_file);

		fr_window_update_history (window);
		fr_window_update_title (window);
		fr_window_go_to_location (window, fr_window_get_current_location (window), TRUE);
		fr_window_update_dir_tree (window);
		fr_window_update_stack_content (window);

		if (! private->batch_mode)
			gtk_window_present (GTK_WINDOW (window));
		break;

	case FR_ACTION_DELETING_FILES:
	case FR_ACTION_ADDING_FILES:
		close_progress_dialog (window, FALSE);

		/* update the file because multi-volume archives can have
		 * a different name after creation. */
		_g_object_unref (private->archive_file);
		private->archive_file = _g_object_ref (fr_archive_get_file (window->archive));

		if (private->notify) {
			_g_object_unref (private->saving_file);
			private->saving_file = g_object_ref (private->archive_file);
		}

		if (error == NULL) {
			if (private->archive_new)
				private->archive_new = FALSE;
			fr_window_add_to_recent_list (window, private->archive_file);
		}

		if (! private->batch_mode && ! operation_canceled)
			private->reload_archive = TRUE;

		break;

	case FR_ACTION_TESTING_ARCHIVE:
		close_progress_dialog (window, FALSE);
		if (error == NULL)
			fr_window_view_last_output (window, _("Test Result"));
		return;

	case FR_ACTION_EXTRACTING_FILES:
		close_progress_dialog (window, FALSE);
		break;

	case FR_ACTION_RENAMING_FILES:
	case FR_ACTION_UPDATING_FILES:
		close_progress_dialog (window, FALSE);
		if (! private->batch_mode && ! operation_canceled)
			private->reload_archive = TRUE;
		break;

	default:
		close_progress_dialog (window, FALSE);
		continue_batch = FALSE;
		break;
	}

	if (continue_batch)
		fr_window_batch_exec_next_action (window);
	else
		fr_window_batch_stop (window);
}


static void
_archive_operation_cancelled (FrWindow *window,
			      FrAction  action)
{
	GError *error;

	error = g_error_new_literal (FR_ERROR, FR_ERROR_STOPPED, "");
	_archive_operation_completed (window, action, error);

	g_error_free (error);
}


static void
_archive_operation_started (FrWindow *window,
			    FrAction  action)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->action = action;
	_fr_window_start_activity_mode (window);

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [START] (FR::Window)\n", action_names[action]);
#endif

	switch (action) {
	case FR_ACTION_EXTRACTING_FILES:
		open_progress_dialog (window, ((private->saving_file != NULL)
					       || private->batch_mode));
		break;
	default:
		if (action == FR_ACTION_LOADING_ARCHIVE)
			fr_window_update_stack_content (window);
		open_progress_dialog (window, private->batch_mode);
		break;
	}

	fr_archive_progress_cb (NULL, -1.0, window);
	fr_archive_message_cb (NULL, _("Please wait…"), window);
}


/* -- selections -- */


#undef DEBUG_GET_DIR_LIST_FROM_PATH

/**
 * get_dir_list_from_path:
 * Returns: (element-type filename) (transfer full)
 */
static GList *
get_dir_list_from_path (FrWindow *window,
			char     *path)
{
	char  *dirname;
	size_t dirname_l;
	GList *list = NULL;

	if (path[strlen (path) - 1] != '/')
		dirname = g_strconcat (path, "/", NULL);
	else
		dirname = g_strdup (path);
	dirname_l = strlen (dirname);
	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fd = g_ptr_array_index (window->archive->files, i);
		gboolean  matches = FALSE;

#ifdef DEBUG_GET_DIR_LIST_FROM_PATH
		g_print ("%s <=> %s (%d)\n", dirname, fd->full_path, dirname_l);
#endif

		if (fd->dir) {
			size_t full_path_l = strlen (fd->full_path);
			if ((full_path_l == dirname_l - 1) && (strncmp (dirname, fd->full_path, full_path_l) == 0))
				/* example: dirname is '/path/to/dir/' and fd->full_path is '/path/to/dir' */
				matches = TRUE;
			else if (strcmp (dirname, fd->full_path) == 0)
				matches = TRUE;
		}

		if (! matches && strncmp (dirname, fd->full_path, dirname_l) == 0) {
			matches = TRUE;
		}

		if (matches) {
#ifdef DEBUG_GET_DIR_LIST_FROM_PATH
			g_print ("`-> OK\n");
#endif
			list = g_list_prepend (list, g_strdup (fd->original_path));
		}
	}
	g_free (dirname);

	return g_list_reverse (list);
}


static GList *
get_dir_list_from_file_data (FrWindow *window,
			     FrFileData *fdata)
{
	char  *dirname;
	GList *list;

	dirname = g_strconcat (fr_window_get_current_location (window),
			       fdata->list_name,
			       NULL);
	list = get_dir_list_from_path (window, dirname);
	g_free (dirname);

	return list;
}


GList *
fr_window_get_file_list_selection (FrWindow *window,
				   gboolean  recursive,
				   gboolean  include_dirs,
				   gboolean *has_dirs)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeSelection *selection;
	GList            *selections = NULL, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected_from_list_view, &selections);

	list = NULL;
	for (scan = selections; scan; scan = scan->next) {
		FrFileData *fd = scan->data;

		if (!fd)
			continue;

		if (fr_file_data_is_dir (fd)) {
			if (has_dirs != NULL)
				*has_dirs = TRUE;

			if (include_dirs) {
				list = g_list_append (list, g_strdup (fd->original_path));
			}

			if (recursive)
				list = g_list_concat (list, get_dir_list_from_file_data (window, fd));
		}
		else
			list = g_list_prepend (list, g_strdup (fd->original_path));
	}
	if (selections)
		g_list_free (selections);

	return g_list_reverse (list);
}


GList *
fr_window_get_folder_tree_selection (FrWindow *window,
				     gboolean  recursive,
				     gboolean *has_dirs)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeSelection *tree_selection;
	GList            *selections, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->tree_view));
	if (tree_selection == NULL)
		return NULL;

	selections = NULL;
	gtk_tree_selection_selected_foreach (tree_selection, add_selected_from_tree_view, &selections);
	if (selections == NULL)
		return NULL;

	if (has_dirs != NULL)
		*has_dirs = TRUE;

	list = NULL;
	for (scan = selections; scan; scan = scan->next) {
		char *path = scan->data;

		if (recursive)
			list = g_list_concat (list, get_dir_list_from_path (window, path));
	}
	_g_string_list_free (selections);

	return g_list_reverse (list);
}


GList *
fr_window_get_file_list_from_path_list (FrWindow *window,
					GList    *path_list,
					gboolean *has_dirs)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeModel *model;
	GList        *selections, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	model = GTK_TREE_MODEL (private->list_store);
	selections = NULL;

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	for (scan = path_list; scan; scan = scan->next) {
		GtkTreeRowReference *reference = scan->data;
		GtkTreePath         *path;
		GtkTreeIter          iter;
		FrFileData *fdata;

		path = gtk_tree_row_reference_get_path (reference);
		if (path == NULL)
			continue;

		if (! gtk_tree_model_get_iter (model, &iter, path))
			continue;

		gtk_tree_model_get (model, &iter,
				    COLUMN_FILE_DATA, &fdata,
				    -1);

		selections = g_list_prepend (selections, fdata);
	}

	list = NULL;
	for (scan = selections; scan; scan = scan->next) {
		FrFileData *fd = scan->data;

		if (!fd)
			continue;

		if (fr_file_data_is_dir (fd)) {
			if (has_dirs != NULL)
				*has_dirs = TRUE;
			list = g_list_concat (list, get_dir_list_from_file_data (window, fd));
		}
		else
			list = g_list_prepend (list, g_strdup (fd->original_path));
	}

	if (selections != NULL)
		g_list_free (selections);

	return g_list_reverse (list);
}


GList *
fr_window_get_file_list_pattern (FrWindow    *window,
				 const char  *pattern)
{
	GRegex **regexps;
	GList   *list;

	g_return_val_if_fail (window != NULL, NULL);

	regexps = _g_regexp_split_from_patterns (pattern, G_REGEX_CASELESS);
	list = NULL;
	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fd = g_ptr_array_index (window->archive->files, i);
		char     *utf8_name;

		if (fd == NULL)
			continue;

		utf8_name = g_filename_to_utf8 (fd->name, -1, NULL, NULL, NULL);
		if (_g_regexp_matchv (regexps, utf8_name, 0))
			list = g_list_prepend (list, g_strdup (fd->original_path));
		g_free (utf8_name);
	}
	_g_regexp_freev (regexps);

	return g_list_reverse (list);
}


static GList *
fr_window_get_file_list (FrWindow *window)
{
	GList *list;

	g_return_val_if_fail (window != NULL, NULL);

	list = NULL;
	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fd = g_ptr_array_index (window->archive->files, i);
		list = g_list_prepend (list, g_strdup (fd->original_path));
	}

	return g_list_reverse (list);
}


int
fr_window_get_n_selected_files (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return gtk_tree_selection_count_selected_rows (gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view)));
}


/**/


static void
dir_tree_button_pressed_cb (GtkGestureClick *gesture,
			    gint             n_press,
			    gdouble          x,
			    gdouble          y,
			    gpointer         user_data)
{
	FrWindow         *window = user_data;
	FrWindowPrivate  *private = fr_window_get_instance_private (window);
	GtkTreeSelection *selection;
	int               button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->tree_view));
	if (selection == NULL)
		return;

	if ((n_press == 1) && (button == GDK_BUTTON_SECONDARY)) {
		GtkTreePath *path;
		GtkTreeIter  iter;
		int	     bx, by;

		gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (private->tree_view), x, y, &bx, &by);

		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (private->tree_view),
						   bx, by,
						   &path, NULL, NULL, NULL))
		{
			if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (private->tree_store), &iter, path)) {
				gtk_tree_path_free (path);
				return;
			}
			gtk_tree_path_free (path);

			if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
				gtk_tree_selection_unselect_all (selection);
				gtk_tree_selection_select_iter (selection, &iter);
			}

			_gtk_popover_popup_at_position (GTK_POPOVER (private->sidebar_folder_popup_menu), x, y);
		}
		else
			gtk_tree_selection_unselect_all (selection);
	}
	else if ((n_press == 1) && (button == 8)) {
		fr_window_go_back (window);
	}
	else if ((n_press == 1) && (button == 9)) {
		fr_window_go_forward (window);
	}
}


static FrFileData *
fr_window_get_selected_item_from_file_list (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeSelection *tree_selection;
	GList            *selection;
	FrFileData *fdata = NULL;

	g_return_val_if_fail (window != NULL, NULL);

	tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
	if (tree_selection == NULL)
		return NULL;

	selection = NULL;
	gtk_tree_selection_selected_foreach (tree_selection, add_selected_from_list_view, &selection);
	if ((selection == NULL) || (selection->next != NULL)) {
		/* return NULL if the selection contains more than one entry. */
		g_list_free (selection);
		return NULL;
	}

	fdata = fr_file_data_copy (selection->data);
	g_list_free (selection);

	return fdata;
}


static char *
fr_window_get_selected_folder_in_tree_view (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeSelection *tree_selection;
	GList            *selections;
	char             *path = NULL;

	g_return_val_if_fail (window != NULL, NULL);

	tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->tree_view));
	if (tree_selection == NULL)
		return NULL;

	selections = NULL;
	gtk_tree_selection_selected_foreach (tree_selection, add_selected_from_tree_view, &selections);

	if (selections != NULL) {
		path = selections->data;
		g_list_free (selections);
	}

	return path;
}


void
fr_window_current_folder_activated (FrWindow *window,
				    gboolean   from_sidebar)
{
	char *dir_path;

	if (! from_sidebar) {
		FrFileData *fdata;
		char     *dir_name;

		fdata = fr_window_get_selected_item_from_file_list (window);
		if ((fdata == NULL) || ! fr_file_data_is_dir (fdata)) {
			fr_file_data_free (fdata);
			return;
		}
		dir_name = g_strdup (fdata->list_name);
		dir_path = g_strconcat (fr_window_get_current_location (window),
					dir_name,
					"/",
					NULL);
		g_free (dir_name);
		fr_file_data_free (fdata);
	}
	else
		dir_path = fr_window_get_selected_folder_in_tree_view (window);

	fr_window_go_to_location (window, dir_path, FALSE);

	g_free (dir_path);
}


static void
row_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  gpointer           data)
{
	FrWindow    *window = data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrFileData *fdata;
	GtkTreeIter  iter;

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (private->list_store),
				       &iter,
				       path))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (private->list_store), &iter,
			    COLUMN_FILE_DATA, &fdata,
			    -1);

	if (! fr_file_data_is_dir (fdata)) {
		GList *list = g_list_prepend (NULL, fdata->original_path);
		fr_window_open_files (window, list, FALSE);
		g_list_free (list);
	}
	else if (private->list_mode == FR_WINDOW_LIST_MODE_AS_DIR) {
		char *new_dir;
		new_dir = g_strconcat (fr_window_get_current_location (window),
				       fdata->list_name,
				       "/",
				       NULL);
		fr_window_go_to_location (window, new_dir, FALSE);
		g_free (new_dir);
	}

	return;
}


static void
list_view_button_pressed_cb (GtkGestureClick *gesture,
			     gint             n_press,
			     gdouble          x,
			     gdouble          y,
			     gpointer         user_data)
{
	FrWindow         *window = user_data;
	FrWindowPrivate  *private = fr_window_get_instance_private (window);
	GtkTreeSelection *selection;
	int               button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
	if (selection == NULL)
		return;

	if ((n_press == 1) && (button == GDK_BUTTON_SECONDARY)) {
		GtkTreePath *path;
		GtkTreeIter  iter;
		int	     n_selected;
		int          bx, by;

		gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (private->list_view), x, y, &bx, &by);

		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (private->list_view),
						   bx, by,
						   &path, NULL, NULL, NULL))
		{
			if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (private->list_store), &iter, path)) {
				gtk_tree_path_free (path);
				return;
			}
			gtk_tree_path_free (path);

			if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
				gtk_tree_selection_unselect_all (selection);
				gtk_tree_selection_select_iter (selection, &iter);
			}
		}
		else
			gtk_tree_selection_unselect_all (selection);

		n_selected = fr_window_get_n_selected_files (window);
		if ((n_selected == 1) && selection_has_a_dir (window))
			_gtk_popover_popup_at_position (GTK_POPOVER (private->folder_popup_menu), x, y);
		else
			_gtk_popover_popup_at_position (GTK_POPOVER (private->file_popup_menu), x, y);
	}
	else if ((n_press == 1) && (button == 8)) {
		fr_window_go_back (window);
	}
	else if ((n_press == 1) && (button == 9)) {
		fr_window_go_forward (window);
	}
}


/* -- drag and drop -- */


static void fr_window_paste_from_clipboard_data (FrWindow *window, FrClipboardData *data);


gboolean
fr_window_create_archive_and_continue (FrWindow   *window,
				       GFile      *file,
				       const char *mime_type,
				       GtkWindow  *error_dialog_parent)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean result = FALSE;

	if (fr_window_archive_new (FR_WINDOW (window), file, mime_type)) {
		if (! fr_window_is_batch_mode (FR_WINDOW (window)))
			gtk_window_present (GTK_WINDOW (window));
		_archive_operation_completed (window, FR_ACTION_CREATING_NEW_ARCHIVE, NULL);

		result = TRUE;
	}
	else {
		GError *error;

		error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, _("Archive type not supported."));
		private->load_error_parent_window = error_dialog_parent;
		_archive_operation_completed (window, FR_ACTION_CREATING_NEW_ARCHIVE, error);

		g_error_free (error);

		result = FALSE;
	}

	return result;
}


static gboolean
_fr_window_get_ask_to_open_destination (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return ! private->batch_mode || private->notify;
}


static void
new_archive_get_file_cb (FrNewArchiveDialog *dialog,
			 GFile              *file,
			 const char         *mime_type,
			 gpointer            user_data)
{
	FrWindow  *window = user_data;
	gboolean   new_window;
	GtkWidget *archive_window;

	if (file == NULL)
		return;

	new_window = fr_window_archive_is_present (window) && ! fr_window_is_batch_mode (window);
	archive_window = new_window ? fr_window_new () : GTK_WIDGET (window);
	fr_window_set_password (FR_WINDOW (archive_window), fr_new_archive_dialog_get_password (dialog));
	fr_window_set_encrypt_header (FR_WINDOW (archive_window), fr_new_archive_dialog_get_encrypt_header (dialog));
	fr_window_set_volume_size (FR_WINDOW (archive_window), fr_new_archive_dialog_get_volume_size (dialog));

	if (fr_window_create_archive_and_continue (FR_WINDOW (archive_window),
						   file,
						   mime_type,
						   GTK_WINDOW (dialog)))
	{
		gtk_window_destroy (GTK_WINDOW (dialog));
	}
	else if (new_window)
		gtk_window_destroy (GTK_WINDOW (archive_window));
}


static void
new_archive_dialog_response_cb (GtkDialog *dialog,
				int        response,
				gpointer   user_data)
{
	FrWindow *window = user_data;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		_archive_operation_cancelled (window, FR_ACTION_CREATING_NEW_ARCHIVE);
		return;
	}

	fr_new_archive_dialog_get_file (FR_NEW_ARCHIVE_DIALOG (dialog),
					new_archive_get_file_cb,
					user_data);
}


/* Dropped files */


typedef struct {
	FrWindow *window;
	GList    *file_list;
} DroppedData;


static DroppedData*
dropped_data_new (FrWindow *window, GList *file_list)
{
	DroppedData *data = g_new0 (DroppedData, 1);
	data->window = g_object_ref (window);
	data->file_list = _g_object_list_ref (file_list);
	return data;
}


static void
dropped_data_free (DroppedData *data)
{
	_g_object_unref (data->window);
	_g_object_list_unref (data->file_list);
	g_free (data);
}


static void
dropped_one_archive_dialog_response (GtkDialog *dialog,
				     int        response_id,
				     gpointer   user_data)
{
	DroppedData *data = user_data;

	if (response_id == DIALOG_RESPONSE_ADD_TO_ARCHIVE)
		fr_window_archive_add_dropped_items (data->window, data->file_list);
	else if (response_id == DIALOG_RESPONSE_OPEN_ARCHIVE)
		fr_window_archive_open (data->window, G_FILE (data->file_list->data), GTK_WINDOW (data->window));

	gtk_window_destroy (GTK_WINDOW (dialog));
	dropped_data_free (data);
}


static void
dropped_files_create_archive_dialog_response (GtkDialog *dialog,
					      int        response_id,
					      gpointer   user_data)
{
	DroppedData *data = user_data;
	FrWindow    *window = data->window;
	GList       *list = data->file_list;
	GFile       *first_file;
	GFile       *folder;
	char        *archive_name;
	GtkWidget   *new_archive_dialog;

	gtk_window_destroy (GTK_WINDOW (dialog));
	if (response_id != GTK_RESPONSE_YES) {
		dropped_data_free (data);
		return;
	}

	fr_window_free_batch_data (window);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_ADD,
				       _g_object_list_ref (list),
				       (GFreeFunc) _g_object_list_unref);

	first_file = G_FILE (list->data);
	folder = g_file_get_parent (first_file);
	if (folder != NULL)
		fr_window_set_open_default_dir (window, folder);

	if ((list->next != NULL) && (folder != NULL))
		archive_name = g_file_get_basename (folder);
	else
		archive_name = g_file_get_basename (first_file);

	new_archive_dialog = fr_new_archive_dialog_new (_("New Archive"),
							GTK_WINDOW (window),
							FR_NEW_ARCHIVE_ACTION_SAVE_AS,
							fr_window_get_open_default_dir (window),
							archive_name,
							NULL);
	gtk_window_set_modal (GTK_WINDOW (new_archive_dialog), TRUE);
	g_signal_connect (GTK_DIALOG (new_archive_dialog),
			  "response",
			  G_CALLBACK (new_archive_dialog_response_cb),
			  window);
	fr_new_archive_dialog_show_options (FR_NEW_ARCHIVE_DIALOG (new_archive_dialog));

	g_free (archive_name);
	_g_object_unref (folder);

	dropped_data_free (data);
}


static void
fr_window_on_dropped_files (FrWindow *window,
			    GList    *list /* GFile list */)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean one_file = (list->next == NULL);
	gboolean is_an_archive = one_file ? _g_file_is_archive (G_FILE (list->data)) : FALSE;

	if (private->archive_present
	    && (window->archive != NULL)
	    && ! window->archive->read_only
	    && fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_STORE_MANY_FILES))
	{
		if (one_file && is_an_archive) {
			GtkWidget *dialog;

			dialog = _gtk_message_dialog_new (GTK_WINDOW (window),
							  GTK_DIALOG_MODAL,
							  _("Do you want to add this file to the current archive or open it as a new archive?"),
							  NULL,
							  _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
							  _GTK_LABEL_ADD, DIALOG_RESPONSE_ADD_TO_ARCHIVE,
							  _GTK_LABEL_OPEN, DIALOG_RESPONSE_OPEN_ARCHIVE,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), DIALOG_RESPONSE_OPEN_ARCHIVE);
			g_signal_connect (GTK_DIALOG (dialog),
					  "response",
					  G_CALLBACK (dropped_one_archive_dialog_response),
					  dropped_data_new (window, list));
			gtk_window_present (GTK_WINDOW (dialog));
		}
		else
			fr_window_archive_add_dropped_items (window, list);
	}
	else {
		if (one_file && is_an_archive)
			fr_window_archive_open (window, G_FILE (list->data), GTK_WINDOW (window));
		else {
			GtkWidget *dialog;

			dialog = _gtk_message_dialog_new (GTK_WINDOW (window),
							  GTK_DIALOG_MODAL,
							  _("Do you want to create a new archive with these files?"),
							  NULL,
							  _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
							  _("Create _Archive"), GTK_RESPONSE_YES,
							  NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
			g_signal_connect (GTK_DIALOG (dialog),
					  "response",
					  G_CALLBACK (dropped_files_create_archive_dialog_response),
					  dropped_data_new (window, list));
			gtk_window_present (GTK_WINDOW (dialog));
		}
	}
}


static gboolean
fr_window_on_drop (GtkDropTarget *target,
		   const GValue  *value,
		   double         x,
		   double         y,
		   gpointer       data)
{
	FrWindow *window = data;

	GList *file_list = NULL;
	if (G_VALUE_HOLDS (value, G_TYPE_FILE)) {
		GFile *file = g_value_get_object (value);
		file_list = g_list_append (NULL, file);
	}
	else if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST)) {
		GSList *slist = g_value_get_boxed (value);
		for (GSList *sscan = slist; sscan; sscan = sscan->next) {
			file_list = g_list_prepend (file_list, sscan->data);
		}
		file_list = g_list_reverse (file_list);
	}
	else {
		return FALSE;
	}

	fr_window_on_dropped_files (window, file_list);
	g_list_free (file_list);

	return TRUE;
}


/* -- window_new -- */


static void
fr_window_update_columns_visibility (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkTreeView       *tree_view = GTK_TREE_VIEW (private->list_view);
	GtkTreeViewColumn *column;

	column = gtk_tree_view_get_column (tree_view, 1);
	gtk_tree_view_column_set_visible (column, g_settings_get_boolean (private->settings_listing, PREF_LISTING_SHOW_SIZE));

	column = gtk_tree_view_get_column (tree_view, 2);
	gtk_tree_view_column_set_visible (column, g_settings_get_boolean (private->settings_listing, PREF_LISTING_SHOW_TYPE));

	column = gtk_tree_view_get_column (tree_view, 3);
	gtk_tree_view_column_set_visible (column, g_settings_get_boolean (private->settings_listing, PREF_LISTING_SHOW_TIME));

	column = gtk_tree_view_get_column (tree_view, 4);
	gtk_tree_view_column_set_visible (column, g_settings_get_boolean (private->settings_listing, PREF_LISTING_SHOW_PATH));
}


static gboolean
key_press_cb (GtkEventControllerKey *self,
	      guint                  keyval,
	      guint                  keycode,
	      GdkModifierType        state,
	      gpointer               user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean  retval = GDK_EVENT_PROPAGATE;
	guint     modifiers;
	gboolean  alt_pressed;

	if (gtk_widget_has_focus (private->location_entry))
		return GDK_EVENT_PROPAGATE;

	if (gtk_widget_has_focus (private->filter_entry))
		return GDK_EVENT_PROPAGATE;

	modifiers = gtk_accelerator_get_default_mod_mask ();
	alt_pressed = (state & modifiers) == GDK_ALT_MASK;

	switch (keyval) {
	case GDK_KEY_Escape:
		fr_window_stop (window);
		if (private->filter_mode)
			fr_window_deactivate_filter (window);
		retval = GDK_EVENT_STOP;
		break;

	case GDK_KEY_F10:
	case GDK_KEY_Menu:
		if ((keyval == GDK_KEY_Menu) ||
		    ((keyval == GDK_KEY_F10) &&
		     (state & modifiers) == GDK_SHIFT_MASK))
		{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
			if (selection == NULL)
				return GDK_EVENT_PROPAGATE;

			_gtk_popover_popup_at_selected (GTK_POPOVER (private->file_popup_menu), GTK_TREE_VIEW (private->list_view));
			retval = GDK_EVENT_STOP;
		}
		break;

	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
		if (alt_pressed) {
			fr_window_go_up_one_level (window);
			retval = GDK_EVENT_STOP;
		}
		break;

	case GDK_KEY_BackSpace:
		fr_window_go_up_one_level (window);
		retval = GDK_EVENT_STOP;
		break;

	case GDK_KEY_Right:
	case GDK_KEY_KP_Right:
		if (alt_pressed) {
			fr_window_go_forward (window);
			retval = GDK_EVENT_STOP;
		}
		break;

	case GDK_KEY_Left:
	case GDK_KEY_KP_Left:
		if (alt_pressed) {
			fr_window_go_back (window);
			retval = GDK_EVENT_STOP;
		}
		break;

	case GDK_KEY_Home:
	case GDK_KEY_KP_Home:
		if (alt_pressed) {
			fr_window_go_to_location (window, "/", FALSE);
			retval = GDK_EVENT_STOP;
		}
		break;

	case GDK_KEY_Delete:
		if (! gtk_widget_has_focus (private->filter_entry)) {
			fr_window_activate_delete (NULL, NULL, window);
			retval = GDK_EVENT_STOP;
		}
		break;

	default:
		break;
	}

	return retval;
}


static void
dir_tree_selection_changed_cb (GtkTreeSelection *selection,
			       gpointer          user_data)
{
	FrWindow    *window = user_data;
	GtkTreeIter  iter;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		FrWindowPrivate *private = fr_window_get_instance_private (window);
		char *path;

		gtk_tree_model_get (GTK_TREE_MODEL (private->tree_store),
				    &iter,
				    TREE_COLUMN_PATH, &path,
				    -1);
		fr_window_go_to_location (window, path, FALSE);
		g_free (path);
	}

	return;
}


static void
selection_changed_cb (GtkTreeSelection *selection,
		      gpointer          user_data)
{
	FrWindow *window = user_data;

	fr_window_update_sensitivity (window);

	return;
}


static gboolean
is_single_click_policy (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char     *value;
	gboolean  result;

	if (private->settings_nautilus == NULL)
		return FALSE;

	value = g_settings_get_string (private->settings_nautilus, NAUTILUS_CLICK_POLICY);
	result = (value != NULL) && (strncmp (value, "single", 6) == 0);
	g_free (value);

	return result;
}


static void
filename_cell_data_func (GtkTreeViewColumn *column,
			 GtkCellRenderer   *renderer,
			 GtkTreeModel      *model,
			 GtkTreeIter       *iter,
			 FrWindow          *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char           *text;
	GtkTreePath    *path;
	PangoUnderline  underline;

	gtk_tree_model_get (model, iter,
			    COLUMN_NAME, &text,
			    -1);

	if (private->single_click) {
		path = gtk_tree_model_get_path (model, iter);

		if ((private->list_hover_path == NULL)
		    || gtk_tree_path_compare (path, private->list_hover_path))
			underline = PANGO_UNDERLINE_NONE;
		else
			underline = PANGO_UNDERLINE_SINGLE;

		gtk_tree_path_free (path);
	}
	else
		underline = PANGO_UNDERLINE_NONE;

	g_object_set (G_OBJECT (renderer),
		      "text", text,
		      "underline", underline,
		      NULL);

	g_free (text);
}


static void
add_dir_tree_columns (FrWindow    *window,
		      GtkTreeView *treeview)
{
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	GValue             value = { 0, };

	/* First column. */

	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, _("Folders"));

	/* icon */

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "gicon", TREE_COLUMN_ICON,
					     NULL);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 5);
	g_object_set_property (G_OBJECT (renderer), "xpad", &value);
	g_value_unset (&value);

	/* name */

	renderer = gtk_cell_renderer_text_new ();

	g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
	g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
	g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 5);
	g_object_set_property (G_OBJECT (renderer), "ypad", &value);
	g_value_unset (&value);

	gtk_tree_view_column_pack_start (column,
					 renderer,
					 TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "text", TREE_COLUMN_NAME,
					     "weight", TREE_COLUMN_WEIGHT,
					     NULL);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, TREE_COLUMN_NAME);

	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
}


static void
add_file_list_columns (FrWindow    *window,
		       GtkTreeView *treeview)
{
	FrWindowPrivate   *private = fr_window_get_instance_private (window);
	static char       *titles[] = {
		NC_("File", "Size"),
		NC_("File", "Type"),
		NC_("File", "Modified"),
		NC_("File", "Location")
	};
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	GValue             value = { 0, };

	/* First column. */

	private->filename_column = column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, C_("File", "Name"));
	gtk_tree_view_column_set_resizable (column, TRUE);

	/* emblem */

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_end (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "gicon", COLUMN_EMBLEM,
					     NULL);

	/* icon */

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "gicon", COLUMN_ICON,
					     NULL);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 5);
	g_object_set_property (G_OBJECT (renderer), "xpad", &value);
	g_value_unset (&value);

	/* name */

	private->single_click = FALSE;

	renderer = gtk_cell_renderer_text_new ();

	g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
	g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
	g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 5);
	g_object_set_property (G_OBJECT (renderer), "ypad", &value);
	g_value_unset (&value);

	gtk_tree_view_column_pack_start (column,
					 renderer,
					 TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "text", COLUMN_NAME,
					     NULL);

	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, FR_WINDOW_SORT_BY_NAME);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) filename_cell_data_func,
						 window, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Other columns */

	for (guint j = 0, i = COLUMN_SIZE; i < NUMBER_OF_COLUMNS; i++, j++) {
		renderer = gtk_cell_renderer_text_new ();
		column = gtk_tree_view_column_new_with_attributes (g_dpgettext2 (NULL, "File", titles[j]),
								   renderer,
								   "text", i,
								   NULL);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_resizable (column, TRUE);
		if (i == COLUMN_SIZE)
			gtk_tree_view_column_set_fixed_width (column, SIZE_COLUMN_WIDTH);
		else if (i == COLUMN_TIME)
			gtk_tree_view_column_set_fixed_width (column, TIME_COLUMN_WIDTH);
		else
			gtk_tree_view_column_set_fixed_width (column, OTHER_COLUMN_WIDTH);
		if (i == COLUMN_PATH)
			gtk_tree_view_column_set_expand (column, TRUE);
		gtk_tree_view_column_set_sort_column_id (column, FR_WINDOW_SORT_BY_NAME + 1 + j);

		g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
		g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
		g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
		g_value_unset (&value);

		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, 5);
		g_object_set_property (G_OBJECT (renderer), "ypad", &value);
		g_value_unset (&value);

		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, 5);
		g_object_set_property (G_OBJECT (renderer), "xpad", &value);
		g_value_unset (&value);

		gtk_tree_view_append_column (treeview, column);
	}
}


static int
name_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	FrFileData *fdata1;
	FrFileData *fdata2;
	GtkSortType  sort_order;
	int          result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
	gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);

	if (fr_file_data_is_dir (fdata1) == fr_file_data_is_dir (fdata2)) {
		result = strcmp (fdata1->sort_key, fdata2->sort_key);
	}
	else {
		result = fr_file_data_is_dir (fdata1) ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	return result;
}


static int
size_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	FrFileData *fdata1;
	FrFileData *fdata2;
	GtkSortType  sort_order;
	int          result;
	goffset      size_difference;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
	gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);

	if (fr_file_data_is_dir (fdata1) == fr_file_data_is_dir (fdata2)) {
		if (fr_file_data_is_dir (fdata1))
			size_difference = fdata1->dir_size - fdata2->dir_size;
		else
			size_difference = fdata1->size - fdata2->size;
		result = (size_difference > 0) - (size_difference < 0);
	}
	else {
		result = fr_file_data_is_dir (fdata1) ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	return result;
}


static int
type_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	FrFileData *fdata1;
	FrFileData *fdata2;
	GtkSortType  sort_order;
	int          result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
	gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);

	if (fr_file_data_is_dir (fdata1) == fr_file_data_is_dir (fdata2)) {
		if (fr_file_data_is_dir (fdata1)) {
			result = strcmp (fdata1->sort_key, fdata2->sort_key);
			if (sort_order == GTK_SORT_DESCENDING)
				result = -1 * result;
		}
		else {
			const char  *desc1, *desc2;

			desc1 = g_content_type_get_description (fdata1->content_type);
			desc2 = g_content_type_get_description (fdata2->content_type);
			result = strcasecmp (desc1, desc2);
			if (result == 0)
				result = strcmp (fdata1->sort_key, fdata2->sort_key);
		}
	}
	else {
		result = fr_file_data_is_dir (fdata1) ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	return result;
}


static int
time_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	FrFileData *fdata1;
	FrFileData *fdata2;
	GtkSortType  sort_order;
	int          result;

	gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model), NULL, &sort_order);

	gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, -1);
	gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, -1);

	if (fr_file_data_is_dir (fdata1) == fr_file_data_is_dir (fdata2)) {
		if (fr_file_data_is_dir (fdata1)) {
			result = strcmp (fdata1->sort_key, fdata2->sort_key);
			if (sort_order == GTK_SORT_DESCENDING)
				result = -1 * result;
		}
		else
			result = fdata1->modified - fdata2->modified;
	}
	else {
		result = fr_file_data_is_dir (fdata1) ? -1 : 1;
		if (sort_order == GTK_SORT_DESCENDING)
			result = -1 * result;
	}

	return result;
}


static int
path_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	FrFileData *fdata1;
	FrFileData *fdata2;
	char     *path1;
	char     *path2;
	int       result;

	gtk_tree_model_get (model, a, COLUMN_FILE_DATA, &fdata1, COLUMN_PATH, &path1, -1);
	gtk_tree_model_get (model, b, COLUMN_FILE_DATA, &fdata2, COLUMN_PATH, &path2, -1);

	result = strcmp (path1, path2);
	if (result == 0)
		result = strcmp (fdata1->sort_key, fdata2->sort_key);

	g_free (path1);
	g_free (path2);

	return result;
}


static gboolean
fr_window_close_request_cb (GtkWindow *window,
			    gpointer   user_data)
{
	fr_window_close (FR_WINDOW (window));
	return TRUE;
}


static void
fr_window_show_cb (GtkWidget *widget,
		   FrWindow  *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	fr_window_update_current_location (window);

	private->view_sidebar = g_settings_get_boolean (private->settings_ui, PREF_UI_VIEW_SIDEBAR);
	fr_window_set_action_state (window, "view-sidebar", private->view_sidebar);
}


/* preferences changes notification callbacks */


static void
pref_view_folders_changed (GSettings  *settings,
			   const char *key,
			   gpointer    user_data)
{
	FrWindow *window = user_data;
	fr_window_set_folders_visibility (window, g_settings_get_boolean (settings, key));
}


static void
pref_show_field_changed (GSettings  *settings,
			 const char *key,
			 gpointer    user_data)
{
	FrWindow *window = user_data;
	fr_window_update_columns_visibility (window);
}


static void
pref_list_mode_changed (GSettings  *settings,
			const char *key,
			gpointer    user_data)
{
	FrWindow *window = user_data;
	fr_window_set_list_mode (window, g_settings_get_enum (settings, key));
}


static void
pref_click_policy_changed (GSettings  *settings,
			   const char *key,
			   gpointer    user_data)
{
	FrWindow        *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->single_click = is_single_click_policy (window);
	gtk_tree_view_set_activate_on_single_click (GTK_TREE_VIEW (private->list_view), private->single_click);
}


static void
fr_archive_stoppable_cb (FrArchive *archive,
			 gboolean   stoppable,
			 FrWindow  *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->stoppable = stoppable;
	if (private->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (private->progress_dialog),
						   GTK_RESPONSE_OK,
						   stoppable);
}


static void
fr_window_activate_filter (FrWindow *window)
{
	FrWindowPrivate   *private = fr_window_get_instance_private (window);
	GtkTreeView       *tree_view = GTK_TREE_VIEW (private->list_view);
	GtkTreeViewColumn *column;

	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (private->filter_bar), TRUE);
	private->list_mode = FR_WINDOW_LIST_MODE_FLAT;

	gtk_list_store_clear (private->list_store);

	column = gtk_tree_view_get_column (tree_view, 4);
	gtk_tree_view_column_set_visible (column, TRUE);

	fr_window_update_file_list (window, TRUE);
	fr_window_update_dir_tree (window);
	fr_window_update_current_location (window);
}


static void
filter_entry_search_changed_cb (GtkSearchEntry *entry,
				FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->filter_mode)
		fr_window_activate_filter (window);
}


static void
filter_entry_stop_search_cb (GtkSearchEntry *entry,
			     FrWindow *window)
{
	fr_window_deactivate_filter (window);
}


static void
fr_window_attach (FrWindow      *window,
		  GtkWidget     *child,
		  FrWindowArea   area)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	int position;

	g_return_if_fail (window != NULL);
	g_return_if_fail (FR_IS_WINDOW (window));
	g_return_if_fail (child != NULL);
	g_return_if_fail (GTK_IS_WIDGET (child));

	switch (area) {
	case FR_WINDOW_AREA_MENUBAR:
		position = 0;
		break;
	case FR_WINDOW_AREA_TOOLBAR:
		position = 1;
		break;
	case FR_WINDOW_AREA_LOCATIONBAR:
		position = 2;
		break;
	case FR_WINDOW_AREA_FILTERBAR:
		position = 3;
		break;
	case FR_WINDOW_AREA_CONTENTS:
		position = 4;
		gtk_widget_set_vexpand (child, TRUE);
		break;
	case FR_WINDOW_AREA_STATUSBAR:
		position = 5;
		break;
	default:
		g_critical ("%s: area not recognized!", G_STRFUNC);
		return;
		break;
	}

	gtk_widget_set_hexpand (child, TRUE);
	gtk_grid_attach (GTK_GRID (private->layout),
			 child,
			 0, position,
			 1, 1);
}


/* -- fr_window_add_accelerators -- */


static void
fr_window_add_accelerators (FrWindow                 *window,
			    const FrAccelerator      *accelerators,
			    int                       n_accelerators)
{
	int            i;

	for (i = 0; i < n_accelerators; i++) {
		const FrAccelerator *acc = accelerators + i;

		_gtk_add_accelerator_for_action (acc->action_name,
						 acc->accelerator,
						 NULL);
	}
}


static void
filter_bar_search_mode_enabled_cb (GObject    *gobject,
				   GParamSpec *pspec,
				   gpointer    user_data)
{
	FrWindow *window = FR_WINDOW (user_data);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	fr_window_find (window, gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (private->filter_bar)));
}


static void
fr_window_construct (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GtkWidget          *list_scrolled_window;
	GtkWidget          *location_box;
	GtkWidget          *tree_scrolled_window;
	GtkWidget          *button;
	GtkWidget          *content;
	GtkTreeSelection   *selection;
	GtkSizeGroup       *header_bar_size_group;

#ifdef DEVELOPMENT_VERSION
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), "devel");
#endif

	/* Create the settings objects */

	private->settings_listing = g_settings_new (FILE_ROLLER_SCHEMA_LISTING);
	private->settings_ui = g_settings_new (FILE_ROLLER_SCHEMA_UI);
	private->settings_general = g_settings_new (FILE_ROLLER_SCHEMA_GENERAL);
	private->settings_dialogs = g_settings_new (FILE_ROLLER_SCHEMA_DIALOGS);
	private->settings_nautilus = _g_settings_new_if_schema_installed (NAUTILUS_SCHEMA);

	/* Create the application. */

	private->layout = gtk_grid_new ();
	gtk_window_set_child (GTK_WINDOW (window), private->layout);

	gtk_window_set_title (GTK_WINDOW (window), _("File Roller"));

	g_signal_connect (G_OBJECT (window),
			  "close-request",
			  G_CALLBACK (fr_window_close_request_cb),
			  window);
	g_signal_connect (window,
			  "show",
			  G_CALLBACK (fr_window_show_cb),
			  window);

	gtk_window_set_default_size (GTK_WINDOW (window),
				     g_settings_get_int (private->settings_ui, PREF_UI_WINDOW_WIDTH),
				     g_settings_get_int (private->settings_ui, PREF_UI_WINDOW_HEIGHT));

	GtkEventController *key_controller = gtk_event_controller_key_new ();
	g_signal_connect (key_controller,
			  "key-pressed",
			  G_CALLBACK (key_press_cb),
			  window);
	gtk_widget_add_controller (GTK_WIDGET (window), key_controller);

	/* Initialize Data. */

	private->list_mode = private->last_list_mode = g_settings_get_enum (private->settings_listing, PREF_LISTING_LIST_MODE);
	g_settings_set_boolean (private->settings_listing, PREF_LISTING_SHOW_PATH, (private->list_mode == FR_WINDOW_LIST_MODE_FLAT));

	private->view_sidebar = g_settings_get_boolean (private->settings_ui, PREF_UI_VIEW_SIDEBAR);

	private->history = NULL;
	private->history_current = NULL;

	private->action = FR_ACTION_NONE;

	private->open_default_dir = g_object_ref (_g_file_get_home ());
	private->add_default_dir = NULL;
	private->extract_default_dir = g_object_ref (_g_file_get_home ());

	private->give_focus_to_the_list = FALSE;

	private->activity_ref = 0;
	private->activity_timeout_handle = 0;

	private->archive_present = FALSE;
	private->archive_new = FALSE;
	private->reload_archive = FALSE;
	private->archive_file = NULL;

	private->drag_destination_folder = NULL;
	private->drag_base_dir = NULL;
	private->drag_error = NULL;
	private->drag_file_list = NULL;

	private->batch_mode = FALSE;
	private->batch_action_list = NULL;
	private->batch_action = NULL;

	private->password = NULL;
	private->compression = g_settings_get_enum (private->settings_general, PREF_GENERAL_COMPRESSION_LEVEL);
	private->encrypt_header = g_settings_get_boolean (private->settings_general, PREF_GENERAL_ENCRYPT_HEADER);
	private->volume_size = 0;

	private->stoppable = TRUE;

	private->batch_adding_one_file = FALSE;

	private->current_view_length = 0;

	private->current_action.type = FR_BATCH_ACTION_NONE;
	private->current_action.data = NULL;
	private->current_action.free_func = NULL;

	private->pd_last_archive = NULL;
	private->pd_last_message = NULL;
	private->pd_last_fraction = 0.0;

	/* Create the widgets. */

	/* * File list. */

	private->list_store = gtk_list_store_new (NUMBER_OF_COLUMNS,
						  G_TYPE_POINTER,
						  G_TYPE_ICON,
						  G_TYPE_STRING,
						  G_TYPE_ICON,
						  G_TYPE_STRING,
						  G_TYPE_STRING,
						  G_TYPE_STRING,
						  G_TYPE_STRING);
	g_object_set_data (G_OBJECT (private->list_store), "FrWindow", window);
	private->list_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (private->list_store));

	add_file_list_columns (window, GTK_TREE_VIEW (private->list_view));
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (private->list_view), TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (private->list_view), COLUMN_NAME);
	gtk_tree_view_set_activate_on_single_click (GTK_TREE_VIEW (private->list_view), private->single_click);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (private->list_store),
					 FR_WINDOW_SORT_BY_NAME, name_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (private->list_store),
					 FR_WINDOW_SORT_BY_SIZE, size_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (private->list_store),
					 FR_WINDOW_SORT_BY_TYPE, type_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (private->list_store),
					 FR_WINDOW_SORT_BY_TIME, time_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (private->list_store),
					 FR_WINDOW_SORT_BY_PATH, path_column_sort_func,
					 NULL, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (selection_changed_cb),
			  window);
	g_signal_connect (GTK_TREE_VIEW (private->list_view),
			  "row-activated",
			  G_CALLBACK (row_activated_cb),
			  window);

	GtkGesture *gesture_click = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture_click), 0);
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture_click), GTK_PHASE_CAPTURE);
	g_signal_connect (gesture_click,
			  "pressed",
			  G_CALLBACK (list_view_button_pressed_cb),
			  window);
	gtk_widget_add_controller (GTK_WIDGET (private->list_view), GTK_EVENT_CONTROLLER (gesture_click));

	/*g_signal_connect (GTK_TREE_VIEW (private->list_view),
			  "motion_notify_event",
			  G_CALLBACK (file_motion_notify_callback),
			  window);
	g_signal_connect (GTK_TREE_VIEW (private->list_view),
			  "leave_notify_event",
			  G_CALLBACK (file_leave_notify_callback),
			  window);*/

	list_scrolled_window = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (list_scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (list_scrolled_window), private->list_view);

	/* filter bar */

	private->filter_bar = gtk_search_bar_new ();
	gtk_search_bar_set_show_close_button (GTK_SEARCH_BAR (private->filter_bar), FALSE);
	//gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (private->filter_bar)), "locationbar");
	g_signal_connect (private->filter_bar,
			  "notify::search-mode-enabled",
			  G_CALLBACK (filter_bar_search_mode_enabled_cb),
			  window);

	private->filter_entry = gtk_search_entry_new ();
	gtk_widget_set_hexpand (private->filter_entry, TRUE);
	GtkWidget *filter_entry_limiter = adw_clamp_new ();
	adw_clamp_set_child (ADW_CLAMP (filter_entry_limiter), private->filter_entry);
	g_signal_connect (GTK_SEARCH_ENTRY (private->filter_entry),
			  "search-changed",
			  G_CALLBACK (filter_entry_search_changed_cb),
			  window);
	g_signal_connect (G_OBJECT (private->filter_entry),
			  "stop-search",
			  G_CALLBACK (filter_entry_stop_search_cb),
			  window);
	gtk_search_bar_set_child (GTK_SEARCH_BAR (private->filter_bar), filter_entry_limiter);
	gtk_search_bar_connect_entry (GTK_SEARCH_BAR (private->filter_bar), GTK_EDITABLE (private->filter_entry));
	fr_window_attach (FR_WINDOW (window), private->filter_bar, FR_WINDOW_AREA_FILTERBAR);

	/* tree view */

	private->tree_store = gtk_tree_store_new (TREE_NUMBER_OF_COLUMNS,
						       G_TYPE_STRING,
						       G_TYPE_ICON,
						       G_TYPE_STRING,
						       PANGO_TYPE_WEIGHT);
	private->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (private->tree_store));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (private->tree_view), FALSE);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (private->tree_view), TRUE);
	add_dir_tree_columns (window, GTK_TREE_VIEW (private->tree_view));

	gesture_click = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture_click), 0);
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture_click), GTK_PHASE_CAPTURE);
	g_signal_connect (gesture_click,
			  "pressed",
			  G_CALLBACK (dir_tree_button_pressed_cb),
			  window);
	gtk_widget_add_controller (GTK_WIDGET (private->tree_view), GTK_EVENT_CONTROLLER (gesture_click));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->tree_view));
	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (dir_tree_selection_changed_cb),
			  window);

	/*g_signal_connect (GTK_TREE_VIEW (private->tree_view),
			  "drag_begin",
			  G_CALLBACK (folde_tree_drag_begin),
			  window);
	g_signal_connect (GTK_TREE_VIEW (private->tree_view),
			  "drag_end",
			  G_CALLBACK (file_list_drag_end),
			  window);
	g_signal_connect (GTK_TREE_VIEW (private->tree_view),
			  "drag_data_get",
			  G_CALLBACK (fr_window_folder_tree_drag_data_get),
			  window);*/
	/*gtk_drag_source_set (private->tree_view,
			     GDK_BUTTON1_MASK,
			     folder_tree_targets, G_N_ELEMENTS (folder_tree_targets),
			     GDK_ACTION_COPY);*/

	tree_scrolled_window = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tree_scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (tree_scrolled_window), private->tree_view);

	/* side pane */

	private->sidepane = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	_gtk_box_pack_start (GTK_BOX (private->sidepane), tree_scrolled_window, TRUE, TRUE);

	GtkWidget *listpane = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	_gtk_box_pack_start (GTK_BOX (listpane), list_scrolled_window, TRUE, TRUE);

	/* main content */

	GtkWidget *status_page = adw_status_page_new ();
	adw_status_page_set_icon_name (ADW_STATUS_PAGE (status_page), "org.gnome.FileRoller");
	adw_status_page_set_title (ADW_STATUS_PAGE (status_page), _("No Archive Opened"));
	adw_status_page_set_description (ADW_STATUS_PAGE (status_page), _("Open an archive or create a new one."));
	GtkWidget *status_buttons = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
	gtk_widget_set_halign (status_buttons, GTK_ALIGN_CENTER);

	GtkWidget *status_button;
	status_button = gtk_button_new_with_mnemonic (C_("Action", "Open"));
	gtk_widget_add_css_class (status_button, "pill");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (status_button), "app.open");
	gtk_box_append (GTK_BOX (status_buttons), status_button);

	status_button = gtk_button_new_with_mnemonic (_("New Archive"));
	gtk_widget_add_css_class (status_button, "pill");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (status_button), "app.new");
	gtk_box_append (GTK_BOX (status_buttons), status_button);

	adw_status_page_set_child (ADW_STATUS_PAGE (status_page), status_buttons);

	private->paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child (GTK_PANED (private->paned), private->sidepane);
	gtk_paned_set_end_child (GTK_PANED (private->paned), listpane);
	gtk_paned_set_position (GTK_PANED (private->paned), g_settings_get_int (private->settings_ui, PREF_UI_SIDEBAR_WIDTH));

	GtkWidget *empty_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	private->content_stack = gtk_stack_new ();
	gtk_stack_add_named (GTK_STACK (private->content_stack), status_page, EMPTY_STATUS);
	gtk_stack_add_named (GTK_STACK (private->content_stack), private->paned, ARCHIVE_CONTENT);
	gtk_stack_add_named (GTK_STACK (private->content_stack), empty_page, LOADING_ARCHIVE);
	gtk_stack_set_visible_child_name (GTK_STACK (private->content_stack), EMPTY_STATUS);
	fr_window_attach (FR_WINDOW (window), private->content_stack, FR_WINDOW_AREA_CONTENTS);

	/* Drop target */

	GtkDropTarget *target = gtk_drop_target_new (G_TYPE_INVALID, GDK_ACTION_COPY);
	gtk_drop_target_set_gtypes (target, (GType[3]) { GDK_TYPE_FILE_LIST, G_TYPE_FILE, }, 2);
	g_signal_connect (target, "drop", G_CALLBACK (fr_window_on_drop), window);
	gtk_widget_add_controller (GTK_WIDGET (window), GTK_EVENT_CONTROLLER (target));

	/* ui actions */

	g_action_map_add_action_entries (G_ACTION_MAP (window),
					 fr_window_actions,
					 G_N_ELEMENTS (fr_window_actions),
					 window);
	fr_window_add_accelerators (window,
				    fr_window_accelerators,
				    G_N_ELEMENTS (fr_window_accelerators));

	/* header bar */

	private->headerbar = gtk_header_bar_new ();
	gtk_header_bar_set_show_title_buttons (GTK_HEADER_BAR (private->headerbar), TRUE);
	gtk_window_set_titlebar (GTK_WINDOW (window), private->headerbar);

	/* header bar buttons */

	header_bar_size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

	button = gtk_button_new ();
	content = adw_button_content_new();
	adw_button_content_set_label (ADW_BUTTON_CONTENT (content), C_("Action", "_Extract…"));
	adw_button_content_set_icon_name (ADW_BUTTON_CONTENT (content), "archive-extract-symbolic");
	adw_button_content_set_use_underline (ADW_BUTTON_CONTENT (content), true);
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.extract-all-by-default");
	gtk_button_set_child (GTK_BUTTON (button), content);
	gtk_widget_set_tooltip_text (button, _("Extract Files"));
	gtk_size_group_add_widget (header_bar_size_group, button);
	gtk_header_bar_pack_start (GTK_HEADER_BAR (private->headerbar), button);

	button = _gtk_header_bar_create_image_button ("list-add-symbolic", C_("Action", "Add Files"), "win.add-files");
	gtk_size_group_add_widget (header_bar_size_group, button);
	gtk_header_bar_pack_start (GTK_HEADER_BAR (private->headerbar), button);

	/* gears menu button */

	{
		GtkBuilder *builder;
		GMenuModel *menu;

		builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "gears-menu.ui");
		menu = G_MENU_MODEL (gtk_builder_get_object (builder, "menu"));
		button = _gtk_menu_button_new_for_header_bar ();
		gtk_size_group_add_widget (header_bar_size_group, button);
		gtk_menu_button_set_direction (GTK_MENU_BUTTON (button), GTK_ARROW_NONE);
		gtk_menu_button_set_primary (GTK_MENU_BUTTON (button), TRUE);
		gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), menu);
		gtk_widget_set_tooltip_text (button, _("Main Menu"));
		gtk_header_bar_pack_end (GTK_HEADER_BAR (private->headerbar), button);

		_gtk_add_accelerators_from_menu (menu);

		g_object_unref (builder);
	}

	button = _gtk_header_bar_create_image_toggle_button ("edit-find-symbolic", _("Search"), "win.find");
	gtk_size_group_add_widget (header_bar_size_group, button);
	gtk_header_bar_pack_end (GTK_HEADER_BAR (private->headerbar), button);

        g_object_unref (header_bar_size_group);

	/* location bar */

	private->location_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (private->location_bar)), "toolbar");
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (private->location_bar)), "locationbar");

	/* Navigation commands. */

	gtk_box_append (GTK_BOX (private->location_bar),
			_gtk_header_bar_create_image_button ("go-previous-symbolic", _("Back"), "win.go-back"));
	gtk_box_append (GTK_BOX (private->location_bar),
			_gtk_header_bar_create_image_button ("go-next-symbolic", _("Forward"), "win.go-forward"));
	gtk_box_append (GTK_BOX (private->location_bar),
			_gtk_header_bar_create_image_button ("go-up-symbolic", _("Up"), "win.go-up-one-level"));
	gtk_box_append (GTK_BOX (private->location_bar),
			_gtk_header_bar_create_image_button ("user-home-symbolic", _("Home"), "win.go-home"));

	/* Current location */

	location_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	/* Translators: after the colon there is a folder name. */
	private->location_label = gtk_label_new_with_mnemonic (_("_Location:"));
	gtk_widget_set_margin_start (private->location_label, 5);
	gtk_widget_set_margin_end (private->location_label, 5);
	gtk_box_append (GTK_BOX (location_box), private->location_label);

	private->location_entry = gtk_entry_new ();
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (private->location_entry),
					   GTK_ENTRY_ICON_PRIMARY,
					   "folder-symbolic");
	g_signal_connect (GTK_ENTRY (private->location_entry),
			  "activate",
			  G_CALLBACK (location_entry_activate_cb),
			  window);
	gtk_widget_set_margin_start (private->location_entry, 5);
	gtk_widget_set_margin_end (private->location_entry, 5);
	_gtk_box_pack_end (GTK_BOX (location_box), private->location_entry, TRUE, FALSE);
	_gtk_box_pack_end (GTK_BOX (private->location_bar), location_box, TRUE, FALSE);

	fr_window_attach (FR_WINDOW (window), private->location_bar, FR_WINDOW_AREA_LOCATIONBAR);
	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT)
		gtk_widget_hide (private->location_bar);

	/* popup menus */
	{
		GtkBuilder *builder;

		builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "menus.ui");

		private->file_popup_menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (builder, "file-popup")));
		private->folder_popup_menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (builder, "folder-popup")));
		private->sidebar_folder_popup_menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (builder, "sidebar-popup")));

		gtk_box_prepend (GTK_BOX (listpane), private->file_popup_menu);
		gtk_box_prepend (GTK_BOX (listpane), private->folder_popup_menu);
		gtk_box_prepend (GTK_BOX (private->sidepane), private->sidebar_folder_popup_menu);

		g_object_unref (builder);
	}

	/**/

	fr_window_update_title (window);
	fr_window_update_sensitivity (window);
	fr_window_update_current_location (window);
	fr_window_update_columns_visibility (window);
	fr_window_update_stack_content (window);

	/* Add notification callbacks. */

	g_signal_connect (private->settings_ui,
			  "changed::" PREF_UI_VIEW_SIDEBAR,
			  G_CALLBACK (pref_view_folders_changed),
			  window);
	g_signal_connect (private->settings_listing,
			  "changed::" PREF_LISTING_SHOW_TYPE,
			  G_CALLBACK (pref_show_field_changed),
			  window);
	g_signal_connect (private->settings_listing,
			  "changed::" PREF_LISTING_SHOW_SIZE,
			  G_CALLBACK (pref_show_field_changed),
			  window);
	g_signal_connect (private->settings_listing,
			  "changed::" PREF_LISTING_SHOW_TIME,
			  G_CALLBACK (pref_show_field_changed),
			  window);
	g_signal_connect (private->settings_listing,
			  "changed::" PREF_LISTING_SHOW_PATH,
			  G_CALLBACK (pref_show_field_changed),
			  window);
	g_signal_connect (private->settings_listing,
			  "changed::" PREF_LISTING_LIST_MODE,
			  G_CALLBACK (pref_list_mode_changed),
			  window);

	if (private->settings_nautilus)
		g_signal_connect (private->settings_nautilus,
				  "changed::" NAUTILUS_CLICK_POLICY,
				  G_CALLBACK (pref_click_policy_changed),
				  window);

	/* Give focus to the list. */

	gtk_widget_grab_focus (private->list_view);
}


GtkWidget *
fr_window_new (void)
{
	GtkWidget *window;

	window = g_object_new (FR_TYPE_WINDOW, "application", g_application_get_default (), NULL);
	fr_window_construct ((FrWindow*) window);

	return window;
}


static void
_fr_window_set_archive (FrWindow  *window,
			FrArchive *archive)
{
	if (window->archive != NULL) {
		g_signal_handlers_disconnect_by_data (window->archive, window);
		g_object_unref (window->archive);
	}

	window->archive = _g_object_ref (archive);

	if (window->archive == NULL)
		return;

	g_signal_connect (window->archive,
			  "progress",
			  G_CALLBACK (fr_archive_progress_cb),
			  window);
	g_signal_connect (window->archive,
			  "message",
			  G_CALLBACK (fr_archive_message_cb),
			  window);
	g_signal_connect (window->archive,
			  "start",
			  G_CALLBACK (fr_archive_start_cb),
			  window);
	g_signal_connect (window->archive,
			  "stoppable",
			  G_CALLBACK (fr_archive_stoppable_cb),
			  window);
	g_signal_connect (window->archive,
			  "working-archive",
			  G_CALLBACK (fr_window_working_archive_cb),
			  window);
}


static void
_fr_window_set_archive_file (FrWindow *window,
			     GFile    *file)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_g_object_unref (private->archive_file);
	private->archive_file = _g_object_ref (file);
}


gboolean
fr_window_archive_new (FrWindow   *window,
		       GFile      *file,
		       const char *mime_type)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrArchive *archive;

	g_return_val_if_fail (window != NULL, FALSE);

	archive = fr_archive_create (file, mime_type);
	if (archive != NULL) {
		fr_window_archive_close (window);
		_fr_window_set_archive (window, archive);
		_fr_window_set_archive_file (window, file);
		private->archive_present = TRUE;
		private->archive_new = TRUE;

		g_object_unref (archive);
	}

	return archive != NULL;
}


static void
archive_list_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_LISTING_CONTENT, error);

	_g_error_free (error);
}


static void
fr_window_archive_list (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (! fr_archive_is_capable_of (window->archive, FR_ARCHIVE_CAN_READ)) {
		fr_window_archive_close (window);
		fr_window_archive_open (window, private->archive_file, NULL);
		return;
	}

	_archive_operation_started (window, FR_ACTION_LISTING_CONTENT);
	fr_archive_list (window->archive,
			 private->password,
			 private->cancellable,
			 archive_list_ready_cb,
			 window);
}


static void
archive_open_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	FrWindow  *window = user_data;
	FrArchive *archive;
	GError    *error = NULL;

	archive = fr_archive_open_finish (G_FILE (source_object), result, &error);
	_fr_window_set_archive (window, archive);

	g_signal_emit (window,
		       fr_window_signals[ARCHIVE_LOADED],
		       0,
		       error == NULL);

	_archive_operation_completed (window, FR_ACTION_LOADING_ARCHIVE, error);
	_g_error_free (error);
	_g_object_unref (archive);
}


FrWindow *
fr_window_archive_open (FrWindow   *current_window,
			GFile      *file,
			GtkWindow  *parent)
{
	FrWindow *window = current_window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	g_return_val_if_fail (file != NULL, FALSE);

	if (private->archive_present)
		window = (FrWindow *) fr_window_new ();

	g_return_val_if_fail (window != NULL, FALSE);

	fr_window_archive_close (window);
	_fr_window_set_archive_file (window, file);
	private->give_focus_to_the_list = TRUE;
	private->load_error_parent_window = parent;

	_archive_operation_started (window, FR_ACTION_LOADING_ARCHIVE);

	/* this is used to reload the archive after asking a password */
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_LOAD,
					    g_object_ref (file),
					    (GFreeFunc) g_object_unref);

	fr_archive_open (file,
			 private->cancellable,
			 archive_open_ready_cb,
			 window);

	return window;
}


void
fr_window_archive_close (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (! private->archive_new && ! private->archive_present) {
		fr_window_update_stack_content (window);
		return;
	}

	fr_window_free_open_files (window);

	fr_window_set_password (window, NULL);
	fr_window_set_volume_size (window, 0);
	fr_window_history_clear (window);

	_fr_window_set_archive (window, NULL);
	private->archive_new = FALSE;
	private->archive_present = FALSE;

	fr_window_update_title (window);
	fr_window_update_sensitivity (window);
	fr_window_update_file_list (window, FALSE);
	fr_window_update_dir_tree (window);
	fr_window_update_current_location (window);
	fr_window_update_stack_content (window);
}


GFile *
fr_window_get_archive_file (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_val_if_fail (window != NULL, NULL);

	return private->archive_file;
}


GFile *
fr_window_get_archive_file_for_paste (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_val_if_fail (window != NULL, NULL);

	if (private->clipboard_data != NULL)
		return private->clipboard_data->file;
	else
		return NULL;
}


gboolean
fr_window_archive_is_present (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_val_if_fail (window != NULL, FALSE);

	return private->archive_present;
}


void
fr_window_archive_reload (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->activity_ref > 0)
		return;
	if (private->archive_new)
		return;
	if (window->archive == NULL)
		return;

	fr_window_archive_list (window);
}


static void
_fr_window_notify_creation_complete (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char                     *basename;
	char                     *message;
	g_autoptr(GNotification)  notification;

	basename = _g_file_get_display_name (private->saving_file);
	/* Translators: %s is a filename */
	message = g_strdup_printf (_("“%s” created successfully"), basename);
	notification = g_notification_new (private->batch_title);
	g_notification_set_body (notification, message);
	g_notification_set_icon (notification, g_themed_icon_new ("document-open-symbolic"));
	g_notification_add_button_with_target (notification,
					       C_("Action", "Open"),
					       "app.open-archive",
					       "s", g_file_get_path (private->saving_file));

	g_application_send_notification (g_application_get_default (), NULL,
					 notification);

	g_free (message);
	g_free (basename);
}


static void
archive_add_files_ready_cb (GObject      *source_object,
			    GAsyncResult *result,
			    gpointer      user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean  notify;
	GError   *error = NULL;

	notify = private->notify;

	g_object_ref (window);
	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_ADDING_FILES, error);

	if ((error == NULL) && notify) {
		private->destroy_with_confirmation_dialog = TRUE;
		fr_window_show_confirmation_dialog_with_open_archive (window);

		if (! gtk_widget_has_focus (private->progress_dialog))
			_fr_window_notify_creation_complete (window);
	}

	_g_error_free (error);
	g_object_unref (window);
}


void
fr_window_archive_add_files (FrWindow   *window,
			     GList      *file_list, /* GFile list */
			     GFile      *base_dir,
			     gboolean    update)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_archive_operation_started (window, FR_ACTION_ADDING_FILES);

	fr_archive_add_files (window->archive,
			      file_list,
			      base_dir,
			      fr_window_get_current_location (window),
			      update,
			      FALSE,
			      private->password,
			      private->encrypt_header,
			      private->compression,
			      private->volume_size,
			      private->cancellable,
			      archive_add_files_ready_cb,
			      window);
}


void
fr_window_archive_add_with_filter (FrWindow      *window,
				   GList         *file_list, /* GFile list */
				   GFile         *base_dir,
				   const char    *include_files,
				   const char    *exclude_files,
				   const char    *exclude_folders,
				   const char    *dest_dir,
				   gboolean       update,
				   gboolean       follow_links)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_archive_operation_started (window, FR_ACTION_ADDING_FILES);

	fr_archive_add_files_with_filter (window->archive,
					  file_list,
					  base_dir,
					  include_files,
					  exclude_files,
					  exclude_folders,
					  (dest_dir == NULL)? fr_window_get_current_location (window): dest_dir,
					  update,
					  follow_links,
					  private->password,
					  private->encrypt_header,
					  private->compression,
					  private->volume_size,
					  private->cancellable,
					  archive_add_files_ready_cb,
					  window);
}


void
fr_window_archive_add_dropped_items (FrWindow *window,
				     GList    *file_list)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_archive_operation_started (window, FR_ACTION_ADDING_FILES);

	fr_archive_add_dropped_items (window->archive,
				      file_list,
				      fr_window_get_current_location (window),
				      private->password,
				      private->encrypt_header,
				      private->compression,
				      private->volume_size,
				      private->cancellable,
				      archive_add_files_ready_cb,
				      window);
}


/* -- fr_window_archive_remove -- */


static void
archive_remove_ready_cb (GObject      *source_object,
			 GAsyncResult *result,
			 gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_DELETING_FILES, error);

	_g_error_free (error);
}


void
fr_window_archive_remove (FrWindow *window,
			  GList    *file_list)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *new_file_list;

	new_file_list = _g_string_list_dup (file_list);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_REMOVE,
					    new_file_list,
					    (GFreeFunc) _g_string_list_free);

	_archive_operation_started (window, FR_ACTION_DELETING_FILES);

	g_object_set (window->archive,
		      "compression", private->compression,
		      "encrypt-header", private->encrypt_header,
		      "password", private->password,
		      "volume-size", private->volume_size,
		      NULL);

	fr_window_clipboard_remove_file_list (window, file_list);
	fr_archive_remove (window->archive,
			   new_file_list,
			   private->compression,
			   private->cancellable,
			   archive_remove_ready_cb,
			   window);
}


/* -- fr_window_archive_extract -- */


static ExtractData *
extract_data_new (FrWindow    *window,
		  GList       *file_list,
		  GFile       *destination,
		  const char  *base_dir,
		  gboolean     skip_older,
		  FrOverwrite  overwrite,
		  gboolean     junk_paths,
		  gboolean     ask_to_open_destination,
		  gboolean     force_directory_creation)
{
	ExtractData *edata;

	edata = g_new0 (ExtractData, 1);
	edata->window = _g_object_ref (window);
	edata->file_list = _g_string_list_dup (file_list);
	edata->destination = _g_object_ref (destination);
	edata->skip_older = skip_older;
	edata->overwrite = overwrite;
	edata->junk_paths = junk_paths;
	if (base_dir != NULL)
		edata->base_dir = g_strdup (base_dir);
	edata->ask_to_open_destination = ask_to_open_destination;
	edata->force_directory_creation = force_directory_creation;

	return edata;
}


static void
extract_data_free (ExtractData *edata)
{
	g_return_if_fail (edata != NULL);

	_g_string_list_free (edata->file_list);
	_g_object_unref (edata->destination);
	_g_object_unref (edata->window);
	g_free (edata->base_dir);

	g_free (edata);
}


typedef struct {
	FrWindow    *window;
	ExtractData *edata;
	GList       *current_file;
	gboolean     extract_all;
} OverwriteData;


#define _FR_RESPONSE_OVERWRITE_YES_ALL 100
#define _FR_RESPONSE_OVERWRITE_YES     101
#define _FR_RESPONSE_OVERWRITE_NO      102
#define _FR_RESPONSE_OVERWRITE_NO_ALL  103


static void
archive_extraction_ready_cb (GObject      *source_object,
			     GAsyncResult *result,
			     gpointer      user_data)
{
	ExtractData *edata = user_data;
	FrWindow    *window = edata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean     ask_to_open_destination;
	gboolean     batch_mode;
	GError      *error = NULL;

	batch_mode = private->batch_mode;
	ask_to_open_destination = edata->ask_to_open_destination;
	g_object_ref (window);

	_g_clear_object (&private->last_extraction_destination);
	private->last_extraction_destination = _g_object_ref (fr_archive_get_last_extraction_destination (window->archive));

	_g_object_list_unref (private->last_extraction_files_first_level);
	private->last_extraction_files_first_level = NULL;

	if (ask_to_open_destination && ! edata->junk_paths) {
		/* collect the files to show in the file manager */

		GHashTable *names_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		for (GList *scan = edata->file_list; scan; scan = scan->next) {
			char *filename = scan->data;

			FrFileData *fdata = g_hash_table_lookup (window->archive->files_hash, filename);
			if (fdata == NULL)
				continue;

			if ((fdata->full_path == NULL) || (fdata->full_path[0] == 0))
				continue;

			const char *relative_path = fdata->full_path;
			if (edata->base_dir != NULL)
				relative_path += strlen (edata->base_dir);
			else
				relative_path += 1; /* skip first '/' */
			const char *second_slash = strchr (relative_path, '/');
			char *first_level;
			if (second_slash != NULL)
				first_level = g_strndup (relative_path, second_slash - relative_path);
			else
				first_level = g_strdup (relative_path);

			/* avoid to insert duplicated entries */

			if (g_hash_table_lookup (names_hash, first_level) == NULL) {

				/* only use this list if there is a single entry,
				 * because Nautilus opens a window for each file
				 * even when the files are all in the same folder.  */

				if (private->last_extraction_files_first_level != NULL) {
					_g_object_list_unref (private->last_extraction_files_first_level);
					private->last_extraction_files_first_level = NULL;
					break;
				}
				else {
					g_hash_table_insert (names_hash, g_strdup (first_level), GINT_TO_POINTER (1));
					private->last_extraction_files_first_level = g_list_prepend (private->last_extraction_files_first_level, _g_file_append_path (private->last_extraction_destination, first_level, NULL));
				}
			}

			g_free (first_level);
		}

		g_hash_table_destroy (names_hash);
	}

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_EXTRACTING_FILES, error);
	if ((error == NULL) || (error->code != FR_ERROR_ASK_PASSWORD))
		fr_window_dnd_extraction_finished (window, error != NULL);

	if ((error == NULL) && ask_to_open_destination) {
		private->destroy_with_confirmation_dialog = private->batch_mode;
		fr_window_show_confirmation_dialog_with_open_destination (window);
	}
	else if ((error == NULL) && ! batch_mode && ! gtk_widget_has_focus (private->progress_dialog))
		gtk_window_present (GTK_WINDOW (window));

	_g_error_free (error);
	g_object_unref (window);
}


static void
_fr_window_archive_extract_from_edata (FrWindow    *window,
				       ExtractData *edata)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *scan;
	gsize  total_size;

	total_size = 0;
	for (scan = edata->file_list; scan; scan = scan->next) {
		char     *filename = scan->data;
		FrFileData *file_data;

		file_data = g_hash_table_lookup (window->archive->files_hash, filename);
		if (file_data == NULL)
			continue;

		total_size += file_data->size;
	}
	fr_archive_progress_set_total_bytes (window->archive, total_size);

	_archive_operation_started (window, FR_ACTION_EXTRACTING_FILES);

	fr_archive_extract (window->archive,
			    edata->file_list,
			    edata->destination,
			    edata->base_dir,
			    edata->skip_older,
			    edata->overwrite == FR_OVERWRITE_YES,
			    edata->junk_paths,
			    private->password,
			    private->cancellable,
			    archive_extraction_ready_cb,
			    edata);
}


static void _fr_window_ask_overwrite_dialog (OverwriteData *odata);


static OverwriteData *
overwrite_data_new (FrWindow *window)
{
	OverwriteData *odata;

	odata = g_new0 (OverwriteData, 1);
	odata->window = _g_object_ref (window);
	odata->edata = NULL;
	odata->current_file = NULL;
	odata->extract_all = FALSE;

	return odata;
}


static void
overwrite_data_free (OverwriteData *odata)
{
	_g_object_unref (odata->window);
	g_free (odata);
}


/* remove the file from the list to extract */
static void
overwrite_data_skip_current (OverwriteData *odata)
{
	GList *next = odata->current_file->next;

	odata->edata->file_list = g_list_remove_link (odata->edata->file_list, odata->current_file);
	_g_string_list_free (odata->current_file);
	odata->current_file = next;
	odata->extract_all = FALSE;
}


static void
overwrite_dialog_response_cb (GtkDialog *dialog,
			      int        response_id,
			      gpointer   user_data)
{
	OverwriteData *odata = user_data;
	gboolean       do_not_extract = FALSE;

	switch (response_id) {
	case _FR_RESPONSE_OVERWRITE_YES_ALL:
		odata->edata->overwrite = FR_OVERWRITE_YES;
		break;

	case _FR_RESPONSE_OVERWRITE_YES:
		odata->current_file = odata->current_file->next;
		break;

	case _FR_RESPONSE_OVERWRITE_NO_ALL:
		overwrite_data_skip_current (odata);
		odata->edata->overwrite = FR_OVERWRITE_NO;
		break;

	case _FR_RESPONSE_OVERWRITE_NO:
		overwrite_data_skip_current (odata);
		break;

	case GTK_RESPONSE_DELETE_EVENT:
	case GTK_RESPONSE_CANCEL:
		do_not_extract = TRUE;
		break;

	default:
		break;
	}

	gtk_window_destroy (GTK_WINDOW (dialog));

	if (do_not_extract) {
		fr_window_batch_stop (odata->window);
		fr_window_dnd_extraction_finished (odata->window, FALSE);
		overwrite_data_free (odata);
		return;
	}

	_fr_window_ask_overwrite_dialog (odata);
}


static void
query_info_ready_for_overwrite_dialog_cb (GObject      *source_object,
					  GAsyncResult *result,
					  gpointer      user_data)
{
	OverwriteData *odata = user_data;
	GFile         *destination = G_FILE (source_object);
	GFileInfo     *info;
	GFileType      file_type;

	info = g_file_query_info_finish (destination, result, NULL);

	/* file does not exist -> keep in the files to extract. */

	if (info == NULL) {
		odata->current_file = odata->current_file->next;
		_fr_window_ask_overwrite_dialog (odata);
		g_object_unref (destination);
		return;
	}

	/* file exists and user selected "Overwrite Nothing" -> automatically
	 * skip the file. */

	if (odata->edata->overwrite == FR_OVERWRITE_NO) {
		overwrite_data_skip_current (odata);
		_fr_window_ask_overwrite_dialog (odata);
		g_object_unref (destination);
		return;
	}

	/* file exists and odata->edata->overwrite == FR_OVERWRITE_ASK */

	file_type = g_file_info_get_file_type (info);
	if ((file_type != G_FILE_TYPE_UNKNOWN) && (file_type != G_FILE_TYPE_DIRECTORY)) {
		char      *msg;
		GFile     *parent;
		char      *parent_name;
		char      *details;
		GtkWidget *d;

		msg = g_strdup_printf (_("Replace file “%s”?"), g_file_info_get_display_name (info));
		parent = g_file_get_parent (destination);
		parent_name = g_file_get_parse_name (parent);
		details = g_strdup_printf (_("Another file with the same name already exists in “%s”."), parent_name);
		d = _gtk_message_dialog_new (GTK_WINDOW (odata->window),
					     GTK_DIALOG_MODAL,
					     msg,
					     details,
					     _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Replace _All"), _FR_RESPONSE_OVERWRITE_YES_ALL,
					     _("Replace _Nothing"), _FR_RESPONSE_OVERWRITE_NO_ALL,
					     _("_Skip"), _FR_RESPONSE_OVERWRITE_NO,
					     _("_Replace"), _FR_RESPONSE_OVERWRITE_YES,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), _FR_RESPONSE_OVERWRITE_YES);
		g_signal_connect (GTK_MESSAGE_DIALOG (d),
				  "response",
				  G_CALLBACK (overwrite_dialog_response_cb),
				  odata);
		gtk_widget_show (d);

		g_free (msg);
		g_free (parent_name);
		g_free (details);
		g_object_unref (parent);
		g_object_unref (info);
		g_object_unref (destination);

		return;
	}

	g_object_unref (info);
	g_object_unref (destination);

	/* file exists and it's a directory or unknown -> skip */

	overwrite_data_skip_current (odata);
	_fr_window_ask_overwrite_dialog (odata);
}


static void
_fr_window_ask_overwrite_dialog (OverwriteData *odata)
{
	gboolean perform_extraction = TRUE;
	gboolean check_file_existence;

	check_file_existence = (odata->edata->overwrite == FR_OVERWRITE_ASK) || (odata->edata->overwrite == FR_OVERWRITE_NO);
	if (check_file_existence && (odata->current_file != NULL)) {
		const char *base_name;
		GFile      *destination;

		base_name = _g_path_get_relative_basename_safe ((char *) odata->current_file->data, odata->edata->base_dir, odata->edata->junk_paths);
		if (base_name != NULL) {
			FrWindowPrivate *win_private = fr_window_get_instance_private (odata->window);
			destination = g_file_get_child (odata->edata->destination, base_name);
			g_file_query_info_async (destination,
						 G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
						 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
						 G_PRIORITY_DEFAULT,
						 win_private->cancellable,
						 query_info_ready_for_overwrite_dialog_cb,
						 odata);

			return;
		}
		else {
			overwrite_data_skip_current (odata);
			_fr_window_ask_overwrite_dialog (odata);
			return;
		}
	}

	if (odata->edata->file_list == NULL)
		perform_extraction = FALSE;

	if (perform_extraction) {
		/* speed optimization: passing NULL when extracting all the
		 * files is faster if the command supports the
		 * propCanExtractAll property. */
		if (odata->extract_all) {
			_g_string_list_free (odata->edata->file_list);
			odata->edata->file_list = NULL;
		}
		odata->edata->overwrite = FR_OVERWRITE_YES;

		_fr_window_archive_extract_from_edata (odata->window, odata->edata);
	}
	else {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (odata->window),
					     0,
					     _("Extraction not performed"),
					     NULL,
					     _GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		fr_window_show_error_dialog (odata->window, d, GTK_WINDOW (odata->window), _("Extraction not performed"));

		fr_window_batch_stop (odata->window);
		fr_window_dnd_extraction_finished (odata->window, TRUE);
	}

	overwrite_data_free (odata);
}


static gboolean
archive_is_encrypted (FrWindow *window,
		      GList    *file_list)
{
	gboolean encrypted = FALSE;

	if (file_list == NULL) {
		for (guint i = 0; ! encrypted && i < window->archive->files->len; i++) {
			FrFileData *fdata = g_ptr_array_index (window->archive->files, i);

			if (fdata->encrypted)
				encrypted = TRUE;
		}
	}
	else {
		GList *scan;

		for (scan = file_list; ! encrypted && scan; scan = scan->next) {
			char     *filename = scan->data;
			FrFileData *fdata;

			fdata = g_hash_table_lookup (window->archive->files_hash, filename);
			g_return_val_if_fail (fdata != NULL, FALSE);

			if (fdata->encrypted)
				encrypted = TRUE;
		}
	}

	return encrypted;
}


static void _fr_window_archive_extract_from_edata_maybe (FrWindow    *window,
							 ExtractData *edata);


static void
confirm_create_folder_on_extract_dialog_response (GtkDialog   *dialog,
						  int          response_id,
						  ExtractData *edata)
{
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (response_id != GTK_RESPONSE_YES) {
		fr_window_batch_stop (edata->window);
		fr_window_dnd_extraction_finished (edata->window, TRUE);
	}
	else {
		edata->force_directory_creation = TRUE;

		/* Try again. */
		_fr_window_archive_extract_from_edata_maybe (edata->window, edata);
	}
}


/* Ask some questions to the user before calling _fr_window_archive_extract_from_edata */
static void
_fr_window_archive_extract_from_edata_maybe (FrWindow    *window,
					     ExtractData *edata)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean         do_not_extract = FALSE;
	GError          *error = NULL;

	if (archive_is_encrypted (window, edata->file_list) && (private->password == NULL)) {
		dlg_ask_password (window);
		return;
	}

	if (! _g_file_query_is_dir (edata->destination)) {
		/* There is nothing to ask if the destination doesn't exist. */
		if (edata->overwrite == FR_OVERWRITE_ASK)
			edata->overwrite = FR_OVERWRITE_YES;

		if (! ForceDirectoryCreation && ! edata->force_directory_creation) {
			char      *folder_name;
			char      *msg;
			GtkWidget *msg_dialog;

			folder_name = _g_file_get_display_basename (edata->destination);
			msg = g_strdup_printf (_("Destination folder “%s” does not exist.\n\nDo you want to create it?"), folder_name);
			msg_dialog = _gtk_message_dialog_new (
				GTK_WINDOW (window),
				GTK_DIALOG_MODAL,
				msg,
				NULL,
				_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				_("Create _Folder"), GTK_RESPONSE_YES,
				NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (msg_dialog), GTK_RESPONSE_YES);
			g_signal_connect (GTK_DIALOG (msg_dialog),
					  "response",
					  G_CALLBACK (confirm_create_folder_on_extract_dialog_response),
					  edata);
			gtk_window_present (GTK_WINDOW (msg_dialog));

			g_free (msg);
			g_free (folder_name);
			return;
		}

		if (! do_not_extract && ! _g_file_make_directory_tree (edata->destination, 0755, &error)) {
			char      *details;
			GtkWidget *msg_dialog;

			details = g_strdup_printf (_("Could not create the destination folder: %s."), error->message);
			msg_dialog = _gtk_error_dialog_new (
				GTK_WINDOW (window),
				0,
				NULL,
				_("Extraction not performed"),
				"%s",
				details);
			fr_window_show_error_dialog (window, msg_dialog, GTK_WINDOW (window), details);

			fr_window_batch_stop (window);
			fr_window_dnd_extraction_finished (window, TRUE);

			g_free (details);
			g_clear_error (&error);
			return;
		}
	}

	if (do_not_extract) {
		GtkWidget *msg_dialog = _gtk_message_dialog_new (
			GTK_WINDOW (window),
			0,
			_("Extraction not performed"),
			NULL,
			_GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
			NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (msg_dialog), GTK_RESPONSE_OK);
		fr_window_show_error_dialog (window, msg_dialog, GTK_WINDOW (window), _("Extraction not performed"));

		fr_window_batch_stop (window);
		fr_window_dnd_extraction_finished (window, TRUE);
	}
	else if (edata->overwrite == FR_OVERWRITE_ASK) {
		OverwriteData *odata;

		odata = overwrite_data_new (window);
		odata->edata = edata;
		odata->extract_all = (edata->file_list == NULL) || (g_list_length (edata->file_list) == window->archive->files->len);
		if (edata->file_list == NULL)
			edata->file_list = fr_window_get_file_list (window);
		odata->current_file = odata->edata->file_list;

		_fr_window_ask_overwrite_dialog (odata);
	}
	else
		_fr_window_archive_extract_from_edata (window, edata);
}


void
fr_window_archive_extract (FrWindow    *window,
			   GList       *file_list,
			   GFile       *destination,
			   const char  *base_dir,
			   gboolean     skip_older,
			   FrOverwrite  overwrite,
			   gboolean     junk_paths,
			   gboolean     ask_to_open_destination)
{
	ExtractData *edata;

	edata = extract_data_new (window,
				  file_list,
				  destination,
				  base_dir,
				  skip_older,
				  overwrite,
				  junk_paths,
				  ask_to_open_destination,
				  FALSE);

	fr_window_set_current_action (window,
				      FR_BATCH_ACTION_EXTRACT,
				      edata,
				      (GFreeFunc) extract_data_free);

	_fr_window_archive_extract_from_edata_maybe (window, edata);
}


/* -- fr_window_archive_extract_here -- */


#define MIN_TOPLEVEL_ITEMS_FOR_A_TARBOMB 2


static gboolean
_archive_extraction_generates_a_tarbomb (FrArchive *archive)
{
	gboolean    tarbomb = FALSE;
	GHashTable *names_hash;
	guint         n_toplevel_items;

	names_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	n_toplevel_items = 0;
	for (guint i = 0; ! tarbomb && (i < archive->files->len); i++) {
		FrFileData *fdata = g_ptr_array_index (archive->files, i);
		char     *second_separator;
		char     *name = NULL;
		gboolean  name_created;

		if ((fdata->full_path == NULL) || (fdata->full_path[0] == '\0'))
			continue;

		second_separator = strchr (fdata->full_path + 1 /* skip the first separator */, '/');
		if (second_separator != NULL) {
			name = g_strndup (fdata->full_path, second_separator - fdata->full_path);
			name_created = TRUE;
		}
		else {
			name = fdata->full_path;
			name_created = FALSE;
		}

		if (g_hash_table_lookup (names_hash, name) == NULL) {
			g_hash_table_insert (names_hash, name_created ? name : g_strdup (name), GINT_TO_POINTER (1));
			n_toplevel_items++;
			if (n_toplevel_items >= MIN_TOPLEVEL_ITEMS_FOR_A_TARBOMB)
				tarbomb = TRUE;
		}
		else if (name_created)
			g_free (name);
	}
	g_hash_table_destroy (names_hash);

	return tarbomb;
}


static GFile *
_get_destination_to_avoid_tarbomb (GFile *file)
{
	GFile      *directory;
	char       *name;
	const char *ext;
	char       *new_name;
	GFile      *destination;

	directory = g_file_get_parent (file);
	name = g_file_get_basename (file);
	ext = fr_get_archive_filename_extension (name);
	if (ext == NULL)
		/* if no extension is present add a suffix to the name... */
		new_name = g_strconcat (name, "_FILES", NULL);
	else
		/* ...else use the name without the extension */
		new_name = g_strndup (name, strlen (name) - strlen (ext));
	destination = g_file_get_child (directory, new_name);

	g_free (new_name);
	g_free (name);
	g_object_unref (directory);

	return destination;
}


void
fr_window_archive_extract_here (FrWindow   *window,
				gboolean    skip_older,
				gboolean    overwrite,
				gboolean    junk_paths)
{
	GFile       *destination;
	ExtractData *edata;

	if (_archive_extraction_generates_a_tarbomb (window->archive))
		destination = _get_destination_to_avoid_tarbomb (fr_archive_get_file (window->archive));
	else
		destination = g_file_get_parent (fr_archive_get_file (window->archive));

	edata = extract_data_new (window,
				  NULL,
				  destination,
				  NULL,
				  skip_older,
				  overwrite,
				  junk_paths,
				  _fr_window_get_ask_to_open_destination (window),
				  TRUE);

	fr_window_set_current_action (window,
				      FR_BATCH_ACTION_EXTRACT,
				      edata,
				      (GFreeFunc) extract_data_free);

	_fr_window_archive_extract_from_edata_maybe (window, edata);

	g_object_unref (destination);
}


/* -- fr_window_archive_test -- */


static void
archive_test_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_TESTING_ARCHIVE, error);

	_g_error_free (error);
}


void
fr_window_archive_test (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_archive_operation_started (window, FR_ACTION_TESTING_ARCHIVE);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_TEST,
					    NULL,
					    NULL);
	fr_archive_test (window->archive,
			 private->password,
			 private->cancellable,
			 archive_test_ready_cb,
			 window);
}


void
fr_window_set_password (FrWindow   *window,
			const char *password)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->password == password)
		return;

	if (private->password != NULL) {
		g_free (private->password);
		private->password = NULL;
	}

	if ((password != NULL) && (password[0] != '\0'))
		private->password = g_strdup (password);
}


void
fr_window_set_password_for_second_archive (FrWindow   *window,
					   const char *password)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->second_password != NULL) {
		g_free (private->second_password);
		private->second_password = NULL;
	}

	if ((password != NULL) && (password[0] != '\0'))
		private->second_password = g_strdup (password);
}


const char *
fr_window_get_password (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_val_if_fail (window != NULL, NULL);

	return private->password;
}


const char *
fr_window_get_password_for_second_archive (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_val_if_fail (window != NULL, NULL);

	return private->second_password;
}


void
fr_window_set_encrypt_header (FrWindow *window,
			      gboolean  encrypt_header)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->encrypt_header = encrypt_header;
}


gboolean
fr_window_get_encrypt_header (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return private->encrypt_header;
}


void
fr_window_set_compression (FrWindow      *window,
			   FrCompression  compression)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->compression = compression;
}


FrCompression
fr_window_get_compression (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return private->compression;
}


void
fr_window_set_volume_size (FrWindow *window,
			   guint     volume_size)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->volume_size = volume_size;
}


guint
fr_window_get_volume_size (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return private->volume_size;
}


void
fr_window_go_to_location (FrWindow   *window,
			  const char *path,
			  gboolean    force_update)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char *dir;

	g_return_if_fail (window != NULL);
	g_return_if_fail (path != NULL);

	if (force_update) {
		g_free (private->last_location);
		private->last_location = NULL;
	}

	if (path[strlen (path) - 1] != '/')
		dir = g_strconcat (path, "/", NULL);
	else
		dir = g_strdup (path);

	if ((private->last_location == NULL) || (strcmp (private->last_location, dir) != 0)) {
		g_free (private->last_location);
		private->last_location = dir;

		fr_window_history_add (window, dir);
		fr_window_update_file_list (window, TRUE);
		fr_window_update_current_location (window);
	}
	else
		g_free (dir);
}


const char *
fr_window_get_current_location (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->history_current == NULL) {
		const char *path = "/";
		fr_window_history_add (window, path);
		return path;
	}
	else
		return (const char*) private->history_current->data;
}


void
fr_window_go_up_one_level (FrWindow *window)
{
	char *parent_dir;

	g_return_if_fail (window != NULL);

	parent_dir = get_parent_dir (fr_window_get_current_location (window));
	fr_window_go_to_location (window, parent_dir, FALSE);
	g_free (parent_dir);
}


void
fr_window_go_back (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->history == NULL)
		return;
	if (private->history_current == NULL)
		return;
	if (private->history_current->next == NULL)
		return;
	private->history_current = private->history_current->next;

	fr_window_go_to_location (window, private->history_current->data, FALSE);
}


void
fr_window_go_forward (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->history == NULL)
		return;
	if (private->history_current == NULL)
		return;
	if (private->history_current->prev == NULL)
		return;
	private->history_current = private->history_current->prev;

	fr_window_go_to_location (window, private->history_current->data, FALSE);
}


void
fr_window_set_list_mode (FrWindow         *window,
			 FrWindowListMode  list_mode)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (list_mode == private->list_mode)
		return;

	private->list_mode = private->last_list_mode = list_mode;
	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fr_window_history_clear (window);
		fr_window_history_add (window, "/");
	}

	g_settings_set_enum (private->settings_listing, PREF_LISTING_LIST_MODE, private->last_list_mode);
	g_settings_set_boolean (private->settings_listing, PREF_LISTING_SHOW_PATH, (private->list_mode == FR_WINDOW_LIST_MODE_FLAT));

	fr_window_update_file_list (window, TRUE);
	fr_window_update_dir_tree (window);
	fr_window_update_current_location (window);
}


GtkTreeModel *
fr_window_get_list_store (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return GTK_TREE_MODEL (private->list_store);
}


void
fr_window_find (FrWindow *window,
		gboolean  active)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (active) {
		g_free (private->location_before_filter);
		private->location_before_filter = g_strdup (gtk_editable_get_text (GTK_EDITABLE (private->location_entry)));
		private->filter_mode = TRUE;
		gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (private->filter_bar), TRUE);
		gtk_widget_hide (private->location_bar);
		fr_window_update_dir_tree (window);
		gtk_widget_grab_focus (private->filter_entry);
	}
	else {
		private->filter_mode = FALSE;
		private->list_mode = private->last_list_mode;

		gtk_editable_set_text (GTK_EDITABLE (private->filter_entry), "");
		gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (private->filter_bar), FALSE);

		gtk_list_store_clear (private->list_store);

		fr_window_update_columns_visibility (window);
		fr_window_update_file_list (window, TRUE);
		fr_window_update_dir_tree (window);
		fr_window_update_current_location (window);
		if (private->location_before_filter != NULL)
			fr_window_go_to_location (window, private->location_before_filter, FALSE);

		g_free (private->location_before_filter);
		private->location_before_filter = NULL;
	}
}


void
fr_window_select_all (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view)));
}


void
fr_window_unselect_all (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (private->list_view)));
}


void
fr_window_stop (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->stoppable && (private->activity_ref > 0))
		g_cancellable_cancel (private->cancellable);
}


void
fr_window_action_new_archive (FrWindow *window)
{
	GtkWidget *dialog;

	if (fr_window_present_dialog_if_created (window, "new_archive"))
		return;

	dialog = fr_new_archive_dialog_new (_("New Archive"),
					    GTK_WINDOW (window),
					    FR_NEW_ARCHIVE_ACTION_NEW_MANY_FILES,
					    fr_window_get_open_default_dir (window),
					    NULL,
					    NULL);
	if (! fr_window_is_batch_mode (window))
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_signal_connect (FR_NEW_ARCHIVE_DIALOG (dialog),
			  "response",
			  G_CALLBACK (new_archive_dialog_response_cb),
			  window);
	fr_window_set_dialog (window, "new_archive", dialog);
	fr_new_archive_dialog_show_options (FR_NEW_ARCHIVE_DIALOG (dialog));
}


/* -- fr_window_action_save_as -- */


typedef struct {
	FrWindow  *window;
	FrArchive *new_archive;
	GFile     *file;
	char      *mime_type;
	char      *password;
	gboolean   encrypt_header;
	guint      volume_size;
	GFile     *temp_extraction_dir;
} ConvertData;


static ConvertData *
convert_data_new (GFile      *file,
		  const char *mime_type,
		  const char *password,
		  gboolean    encrypt_header,
		  guint       volume_size)
{
	ConvertData *cdata;

	cdata = g_new0 (ConvertData, 1);
	cdata->file = _g_object_ref (file);
	if (mime_type != NULL)
		cdata->mime_type = g_strdup (mime_type);
	if (password != NULL)
		cdata->password = g_strdup (password);
	cdata->encrypt_header = encrypt_header;
	cdata->volume_size = volume_size;
	cdata->temp_extraction_dir = _g_file_get_temp_work_dir (NULL);

	return cdata;
}


static void
convert_data_free (ConvertData *cdata)
{
	if (cdata == NULL)
		return;
	_g_file_remove_directory (cdata->temp_extraction_dir, NULL, NULL);
	_g_object_unref (cdata->temp_extraction_dir);
	_g_object_unref (cdata->file);
	_g_object_unref (cdata->new_archive);
	g_free (cdata->mime_type);
	g_free (cdata->password);
	g_free (cdata);
}


static void
archive_add_ready_for_conversion_cb (GObject      *source_object,
				     GAsyncResult *result,
				     gpointer      user_data)
{
	ConvertData *cdata = user_data;
	FrWindow    *window = cdata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError      *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);

	_fr_window_stop_activity_mode (window);
	close_progress_dialog (window, FALSE);

	if (error != NULL) {
		_handle_archive_operation_error (window,
						 cdata->new_archive,
						 FR_ACTION_ADDING_FILES,
						 error,
						 NULL,
						 NULL);
		fr_window_batch_stop (window);
		g_error_free (error);
		return;
	}

	/* update the file because multi-volume archives can have
	 * a different name after creation. */
	_g_object_unref (private->saving_file);
	private->saving_file = fr_archive_get_file (cdata->new_archive);

	fr_window_show_confirmation_dialog_with_open_archive (window);
	fr_window_batch_exec_next_action (window);
}


static void
_convertion_completed_with_error (FrWindow *window,
				  FrAction  action,
				  GError   *error)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	gboolean opens_dialog;

	g_return_if_fail (error != NULL);

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	_fr_window_stop_activity_mode (window);
	close_progress_dialog (window, FALSE);

	_handle_archive_operation_error (window, window->archive, action, error, NULL, &opens_dialog);
	if (opens_dialog)
		return;

	_g_clear_object (&private->saving_file);
	fr_window_batch_stop (window);
}


static void
archive_extraction_ready_for_convertion_cb (GObject      *source_object,
					    GAsyncResult *result,
					    gpointer      user_data)
{
	ConvertData *cdata = user_data;
	FrWindow    *window = cdata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList       *list;
	GError      *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_convertion_completed_with_error (window, FR_ACTION_EXTRACTING_FILES, error);
		return;
	}

	list = g_list_prepend (NULL, cdata->temp_extraction_dir);
	fr_archive_add_files (cdata->new_archive,
			      list,
			      cdata->temp_extraction_dir,
			      NULL,
			      FALSE,
			      FALSE,
			      cdata->password,
			      cdata->encrypt_header,
			      private->compression,
			      cdata->volume_size,
			      private->cancellable,
			      archive_add_ready_for_conversion_cb,
			      cdata);

	g_list_free (list);
}


static void
fr_window_archive_save_as (FrWindow   *window,
			   GFile      *file,
			   const char *mime_type,
			   const char *password,
			   gboolean    encrypt_header,
			   guint       volume_size)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrArchive   *new_archive;
	ConvertData *cdata;

	g_return_if_fail (window != NULL);
	g_return_if_fail (file != NULL);
	g_return_if_fail (window->archive != NULL);

	/* create the new archive */

	new_archive = fr_archive_create (file, mime_type);
	if (new_archive == NULL) {
		GtkWidget *d;
		char      *utf8_name;
		char      *message;

		utf8_name = _g_file_get_display_name (file);
		message = g_strdup_printf (_("Could not save the archive “%s”"), utf8_name);
		g_free (utf8_name);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   message,
					   "%s",
					   _("Archive type not supported."));
		_gtk_dialog_run (GTK_DIALOG (d));

		g_free (message);

		return;
	}

	cdata = convert_data_new (file, mime_type, password, encrypt_header, volume_size);
	cdata->window = window;
	cdata->new_archive = new_archive;

	_archive_operation_started (window, FR_ACTION_CREATING_ARCHIVE);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_SAVE_AS,
					    cdata,
					    (GFreeFunc) convert_data_free);

	g_signal_connect (cdata->new_archive,
			  "progress",
			  G_CALLBACK (fr_archive_progress_cb),
			  window);
	g_signal_connect (cdata->new_archive,
			  "message",
			  G_CALLBACK (fr_archive_message_cb),
			  window);
	g_signal_connect (cdata->new_archive,
			  "start",
			  G_CALLBACK (fr_archive_start_cb),
			  window);
	g_signal_connect (cdata->new_archive,
			  "stoppable",
			  G_CALLBACK (fr_archive_stoppable_cb),
			  window);
	g_signal_connect (cdata->new_archive,
			  "working-archive",
			  G_CALLBACK (fr_window_working_archive_cb),
			  window);

	_g_object_unref (private->saving_file);
	private->saving_file = g_object_ref (cdata->file);

	fr_archive_action_started (window->archive, FR_ACTION_EXTRACTING_FILES);
	fr_archive_extract (window->archive,
			    NULL,
			    cdata->temp_extraction_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    private->password,
			    private->cancellable,
			    archive_extraction_ready_for_convertion_cb,
			    cdata);
}


static void
save_as_new_get_file_cb (FrNewArchiveDialog *dialog,
			 GFile              *file,
			 const char         *mime_type,
			 gpointer            user_data)
{
	FrWindow   *window = user_data;
	const char *password;
	gboolean    encrypt_header;
	int         volume_size;
	GSettings  *settings;

	if (file == NULL)
		return;

	password = fr_new_archive_dialog_get_password (dialog);
	encrypt_header = fr_new_archive_dialog_get_encrypt_header (dialog);
	volume_size = fr_new_archive_dialog_get_volume_size (dialog);

	settings = g_settings_new (FILE_ROLLER_SCHEMA_NEW);
	g_settings_set_int (settings, PREF_NEW_VOLUME_SIZE, volume_size);
	g_object_unref (settings);

	fr_window_archive_save_as (window, file, mime_type, password, encrypt_header, volume_size);
	gtk_window_destroy (GTK_WINDOW (dialog));
}


static void
save_as_archive_dialog_response_cb (GtkDialog *dialog,
				    int        response,
				    gpointer   user_data)
{
	FrWindow *window = user_data;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		_archive_operation_cancelled (window, FR_ACTION_CREATING_ARCHIVE);
	}
	else if (response == GTK_RESPONSE_OK)
		fr_new_archive_dialog_get_file (FR_NEW_ARCHIVE_DIALOG (dialog),
						save_as_new_get_file_cb,
						window);
}


void
fr_window_action_save_as (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char            *archive_name;
	GtkWidget       *dialog;

	archive_name = NULL;
	if (private->archive_file != NULL) {
		GFileInfo *info;

		info = g_file_query_info (private->archive_file,
					  G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
					  0, NULL, NULL);

		if (info != NULL) {
			archive_name = g_strdup (g_file_info_get_display_name (info));
			g_object_unref (info);
		}
	}

	dialog = fr_new_archive_dialog_new (_("Save"),
					    GTK_WINDOW (window),
					    FR_NEW_ARCHIVE_ACTION_SAVE_AS,
					    fr_window_get_open_default_dir (window),
					    archive_name,
					    private->archive_file);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_signal_connect (FR_NEW_ARCHIVE_DIALOG (dialog),
			  "response",
			  G_CALLBACK (save_as_archive_dialog_response_cb),
			  window);
	fr_new_archive_dialog_show_options (FR_NEW_ARCHIVE_DIALOG (dialog));

	g_free (archive_name);
}


/* -- fr_window_archive_encrypt -- */


typedef struct {
	FrWindow  *window;
	char      *password;
	gboolean   encrypt_header;
	GFile     *temp_extraction_dir;
	GFile     *temp_new_file;
	FrArchive *new_archive;
} EncryptData;


static EncryptData *
encrypt_data_new (FrWindow   *window,
		  const char *password,
		  gboolean    encrypt_header)
{
	EncryptData *edata;

	edata = g_new0 (EncryptData, 1);
	edata->window = window;
	if (password != NULL)
		edata->password = g_strdup (password);
	edata->encrypt_header = encrypt_header;
	edata->temp_extraction_dir = _g_file_get_temp_work_dir (NULL);

	return edata;
}


static void
encrypt_data_free (EncryptData *edata)
{
	if (edata == NULL)
		return;

	if (edata->temp_new_file != NULL) {
		GFile *parent = g_file_get_parent (edata->temp_new_file);
		if (parent != NULL)
			_g_file_remove_directory (parent, NULL, NULL);
		_g_object_unref (parent);
	}
	_g_object_unref (edata->temp_new_file);
	_g_object_unref (edata->new_archive);
	_g_file_remove_directory (edata->temp_extraction_dir, NULL, NULL);
	_g_object_unref (edata->temp_extraction_dir);
	g_free (edata->password);
	g_free (edata);
}


static void
_encrypt_operation_completed_with_error (FrWindow *window,
					 FrAction  action,
					 GError   *error)
{
	gboolean opens_dialog;

	g_return_if_fail (error != NULL);

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	_fr_window_stop_activity_mode (window);
	_handle_archive_operation_error (window, window->archive, action, error, NULL, &opens_dialog);
	if (opens_dialog)
		return;

	close_progress_dialog (window, FALSE);
	fr_window_batch_stop (window);
}


static void
ecryption_copy_ready_cb (GObject      *source_object,
			 GAsyncResult *result,
			 gpointer      user_data)
{
	EncryptData *edata = user_data;
	FrWindow    *window = edata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError      *error = NULL;

	_fr_window_stop_activity_mode (window);
	close_progress_dialog (window, FALSE);

	if (! g_file_copy_finish (G_FILE (source_object), result, &error)) {
		_handle_archive_operation_error (window,
						 edata->new_archive,
						 FR_ACTION_CREATING_NEW_ARCHIVE,
						 error,
						 NULL,
						 NULL);
		fr_window_batch_stop (window);

		g_error_free (error);
		return;
	}

	fr_window_set_password (window, edata->password);
	fr_window_set_encrypt_header (window, edata->encrypt_header);
	private->reload_archive = TRUE;
	fr_window_batch_exec_next_action (window);
}


static void
encryption_copy_progress_cb (goffset  current_num_bytes,
			     goffset  total_num_bytes,
			     gpointer user_data)
{
	EncryptData *edata = user_data;

	fr_archive_progress (edata->new_archive, (double) current_num_bytes / total_num_bytes);
}


static void
archive_add_ready_for_encryption_cb (GObject      *source_object,
				     GAsyncResult *result,
				     gpointer      user_data)
{
	EncryptData *edata = user_data;
	FrWindow    *window = edata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError      *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_encrypt_operation_completed_with_error (window, FR_ACTION_ENCRYPTING_ARCHIVE, error);
		return;
	}

	fr_archive_action_started (window->archive, FR_ACTION_SAVING_REMOTE_ARCHIVE);
	g_file_copy_async (edata->temp_new_file,
			   fr_archive_get_file (window->archive),
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   private->cancellable,
			   encryption_copy_progress_cb,
			   edata,
			   ecryption_copy_ready_cb,
			   edata);
}


static void
archive_extraction_ready_for_encryption_cb (GObject      *source_object,
					    GAsyncResult *result,
					    gpointer      user_data)
{
	EncryptData *edata = user_data;
	FrWindow    *window = edata->window;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList       *list;
	GError      *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_encrypt_operation_completed_with_error (window, FR_ACTION_ENCRYPTING_ARCHIVE, error);
		return;
	}

	fr_archive_action_started (window->archive, FR_ACTION_ENCRYPTING_ARCHIVE);

	list = g_list_prepend (NULL, edata->temp_extraction_dir);
	fr_archive_add_files (edata->new_archive,
			      list,
			      edata->temp_extraction_dir,
			      NULL,
			      FALSE,
			      FALSE,
			      edata->password,
			      edata->encrypt_header,
			      private->compression,
			      0,
			      private->cancellable,
			      archive_add_ready_for_encryption_cb,
			      edata);

	g_list_free (list);
}


void
fr_window_archive_encrypt (FrWindow   *window,
			   const char *password,
			   gboolean    encrypt_header)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	EncryptData *edata;
	GFile       *temp_destination_parent;
	GFile       *temp_destination;
	char        *basename;
	GFile       *temp_new_file;
	FrArchive   *new_archive;

	/* create the new archive */

	if (g_file_is_native (fr_archive_get_file (window->archive)))
		temp_destination_parent = g_file_get_parent (fr_archive_get_file (window->archive));
	else
		temp_destination_parent = NULL;
	temp_destination = _g_file_get_temp_work_dir (temp_destination_parent);
	basename = _g_file_get_display_name (fr_archive_get_file (window->archive));
	temp_new_file = g_file_get_child (temp_destination, basename);

	g_free (basename);
	_g_object_unref (temp_destination_parent);

	new_archive = fr_archive_create (temp_new_file, fr_archive_get_mime_type (window->archive));
	if (new_archive == NULL) {
		GtkWidget *d;
		char      *utf8_name;
		char      *message;

		utf8_name = _g_file_get_display_name (temp_new_file);
		message = g_strdup_printf (_("Could not save the archive “%s”"), utf8_name);
		g_free (utf8_name);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   message,
					   "%s",
					   _("Archive type not supported."));
		_gtk_dialog_run (GTK_DIALOG (d));

		g_free (message);
		g_object_unref (temp_new_file);

		_g_file_remove_directory (temp_destination, NULL, NULL);
		g_object_unref (temp_destination);

		return;
	}

	g_object_unref (temp_destination);

	edata = encrypt_data_new (window, password, encrypt_header);
	edata->temp_new_file = temp_new_file;
	edata->new_archive = new_archive;

	g_signal_connect (edata->new_archive,
			  "progress",
			  G_CALLBACK (fr_archive_progress_cb),
			  window);
	g_signal_connect (edata->new_archive,
			  "message",
			  G_CALLBACK (fr_archive_message_cb),
			  window);
	g_signal_connect (edata->new_archive,
			  "start",
			  G_CALLBACK (fr_archive_start_cb),
			  window);
	g_signal_connect (edata->new_archive,
			  "stoppable",
			  G_CALLBACK (fr_archive_stoppable_cb),
			  window);
	g_signal_connect (edata->new_archive,
			  "working-archive",
			  G_CALLBACK (fr_window_working_archive_cb),
			  window);

	_archive_operation_started (window, FR_ACTION_ENCRYPTING_ARCHIVE);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_ENCRYPT,
					    edata,
					    (GFreeFunc) encrypt_data_free);

	fr_archive_action_started (window->archive, FR_ACTION_EXTRACTING_FILES);
	fr_archive_extract (window->archive,
			    NULL,
			    edata->temp_extraction_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    private->password,
			    private->cancellable,
			    archive_extraction_ready_for_encryption_cb,
			    edata);
}


/* -- fr_window_view_last_output  -- */


static void
last_output_window__unrealize_cb (GtkWidget  *widget,
				  gpointer    data)
{
	pref_util_save_window_geometry (GTK_WINDOW (widget), LAST_OUTPUT_SCHEMA_NAME);
	return;
}


void
fr_window_view_last_output (FrWindow   *window,
			    const char *title)
{
	GtkWidget     *dialog;
	GtkWidget     *vbox;
	GtkWidget     *text_view;
	GtkWidget     *scrolled;
	GtkTextBuffer *text_buffer;
	GtkTextIter    iter;
	GList         *scan;

	if (title == NULL)
		title = _("Last Output");

	dialog = g_object_new (GTK_TYPE_DIALOG,
			       "title", title,
			       "transient-for", window,
			       "destroy-with-parent", TRUE,
			       "use-header-bar", TRUE,
			       NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	/* Add text */

	scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (scrolled), TRUE);

	text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buffer, "monospace",
				    "family", "monospace",
				    "size-points", 12.0,
				    NULL);

	text_view = gtk_text_view_new_with_buffer (text_buffer);
	g_object_unref (text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (text_view), 10);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (text_view), 10);
	gtk_text_view_set_top_margin (GTK_TEXT_VIEW (text_view), 10);
	gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (text_view), 10);

	/**/

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (vbox)), "dialog-content");
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (vbox)), "small-padding");
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), text_view);
	_gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE);
	_gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox, TRUE, TRUE);

	/* signals */

	g_signal_connect (GTK_DIALOG (dialog),
			  "response",
			  G_CALLBACK (gtk_window_destroy),
			  NULL);

	g_signal_connect (GTK_DIALOG (dialog),
			  "unrealize",
			  G_CALLBACK (last_output_window__unrealize_cb),
			  NULL);

	/**/

	gtk_text_buffer_get_iter_at_offset (text_buffer, &iter, 0);
	if (FR_IS_COMMAND (window->archive))
		scan = fr_command_get_last_output (FR_COMMAND (window->archive));
	else
		scan = NULL;
	for (; scan; scan = scan->next) {
		char        *line = scan->data;
		char        *utf8_line;
		gsize        bytes_written;

		utf8_line = g_locale_to_utf8 (line, -1, NULL, &bytes_written, NULL);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer,
							  &iter,
							  utf8_line,
							  bytes_written,
							  "monospace", NULL);
		g_free (utf8_line);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", 1);
	}

	/**/

	pref_util_restore_window_geometry (GTK_WINDOW (dialog), LAST_OUTPUT_SCHEMA_NAME);
}


/* -- fr_window_rename_selection -- */


typedef struct {
	char     *path_to_rename;
	char     *old_name;
	char     *new_name;
	char     *current_dir;
	gboolean  is_dir;
	gboolean  dir_in_archive;
	char     *original_path;
} RenameData;


static RenameData*
rename_data_new (const char *path_to_rename,
		 const char *old_name,
		 const char *new_name,
		 const char *current_dir,
		 gboolean    is_dir,
		 gboolean    dir_in_archive,
		 const char *original_path)
{
	RenameData *rdata;

	rdata = g_new0 (RenameData, 1);
	rdata->path_to_rename = g_strdup (path_to_rename);
	if (old_name != NULL)
		rdata->old_name = g_strdup (old_name);
	if (new_name != NULL)
		rdata->new_name = g_strdup (new_name);
	if (current_dir != NULL)
		rdata->current_dir = g_strdup (current_dir);
	rdata->is_dir = is_dir;
	rdata->dir_in_archive = dir_in_archive;
	if (original_path != NULL)
		rdata->original_path = g_strdup (original_path);

	return rdata;
}


static void
rename_data_free (RenameData *rdata)
{
	g_return_if_fail (rdata != NULL);

	g_free (rdata->path_to_rename);
	g_free (rdata->old_name);
	g_free (rdata->new_name);
	g_free (rdata->current_dir);
	g_free (rdata->original_path);
	g_free (rdata);
}


static void
archive_rename_ready_cb (GObject      *source_object,
			 GAsyncResult *result,
			 gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_RENAMING_FILES, error);

	_g_error_free (error);
}


static void
rename_selection (FrWindow   *window,
		  const char *path_to_rename,
		  const char *old_name,
		  const char *new_name,
		  const char *current_dir,
		  gboolean    is_dir,
		  gboolean    dir_in_archive,
		  const char *original_path)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	RenameData *rdata;
	GList      *file_list;

	rdata = rename_data_new (path_to_rename,
				 old_name,
				 new_name,
				 current_dir,
				 is_dir,
				 dir_in_archive,
				 original_path);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_RENAME,
					    rdata,
					    (GFreeFunc) rename_data_free);

	_archive_operation_started (window, FR_ACTION_RENAMING_FILES);

	g_object_set (window->archive,
		      "compression", private->compression,
		      "encrypt-header", private->encrypt_header,
		      "password", private->password,
		      "volume-size", private->volume_size,
		      NULL);

	if (is_dir)
		file_list = get_dir_list_from_path (window, rdata->path_to_rename);
	else
		file_list = g_list_append (NULL, g_strdup (rdata->path_to_rename));
	fr_window_clipboard_remove_file_list (window, file_list);
	fr_archive_rename (window->archive,
			   file_list,
			   rdata->old_name,
			   rdata->new_name,
			   rdata->current_dir,
			   rdata->is_dir,
			   rdata->dir_in_archive,
			   rdata->original_path,
			   private->cancellable,
			   archive_rename_ready_cb,
			   window);

	_g_string_list_free (file_list);
}


static gboolean
name_is_present (FrWindow    *window,
		 const char  *current_dir,
		 const char  *new_name,
		 char       **reason)
{
	gboolean  retval = FALSE;
	char     *new_filename;
	size_t    new_filename_l;

	*reason = NULL;

	new_filename = g_build_filename (current_dir, new_name, NULL);
	new_filename_l = strlen (new_filename);

	for (guint i = 0; i < window->archive->files->len; i++) {
		FrFileData *fdata = g_ptr_array_index (window->archive->files, i);
		const char *filename = fdata->full_path;

		if ((strncmp (filename, new_filename, new_filename_l) == 0)
		    && ((filename[new_filename_l] == '\0')
			|| (filename[new_filename_l] == G_DIR_SEPARATOR))) {
			char *utf8_name = g_filename_display_name (new_name);

			if (filename[new_filename_l] == G_DIR_SEPARATOR)
				*reason = g_strdup_printf (_("A folder named “%s” already exists.\n\n%s"), utf8_name, _("Please use a different name."));
			else
				*reason = g_strdup_printf (_("A file named “%s” already exists.\n\n%s"), utf8_name, _("Please use a different name."));

			retval = TRUE;
			break;
		}
	}

	g_free (new_filename);

	return retval;
}


/* -- fr_window_rename_selection -- */


typedef struct {
	FrWindow *window;
	gboolean  from_sidebar;
	char     *path_to_rename;
	char     *parent_dir;
	char     *old_name;
	gboolean  renaming_dir;
	gboolean  dir_in_archive;
	char     *original_path;
} RenameDialogData;


static void
rename_dialog_data_free (RenameDialogData *rename_data)
{
	g_free (rename_data->path_to_rename);
	g_free (rename_data->parent_dir);
	g_free (rename_data->old_name);
	g_free (rename_data->original_path);
	g_free (rename_data);
}


static void
rename_dialog_response_cb (GtkDialog *dialog,
			   int        response_id,
			   gpointer   user_data)
{
	RenameDialogData *rename_data = user_data;
	char             *utf8_new_name;
	char             *new_name;
	char             *reason;

	if (response_id != GTK_RESPONSE_YES) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		rename_dialog_data_free (rename_data);
		return;
	}

	utf8_new_name = _gth_request_dialog_get_text (dialog);
	if (utf8_new_name == NULL) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		rename_dialog_data_free (rename_data);
		return;
	}

	new_name = g_filename_from_utf8 (utf8_new_name, -1, 0, 0, 0);
	g_free (utf8_new_name);

	reason = NULL;
	if (!_g_basename_is_valid (new_name, rename_data->old_name, &reason)) {
		GtkWidget *dlg;

		dlg = _gtk_error_dialog_new (GTK_WINDOW (dialog),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     NULL,
					     (rename_data->renaming_dir ? _("Could not rename the folder") : _("Could not rename the file")),
					     "%s",
					     reason);
		_gtk_dialog_run (GTK_DIALOG (dlg));

		g_free (reason);
		g_free (new_name);
		return;
	}

	if (name_is_present (rename_data->window, rename_data->parent_dir, new_name, &reason)) {
		GtkWidget *dlg;

		dlg = _gtk_message_dialog_new (GTK_WINDOW (dialog),
					       GTK_DIALOG_MODAL,
					       (rename_data->renaming_dir ? _("Could not rename the folder") : _("Could not rename the file")),
					       reason,
					       _GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
					       NULL);
		_gtk_dialog_run (GTK_DIALOG (dlg));

		g_free (reason);
		g_free (new_name);
		return;
	}

	gtk_window_destroy (GTK_WINDOW (dialog));

	rename_selection (rename_data->window,
			  rename_data->path_to_rename,
			  rename_data->old_name,
			  new_name,
			  rename_data->parent_dir,
			  rename_data->renaming_dir,
			  rename_data->dir_in_archive,
			  rename_data->original_path);

	g_free (new_name);
	rename_dialog_data_free (rename_data);
}


void
fr_window_rename_selection (FrWindow *window,
			    gboolean  from_sidebar)
{
	RenameDialogData *rename_data;
	char             *utf8_old_name;
	GtkWidget        *rename_dialog;

	rename_data = g_new0 (RenameDialogData, 1);
	rename_data->window = window;
	rename_data->from_sidebar = from_sidebar;
	rename_data->renaming_dir = FALSE;
	rename_data->dir_in_archive = FALSE;

	if (from_sidebar) {
		rename_data->path_to_rename = fr_window_get_selected_folder_in_tree_view (window);
		if (rename_data->path_to_rename == NULL) {
			rename_dialog_data_free (rename_data);
			return;
		}
		rename_data->parent_dir = _g_path_remove_level (rename_data->path_to_rename);
		rename_data->old_name = g_strdup (_g_path_get_basename (rename_data->path_to_rename));
		rename_data->renaming_dir = TRUE;
	}
	else {
		FrFileData *selected_item;

		selected_item = fr_window_get_selected_item_from_file_list (window);
		if (selected_item == NULL) {
			rename_dialog_data_free (rename_data);
			return;
		}

		rename_data->renaming_dir = fr_file_data_is_dir (selected_item);
		rename_data->dir_in_archive = selected_item->dir && ! selected_item->list_dir;
		rename_data->original_path = g_strdup (selected_item->original_path);

		if (rename_data->renaming_dir && ! rename_data->dir_in_archive) {
			rename_data->parent_dir = g_strdup (fr_window_get_current_location (window));
			rename_data->old_name = g_strdup (selected_item->list_name);
			rename_data->path_to_rename = g_build_filename (rename_data->parent_dir, rename_data->old_name, NULL);
		}
		else {
			if (rename_data->renaming_dir) {
				rename_data->path_to_rename = _g_path_remove_ending_separator (selected_item->full_path);
				rename_data->parent_dir = _g_path_remove_level (rename_data->path_to_rename);
			}
			else {
				rename_data->path_to_rename = g_strdup (selected_item->original_path);
				rename_data->parent_dir = _g_path_remove_level (selected_item->full_path);
			}
			rename_data->old_name = g_strdup (selected_item->name);
		}

		fr_file_data_free (selected_item);
	}

	utf8_old_name = g_locale_to_utf8 (rename_data->old_name, -1 ,0 ,0 ,0);
	rename_dialog = _gtk_request_dialog_new (GTK_WINDOW (rename_data->window),
						 (GTK_DIALOG_DESTROY_WITH_PARENT
						  | GTK_DIALOG_MODAL),
						 _("Rename"),
						 (rename_data->renaming_dir ? _("_New folder name:") : _("_New file name:")),
						 utf8_old_name,
						 1024,
						 _GTK_LABEL_CANCEL,
						 _("_Rename"));
	g_signal_connect (rename_dialog,
			  "response",
			  G_CALLBACK (rename_dialog_response_cb),
			  rename_data);
	gtk_window_present (GTK_WINDOW (rename_dialog));

	g_free (utf8_old_name);
}


/* -- fr_window_paste_selection -- */


GList *
fr_window_get_selection (FrWindow   *window,
			 gboolean    from_sidebar,
			 char      **return_base_dir)
{
	GList *files;
	char  *base_dir;

	if (from_sidebar) {
		char *selected_folder;
		char *parent_folder;

		files = fr_window_get_folder_tree_selection (window, TRUE, NULL);
		selected_folder = fr_window_get_selected_folder_in_tree_view (window);
		parent_folder = _g_path_remove_level (selected_folder);
		if (parent_folder == NULL)
			base_dir = g_strdup ("/");
		else if (parent_folder[strlen (parent_folder) - 1] == '/')
			base_dir = g_strdup (parent_folder);
		else
			base_dir = g_strconcat (parent_folder, "/", NULL);
		g_free (selected_folder);
		g_free (parent_folder);
	}
	else {
		files = fr_window_get_file_list_selection (window, TRUE, FALSE, NULL);
		base_dir = g_strdup (fr_window_get_current_location (window));
	}

	if (return_base_dir)
		*return_base_dir = base_dir;
	else
		g_free (base_dir);

	return files;
}


static void
fr_window_copy_or_cut_selection (FrWindow      *window,
				 FrClipboardOp  op,
				 gboolean       from_sidebar)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *files;
	char  *base_dir;

	files = fr_window_get_selection (window, from_sidebar, &base_dir);

	if (app_clipboard != NULL)
		fr_clipboard_data_unref (app_clipboard);
	app_clipboard = fr_clipboard_data_new ();
	app_clipboard->file = g_object_ref (private->archive_file);
	app_clipboard->files = files;
	app_clipboard->op = op;
	app_clipboard->base_dir = base_dir;

	fr_window_update_sensitivity (window);
}


void
fr_window_copy_selection (FrWindow *window,
			  gboolean  from_sidebar)
{
	fr_window_copy_or_cut_selection (window, FR_CLIPBOARD_OP_COPY, from_sidebar);
}


void
fr_window_cut_selection (FrWindow *window,
			 gboolean  from_sidebar)
{
	fr_window_copy_or_cut_selection (window, FR_CLIPBOARD_OP_CUT, from_sidebar);
}


/* -- fr_window_paste_from_clipboard_data -- */


static void
_paste_from_archive_operation_completed (FrWindow *window,
					 FrAction  action,
					 GError   *error)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrArchive *archive;

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	_fr_window_stop_activity_mode (window);
	close_progress_dialog (window, FALSE);

	if ((error != NULL) && (error->code == FR_ERROR_ASK_PASSWORD)) {
		dlg_ask_password_for_second_archive (window);
		return;
	}

	if (action == FR_ACTION_ADDING_FILES)
		archive = window->archive;
	else
		archive = private->copy_from_archive;
	_handle_archive_operation_error (window, archive, action, error, NULL, NULL);

	if (error != NULL) {
		if (private->second_password != NULL) {
			g_free (private->second_password);
			private->second_password = NULL;
		}

		fr_clipboard_data_unref (private->clipboard_data);
		private->clipboard_data = NULL;
	}
}


static void
paste_from_archive_completed_successfully (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, NULL);

	fr_clipboard_data_unref (private->clipboard_data);
	private->clipboard_data = NULL;

	if (private->second_password != NULL) {
		g_free (private->second_password);
		private->second_password = NULL;
	}

	private->archive_new = FALSE;
	fr_window_archive_reload (window);
}


static void
paste_from_archive_remove_ready_cb (GObject      *source_object,
				    GAsyncResult *result,
				    gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, error);
		g_error_free (error);
		return;
	}

	paste_from_archive_completed_successfully (window);
}


static void
paste_from_archive_paste_clipboard_ready_cb (GObject      *source_object,
					     GAsyncResult *result,
					     gpointer      user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError   *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, error);
		g_error_free (error);
		return;
	}

	if (private->clipboard_data->op == FR_CLIPBOARD_OP_CUT) {
		fr_archive_action_started (private->copy_from_archive, FR_ACTION_DELETING_FILES);
		fr_archive_remove (private->copy_from_archive,
				   private->clipboard_data->files,
				   private->compression,
				   private->cancellable,
				   paste_from_archive_remove_ready_cb,
				   window);
	}
	else
		paste_from_archive_completed_successfully (window);
}


static void
paste_from_archive_extract_ready_cb (GObject      *source_object,
				     GAsyncResult *result,
				     gpointer      user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError   *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, error);
		g_error_free (error);
		return;
	}

	fr_archive_paste_clipboard (window->archive,
				    private->clipboard_data->file,
				    private->password,
				    private->encrypt_header,
				    private->compression,
				    private->volume_size,
				    private->clipboard_data->op,
				    private->clipboard_data->base_dir,
				    private->clipboard_data->files,
				    private->clipboard_data->tmp_dir,
				    private->clipboard_data->current_dir,
				    private->cancellable,
				    paste_from_archive_paste_clipboard_ready_cb,
				    window);
}


static void
paste_from_archive_list_ready_cb (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError   *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, error);
		g_error_free (error);
		return;
	}

	fr_archive_action_started (private->copy_from_archive, FR_ACTION_EXTRACTING_FILES);
	fr_archive_extract (private->copy_from_archive,
			    private->clipboard_data->files,
			    private->clipboard_data->tmp_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    private->clipboard_data->password,
			    private->cancellable,
			    paste_from_archive_extract_ready_cb,
			    window);
}


static void
paste_from_archive_open_cb (GObject      *source_object,
			    GAsyncResult *result,
			    gpointer      user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GError   *error;

	_g_object_unref (private->copy_from_archive);
	private->copy_from_archive = fr_archive_open_finish (G_FILE (source_object), result, &error);
	if (private->copy_from_archive == NULL) {
		_paste_from_archive_operation_completed (window, FR_ACTION_PASTING_FILES, error);
		g_error_free (error);
		return;
	}

	g_signal_connect (private->copy_from_archive,
			  "progress",
			  G_CALLBACK (fr_archive_progress_cb),
			  window);
	g_signal_connect (private->copy_from_archive,
			  "message",
			  G_CALLBACK (fr_archive_message_cb),
			  window);
	g_signal_connect (private->copy_from_archive,
			  "start",
			  G_CALLBACK (fr_archive_start_cb),
			  window);
	g_signal_connect (private->copy_from_archive,
			  "stoppable",
			  G_CALLBACK (fr_archive_stoppable_cb),
			  window);
	g_signal_connect (private->copy_from_archive,
			  "working-archive",
			  G_CALLBACK (fr_window_working_archive_cb),
			  window);

	fr_archive_action_started (private->copy_from_archive, FR_ACTION_LISTING_CONTENT);
	fr_archive_list (private->copy_from_archive,
			 private->clipboard_data->password,
			 private->cancellable,
			 paste_from_archive_list_ready_cb,
			 window);
}


static void
_window_started_loading_file (FrWindow *window,
			      GFile    *file)
{
	char *description;

	description = get_action_description (window, FR_ACTION_LOADING_ARCHIVE, file);
	fr_archive_message_cb (NULL, description, window);

	g_free (description);
}


static void
fr_window_paste_from_clipboard_data (FrWindow        *window,
				     FrClipboardData *data)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	const char *current_dir_relative;
	GHashTable *created_dirs;
	GList      *scan;
	char       *from_archive;
	char       *to_archive;

	if (private->second_password != NULL)
		fr_clipboard_data_set_password (data, private->second_password);

	if (private->clipboard_data != data) {
		fr_clipboard_data_unref (private->clipboard_data);
		private->clipboard_data = data;
	}

	if (g_file_equal (data->file, private->archive_file) && (g_strcmp0 (data->current_dir, data->base_dir) == 0)) {
		_gtk_error_dialog_run (GTK_WINDOW (window),
				       _("Could not perform the operation"),
				       "%s",
				       _("Source and destination are the same."));
		fr_clipboard_data_unref (private->clipboard_data);
		private->clipboard_data = NULL;
		return;
	}

	fr_window_set_current_action (window,
				      FR_BATCH_ACTION_PASTE,
				      fr_clipboard_data_ref (data),
				      (GFreeFunc) fr_clipboard_data_unref);

	current_dir_relative = data->current_dir + 1;

	data->tmp_dir = _g_file_get_temp_work_dir (NULL);
	created_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (scan = data->files; scan; scan = scan->next) {
		const char *old_name = (char*) scan->data;
		char       *new_name;
		char       *dir;

		new_name = g_build_filename (current_dir_relative, old_name + strlen (data->base_dir) - 1, NULL);
		dir = _g_path_remove_level (new_name);
		if ((dir != NULL) && (g_hash_table_lookup (created_dirs, dir) == NULL)) {
			GFile *directory;
			char *uri;

			directory = _g_file_append_path (data->tmp_dir, dir, NULL);
			uri = g_file_get_uri (directory);
			debug (DEBUG_INFO, "mktree %s\n", uri);
			_g_file_make_directory_tree (directory, 0700, NULL);

			g_hash_table_replace (created_dirs, g_strdup (dir), GINT_TO_POINTER (1));

			g_object_unref (directory);
			g_free (uri);
		}

		g_free (dir);
		g_free (new_name);
	}
	g_hash_table_destroy (created_dirs);

	/**/

	g_free (private->custom_action_message);
	from_archive = _g_file_get_display_name (data->file);
	to_archive = _g_file_get_display_name (private->archive_file);
	if (data->op == FR_CLIPBOARD_OP_CUT)
		/* Translators: %s are archive filenames */
		private->custom_action_message = g_strdup_printf (_("Moving the files from “%s” to “%s”"), from_archive, to_archive);
	else
		/* Translators: %s are archive filenames */
		private->custom_action_message = g_strdup_printf (_("Copying the files from “%s” to “%s”"), from_archive, to_archive);
	_archive_operation_started (window, FR_ACTION_PASTING_FILES);

	_window_started_loading_file (window, data->file);
	fr_archive_open (data->file,
			 private->cancellable,
			 paste_from_archive_open_cb,
			 window);

	g_free (to_archive);
	g_free (from_archive);
}


static void
fr_window_paste_selection_to (FrWindow   *window,
			      const char *current_dir)
{
	if (app_clipboard == NULL)
		return;
	FrClipboardData *paste_data = fr_clipboard_data_dup (app_clipboard);
	paste_data->current_dir = g_strdup (current_dir);
	fr_window_paste_from_clipboard_data (window, paste_data);
}


/* -- fr_window_paste_selection -- */


static void
paste_destination_dialog_response_cb (GtkDialog *dialog,
				      int        response_id,
				      gpointer   user_data)
{
	FrWindow *window = user_data;
	char     *utf8_path;
	char     *destination;
	char     *current_dir;

	if (response_id != GTK_RESPONSE_YES) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		return;
	}

	utf8_path = _gth_request_dialog_get_text (dialog);
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (utf8_path == NULL)
		return;

	destination = g_filename_from_utf8 (utf8_path, -1, NULL, NULL, NULL);
	if (destination[0] != '/')
		current_dir = g_build_filename (fr_window_get_current_location (window), destination, NULL);
	else
		current_dir = g_strdup (destination);
	fr_window_paste_selection_to (window, current_dir);

	g_free (current_dir);
	g_free (destination);
	g_free (utf8_path);
}


void
fr_window_paste_selection (FrWindow *window,
			   gboolean  from_sidebar)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	char            *utf8_old_path;
	GtkWidget       *destination_dialog;

	if (private->list_mode == FR_WINDOW_LIST_MODE_FLAT)
		return;

	utf8_old_path = g_filename_to_utf8 (fr_window_get_current_location (window), -1, NULL, NULL, NULL);
	destination_dialog = _gtk_request_dialog_new (GTK_WINDOW (window),
						      (GTK_DIALOG_DESTROY_WITH_PARENT
						       | GTK_DIALOG_MODAL),
						      _("Paste Selection"),
						      _("_Destination folder:"),
						      utf8_old_path,
						      1024,
						      _GTK_LABEL_CANCEL,
						      _("_Paste"));
	g_signal_connect (destination_dialog,
			  "response",
			  G_CALLBACK (paste_destination_dialog_response_cb),
			  window);
	gtk_window_present (GTK_WINDOW (destination_dialog));

	g_free (utf8_old_path);
}


/* -- fr_window_open_files -- */


void
fr_window_open_files_with_command (FrWindow *window,
				   GList    *file_list,
				   char     *command)
{
	GAppInfo *app;
	GError   *error = NULL;

	app = g_app_info_create_from_commandline (command, NULL, G_APP_INFO_CREATE_NONE, &error);
	if (error != NULL) {
		_gtk_error_dialog_run (GTK_WINDOW (window),
				       _("Could not perform the operation"),
				       "%s",
				       error->message);
		g_clear_error (&error);
		return;
	}

	fr_window_open_files_with_application (window, file_list, app);
}


void
fr_window_open_files_with_application (FrWindow *window,
				       GList    *file_list,
				       GAppInfo *app)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList               *uris;
	GList               *scan;
	GdkAppLaunchContext *context;
	GError              *error = NULL;

	if (private->activity_ref > 0)
		return;

	uris = NULL;
	for (scan = file_list; scan; scan = scan->next)
		uris = g_list_prepend (uris, g_file_get_uri (G_FILE (scan->data)));

	context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (window)));
	gdk_app_launch_context_set_timestamp (context, 0);

	if (! g_app_info_launch_uris (app, uris, G_APP_LAUNCH_CONTEXT (context), &error)) {
		_gtk_error_dialog_run (GTK_WINDOW (window),
				       _("Could not perform the operation"),
				       "%s",
				       error->message);
		g_clear_error (&error);
	}

	g_object_unref (context);
	_g_string_list_free (uris);
}


typedef struct {
	int          ref_count;
	FrWindow    *window;
	GList       *file_list;
	gboolean     ask_application;
	FrCommandData *cdata;
} OpenFilesData;


static OpenFilesData*
open_files_data_new (FrWindow *window,
		     GList    *file_list,
		     gboolean  ask_application)

{
	OpenFilesData *odata;
	GList         *scan;

	odata = g_new0 (OpenFilesData, 1);
	odata->ref_count = 1;
	odata->window = g_object_ref (window);
	odata->file_list = _g_string_list_dup (file_list);
	odata->ask_application = ask_application;
	odata->cdata = g_new0 (FrCommandData, 1);
	odata->cdata->temp_dir = _g_file_get_temp_work_dir (NULL);
	odata->cdata->file_list = NULL;
	for (scan = file_list; scan; scan = scan->next) {
		char  *filename = scan->data;
		GFile *file;

		file = _g_file_append_path (odata->cdata->temp_dir, filename, NULL);
		odata->cdata->file_list = g_list_prepend (odata->cdata->file_list, file);
	}

	/* Add to CommandList so the cdata is released on exit. */
	CommandList = g_list_prepend (CommandList, odata->cdata);

	return odata;
}


static void
open_files_data_ref (OpenFilesData *odata)
{
	g_return_if_fail (odata != NULL);
	odata->ref_count++;
}


static void
open_files_data_unref (OpenFilesData *odata)
{
	g_return_if_fail (odata != NULL);

	if (--odata->ref_count > 0)
		return;

	_g_string_list_free (odata->file_list);
	g_object_unref (odata->window);
	g_free (odata);
}


/* -- -- */


void
fr_window_update_dialog_closed (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->update_dialog = NULL;
}


static void
update_files_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	FrWindow *window = user_data;
	GError   *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (window, FR_ACTION_UPDATING_FILES, error);

	_g_error_free (error);
}


gboolean
fr_window_update_files (FrWindow *window,
			GList    *open_file_list)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	GList *file_list;
	GList *dir_list;
	GList *scan;

	if (private->activity_ref > 0)
		return FALSE;

	if (window->archive->read_only)
		return FALSE;

	/* the size will be computed by the archive object */
	window->archive->files_to_add_size = 0;

	file_list = NULL;
	dir_list = NULL;
	for (scan = open_file_list; scan; scan = scan->next) {
		OpenFile *open_file = scan->data;

		file_list = g_list_prepend (file_list, g_object_ref (open_file->extracted_file));
		dir_list = g_list_prepend (dir_list, g_object_ref (open_file->temp_dir));
	}

	_archive_operation_started (window, FR_ACTION_UPDATING_FILES);

	fr_archive_update_open_files (window->archive,
				      file_list,
				      dir_list,
				      private->password,
				      private->encrypt_header,
				      private->compression,
				      private->volume_size,
				      private->cancellable,
				      update_files_ready_cb,
				      window);

	_g_object_list_unref (dir_list);
	_g_object_list_unref (file_list);

	return TRUE;
}


static void
open_file_modified_cb (GFileMonitor     *monitor,
		       GFile            *monitor_file,
		       GFile            *other_file,
		       GFileMonitorEvent event_type,
		       gpointer          user_data)
{
	FrWindow *window = user_data;
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	OpenFile *file;
	GList    *scan;

	if ((event_type != G_FILE_MONITOR_EVENT_CHANGED)
	    && (event_type != G_FILE_MONITOR_EVENT_CREATED))
	{
		return;
	}

	file = NULL;
	for (scan = private->open_files; scan; scan = scan->next) {
		OpenFile *test = scan->data;
		if (_g_file_cmp_uris (test->extracted_file, monitor_file) == 0) {
			file = test;
			break;
		}
	}

	g_return_if_fail (file != NULL);

	if (private->update_dialog == NULL)
		private->update_dialog = dlg_update (window);
	dlg_update_add_file (private->update_dialog, file);
}


static void
fr_window_monitor_open_file (FrWindow *window,
			     OpenFile *file)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->open_files = g_list_prepend (private->open_files, file);
	file->monitor = g_file_monitor_file (file->extracted_file, 0, NULL, NULL);
	g_signal_connect (file->monitor,
			  "changed",
			  G_CALLBACK (open_file_modified_cb),
			  window);
}


static void
monitor_extracted_files (OpenFilesData *odata)
{
	FrWindow *window = odata->window;
	GList    *scan1, *scan2;

	for (scan1 = odata->file_list, scan2 = odata->cdata->file_list;
	     scan1 && scan2;
	     scan1 = scan1->next, scan2 = scan2->next)
	{
		char     *original_path = (char *) scan1->data;
		GFile    *extracted_file = G_FILE (scan2->data);
		OpenFile *ofile;

		ofile = open_file_new (original_path, extracted_file, odata->cdata->temp_dir);
		if (ofile != NULL)
			fr_window_monitor_open_file (window, ofile);
	}
}


#ifdef USE_NATIVE_APPCHOOSER

static void
open_extracted_files_with_native_appchooser (OpenFilesData *odata)
{
	GList     *file_list = odata->cdata->file_list;
	GList     *scan;
	GtkWindow *window;

	g_return_if_fail (file_list != NULL);

	if (file_list->data == NULL)
		return;

	window = GTK_WINDOW (odata->window);

	if (! odata->window->archive->read_only)
		monitor_extracted_files (odata);

	if (odata->ask_application) {
		dlg_open_with (odata->window, file_list);
		return;
	}

	for (scan = file_list; scan; scan = scan->next) {
		g_autofree char *uri;
		uri = g_file_get_uri (G_FILE (scan->data));
		gtk_show_uri (window, uri, GDK_CURRENT_TIME);
	}
}

#endif


static void
open_extracted_files_with_nonnative_appchooser (OpenFilesData *odata)
{
	GList               *file_list = odata->cdata->file_list;
	GFile               *first_file;
	const char          *first_mime_type;
	GAppInfo            *app;
	GList               *files_to_open = NULL;
	GdkAppLaunchContext *context;
	GError              *error = NULL;

	g_return_if_fail (file_list != NULL);

	first_file = G_FILE (file_list->data);
	if (first_file == NULL)
		return;

	if (! odata->window->archive->read_only)
		monitor_extracted_files (odata);

	if (odata->ask_application) {
		dlg_open_with (odata->window, file_list);
		return;
	}

	first_mime_type = _g_file_get_mime_type (first_file, FALSE);
	app = g_app_info_get_default_for_type (first_mime_type, FALSE);

	if (app == NULL) {
		dlg_open_with (odata->window, file_list);
		return;
	}

	files_to_open = g_list_append (files_to_open, g_file_get_uri (first_file));

	if (g_app_info_supports_files (app)) {
		GList *scan;

		for (scan = file_list->next; scan; scan = scan->next) {
			GFile      *file = G_FILE (scan->data);
			const char *mime_type;

			mime_type = _g_file_get_mime_type (file, FALSE);
			if (mime_type == NULL)
				continue;

			if (strcmp (mime_type, first_mime_type) == 0) {
				files_to_open = g_list_append (files_to_open, g_file_get_uri (file));
			}
			else {
				GAppInfo *app2;

				app2 = g_app_info_get_default_for_type (mime_type, FALSE);
				if (g_app_info_equal (app, app2))
					files_to_open = g_list_append (files_to_open, g_file_get_uri (file));
				g_object_unref (app2);
			}
		}
	}

	context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (odata->window)));
	gdk_app_launch_context_set_timestamp (context, 0);
	if (! g_app_info_launch_uris (app, files_to_open, G_APP_LAUNCH_CONTEXT (context), &error)) {
		_gtk_error_dialog_run (GTK_WINDOW (odata->window),
				       _("Could not perform the operation"),
				       "%s",
				       error->message);
		g_clear_error (&error);
	}

	g_object_unref (context);
	g_object_unref (app);
	_g_string_list_free (files_to_open);
}


static void
fr_window_open_extracted_files (OpenFilesData *odata)
{
#ifdef USE_NATIVE_APPCHOOSER
	// Use the native appchooser only for a single file.
	if (odata->cdata->file_list->next == NULL) {
		open_extracted_files_with_native_appchooser (odata);
	}
	else
#endif
	open_extracted_files_with_nonnative_appchooser (odata);
}


static void
open_files_extract_ready_cb (GObject      *source_object,
			     GAsyncResult *result,
			     gpointer      user_data)
{
	OpenFilesData *odata = user_data;
	GError        *error = NULL;

	open_files_data_ref (odata);
	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	_archive_operation_completed (odata->window, FR_ACTION_EXTRACTING_FILES, error);

	if (error == NULL)
		fr_window_open_extracted_files (odata);

	open_files_data_unref (odata);
	_g_error_free (error);
}


void
fr_window_open_files (FrWindow *window,
		      GList    *file_list,
		      gboolean  ask_application)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	OpenFilesData *odata;

	if (private->activity_ref > 0)
		return;

	odata = open_files_data_new (window, file_list, ask_application);
	fr_window_set_current_action (window,
					    FR_BATCH_ACTION_OPEN_FILES,
					    odata,
					    (GFreeFunc) open_files_data_unref);

	_archive_operation_started (odata->window, FR_ACTION_EXTRACTING_FILES);

	fr_archive_extract (window->archive,
			    odata->file_list,
			    odata->cdata->temp_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    private->password,
			    private->cancellable,
			    open_files_extract_ready_cb,
			    odata);
}


/**/


static GFile *
_get_default_dir (GFile *directory)
{
	if (! _g_file_is_temp_dir (directory))
		return g_object_ref (directory);
	else
		return NULL;
}


void
fr_window_set_open_default_dir (FrWindow *window,
				GFile    *default_dir)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_if_fail (default_dir != NULL);

	_g_object_unref (private->open_default_dir);
	private->open_default_dir = _get_default_dir (default_dir);
}


GFile *
fr_window_get_open_default_dir (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->open_default_dir == NULL)
		return _g_file_get_home ();
	else
		return  private->open_default_dir;
}


void
fr_window_set_add_default_dir (FrWindow *window,
			       GFile    *default_dir)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_if_fail (default_dir != NULL);

	_g_object_unref (private->add_default_dir);
	private->add_default_dir = _get_default_dir (default_dir);
}


GFile *
fr_window_get_add_default_dir (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return  private->add_default_dir;
}


void
fr_window_set_extract_default_dir (FrWindow *window,
				   GFile    *default_dir)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_if_fail (default_dir != NULL);

	_g_object_unref (private->extract_default_dir);
	private->extract_default_dir = _get_default_dir (default_dir);
}


GFile *
fr_window_get_extract_default_dir (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->extract_default_dir == NULL)
		return _g_file_get_home ();
	else
		return  private->extract_default_dir;
}


void
fr_window_set_default_dir (FrWindow *window,
			   GFile    *default_dir,
			   gboolean  freeze)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	g_return_if_fail (default_dir != NULL);

	private->freeze_default_dir = freeze;

	fr_window_set_open_default_dir (window, default_dir);
	fr_window_set_add_default_dir (window, default_dir);
	fr_window_set_extract_default_dir (window, default_dir);
}


void
fr_window_set_folders_visibility (FrWindow   *window,
				  gboolean    value)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	private->view_sidebar = value;
	fr_window_update_dir_tree (window);

	fr_window_set_action_state (window, "view-sidebar", private->view_sidebar);
}


void
fr_window_use_progress_dialog (FrWindow *window,
			       gboolean  value)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->use_progress_dialog = value;
}


/* -- batch mode procedures -- */


static void fr_window_batch_exec_current_action (FrWindow *window);


static void
fr_window_exec_batch_action (FrWindow      *window,
			     FrBatchAction *action)
{
	ExtractData   *edata;
	RenameData    *rdata;
	OpenFilesData *odata;
	ConvertData   *cdata;
	EncryptData   *enc_data;

	switch (action->type) {
	case FR_BATCH_ACTION_LOAD:
		debug (DEBUG_INFO, "[BATCH] LOAD\n");

		if (! g_file_query_exists (G_FILE (action->data), NULL)) {
			GError *error = NULL;

			if (! fr_window_archive_new (window, G_FILE (action->data), NULL))
				error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, _("Archive type not supported."));
			_archive_operation_completed (window, FR_ACTION_CREATING_NEW_ARCHIVE, error);

			_g_error_free (error);
		}
		else
			fr_window_archive_open (window, G_FILE (action->data), GTK_WINDOW (window));
		break;

	case FR_BATCH_ACTION_ADD:
		debug (DEBUG_INFO, "[BATCH] ADD\n");

		fr_window_archive_add_dropped_items (window, (GList *) action->data);
		break;

	case FR_BATCH_ACTION_REMOVE:
		debug (DEBUG_INFO, "[BATCH] REMOVE\n");
		fr_window_archive_remove (window, (GList *) action->data);
		break;

	case FR_BATCH_ACTION_OPEN:
		debug (DEBUG_INFO, "[BATCH] OPEN\n");

		dlg_batch_add_files (window, (GList *) action->data);
		break;

	case FR_BATCH_ACTION_EXTRACT:
		debug (DEBUG_INFO, "[BATCH] EXTRACT\n");

		edata = action->data;
		fr_window_archive_extract (window,
					   edata->file_list,
					   edata->destination,
					   edata->base_dir,
					   edata->skip_older,
					   edata->overwrite,
					   edata->junk_paths,
					   edata->ask_to_open_destination);
		break;

	case FR_BATCH_ACTION_EXTRACT_HERE:
		debug (DEBUG_INFO, "[BATCH] EXTRACT HERE\n");

		edata = action->data;
		fr_window_archive_extract_here (window,
						edata->skip_older,
						edata->overwrite,
						edata->junk_paths);
		break;

	case FR_BATCH_ACTION_EXTRACT_ASK_OPTIONS:
		debug (DEBUG_INFO, "[BATCH] EXTRACT ASK OPTIONS\n");

		dlg_extract (NULL, window);
		break;

	case FR_BATCH_ACTION_RENAME:
		debug (DEBUG_INFO, "[BATCH] RENAME\n");

		rdata = action->data;
		rename_selection (window,
				  rdata->path_to_rename,
				  rdata->old_name,
				  rdata->new_name,
				  rdata->current_dir,
				  rdata->is_dir,
				  rdata->dir_in_archive,
				  rdata->original_path);
		break;

	case FR_BATCH_ACTION_PASTE:
		debug (DEBUG_INFO, "[BATCH] PASTE\n");

		fr_window_paste_from_clipboard_data (window, (FrClipboardData*) action->data);
		break;

	case FR_BATCH_ACTION_OPEN_FILES:
		debug (DEBUG_INFO, "[BATCH] OPEN FILES\n");

		odata = action->data;
		fr_window_open_files (window, odata->file_list, odata->ask_application);
		break;

	case FR_BATCH_ACTION_SAVE_AS:
		debug (DEBUG_INFO, "[BATCH] SAVE_AS\n");

		cdata = action->data;
		fr_window_archive_save_as (window,
					   cdata->file,
					   cdata->mime_type,
					   cdata->password,
					   cdata->encrypt_header,
					   cdata->volume_size);
		break;

	case FR_BATCH_ACTION_TEST:
		debug (DEBUG_INFO, "[BATCH] TEST\n");

		fr_window_archive_test (window);
		break;

	case FR_BATCH_ACTION_ENCRYPT:
		debug (DEBUG_INFO, "[BATCH] ENCRYPT\n");

		enc_data = action->data;
		fr_window_archive_encrypt (window,
					   enc_data->password,
					   enc_data->encrypt_header);
		break;


	case FR_BATCH_ACTION_CLOSE:
		debug (DEBUG_INFO, "[BATCH] CLOSE\n");

		fr_window_archive_close (window);
		fr_window_batch_exec_next_action (window);
		break;

	case FR_BATCH_ACTION_QUIT:
		debug (DEBUG_INFO, "[BATCH] QUIT\n");

		g_signal_emit (window,
			       fr_window_signals[READY],
			       0,
			       NULL);

		{
			FrWindowPrivate *private = fr_window_get_instance_private (window);

			if ((private->progress_dialog != NULL) && (gtk_widget_get_parent (private->progress_dialog) != GTK_WIDGET (window))) {
				gtk_window_destroy (GTK_WINDOW (private->progress_dialog));
				private->progress_dialog = NULL;
			}
		}
		gtk_window_destroy (GTK_WINDOW (window));
		break;

	default:
		break;
	}
}


void
fr_window_set_current_action (FrWindow          *window,
			      FrBatchActionType  action_type,
			      void              *data,
			      GFreeFunc          free_func)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *action;

	fr_window_reset_current_action (window);

	action = &private->current_action;
	action->type = action_type;
	action->data = data;
	action->free_func = free_func;
}


void
fr_window_reset_current_action (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *action = &private->current_action;

	if ((action->data != NULL) && (action->free_func != NULL))
		(*action->free_func) (action->data);
	action->type = FR_BATCH_ACTION_NONE;
	action->data = NULL;
	action->free_func = NULL;
}


void
fr_window_restart_current_action (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	fr_window_exec_batch_action (window, &private->current_action);
}


void
fr_window_batch_append_action (FrWindow          *window,
			       FrBatchActionType  action,
			       void              *data,
			       GFreeFunc          free_func)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *a_desc;

	g_return_if_fail (window != NULL);

	a_desc = g_new0 (FrBatchAction, 1);
	a_desc->type = action;
	a_desc->data = data;
	a_desc->free_func = free_func;

	private->batch_action_list = g_list_append (private->batch_action_list, a_desc);
}


void
fr_window_batch_replace_current_action (FrWindow          *window,
					FrBatchActionType  action,
					void              *data,
					GFreeFunc          free_func)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *a_desc;

	g_return_if_fail (window != NULL);
	g_return_if_fail (private->batch_action != NULL);

	a_desc = g_new0 (FrBatchAction, 1);
	a_desc->type = action;
	a_desc->data = data;
	a_desc->free_func = free_func;

	fr_batch_action_free ((FrBatchAction *) private->batch_action->data);
	private->batch_action->data = a_desc;
}


static void
fr_window_batch_exec_current_action (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *action;

	if (private->batch_action == NULL) {
		fr_window_free_batch_data (window);
		if (private->reload_archive) {
			private->reload_archive = FALSE;
			fr_window_archive_reload (window);
		}
		return;
	}

	action = (FrBatchAction *) private->batch_action->data;
	fr_window_exec_batch_action (window, action);
}


static void
fr_window_batch_exec_next_action (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->batch_action != NULL)
		private->batch_action = g_list_next (private->batch_action);
	else
		private->batch_action = private->batch_action_list;
	fr_window_batch_exec_current_action (window);
}


void
fr_window_batch_start (FrWindow *window)
{
	g_return_if_fail (window != NULL);
	FrWindowPrivate *private = fr_window_get_instance_private (window);

	if (private->batch_mode)
		return;

	if (private->batch_action_list == NULL)
		return;

	if (private->progress_dialog != NULL)
		gtk_window_set_title (GTK_WINDOW (private->progress_dialog),
				      private->batch_title);

	private->batch_mode = TRUE;
	private->batch_action = private->batch_action_list;
	gtk_widget_hide (GTK_WIDGET (window));

	fr_window_batch_exec_current_action (window);
}


void
fr_window_batch_stop (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (! private->batch_mode) {
		fr_window_free_batch_data (window);
		private->reload_archive = FALSE;
		return;
	}

	if (! private->showing_error_dialog) {
		g_signal_emit (window,
			       fr_window_signals[READY],
			       0,
			       NULL);
		gtk_window_destroy (GTK_WINDOW (window));
	}
}


void
fr_window_batch_stop_with_error (FrWindow     *window,
				 FrAction      action,
				 FrErrorType   error_type,
				 const char   *error_message)
{
	GError *error;

	error = g_error_new_literal (FR_ERROR, error_type, error_message);
	_archive_operation_completed (window, action , error);

	g_error_free (error);
}


void
fr_window_batch_resume (FrWindow *window)
{
	fr_window_batch_exec_current_action (window);
}


gboolean
fr_window_is_batch_mode (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return private->batch_mode;
}


FrBatchActionType
fr_window_batch_get_current_action_type (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	FrBatchAction *action;

	if (! private->batch_mode || (private->batch_action == NULL))
		return FR_BATCH_ACTION_NONE;

	action = (FrBatchAction *) private->batch_action->data;
	if (action == NULL)
		return FR_BATCH_ACTION_NONE;

	return action->type;
}


void
fr_window_batch_new (FrWindow   *window,
		     const char *title)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	fr_window_free_batch_data (window);
	g_free (private->batch_title);
	private->batch_title = g_strdup (title);
}


const char *
fr_window_batch_get_title (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	return private->batch_title;
}


void
fr_window_batch__extract_here (FrWindow *window,
			       GFile    *archive,
			       gboolean  ask_to_open_destination)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (archive != NULL);

	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_LOAD,
				       g_object_ref (archive),
				       (GFreeFunc) g_object_unref);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_EXTRACT_HERE,
				       extract_data_new (window,
							 NULL,
							 NULL,
							 NULL,
							 FALSE,
							 FR_OVERWRITE_ASK,
							 FALSE,
							 ask_to_open_destination,
							 TRUE),
				       (GFreeFunc) extract_data_free);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);
}


void
fr_window_batch__extract (FrWindow  *window,
			  GFile     *archive,
			  GFile     *destination,
			  gboolean   ask_to_open_destination)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (archive != NULL);

	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_LOAD,
				       g_object_ref (archive),
				       (GFreeFunc) g_object_unref);
	if (destination != NULL)
		fr_window_batch_append_action (window,
					       FR_BATCH_ACTION_EXTRACT,
					       extract_data_new (window,
								 NULL,
								 destination,
								 NULL,
								 FALSE,
								 FR_OVERWRITE_ASK,
								 FALSE,
								 ask_to_open_destination,
								 FALSE),
					       (GFreeFunc) extract_data_free);
	else
		fr_window_batch_append_action (window,
					       FR_BATCH_ACTION_EXTRACT_ASK_OPTIONS,
					       NULL,
					       NULL);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);
}


void
fr_window_batch__add_files (FrWindow *window,
			    GFile    *archive,
			    GList    *file_list)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	private->batch_adding_one_file = (file_list->next == NULL) && (_g_file_query_is_file (file_list->data));

	if (archive != NULL)
		fr_window_batch_append_action (window,
					       FR_BATCH_ACTION_LOAD,
					       g_object_ref (archive),
					       (GFreeFunc) g_object_unref);
	else
		fr_window_batch_append_action (window,
					       FR_BATCH_ACTION_OPEN,
					       _g_object_list_ref (file_list),
					       (GFreeFunc) _g_object_list_unref);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_ADD,
				       _g_object_list_ref (file_list),
				       (GFreeFunc) _g_object_list_unref);
	fr_window_batch_append_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);
}


void
fr_window_dnd_extraction_finished (FrWindow *window,
				   gboolean  error)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->dnd_extract_is_running == TRUE) {
		private->dnd_extract_is_running = FALSE;
		private->dnd_extract_finished_with_error = error;
	}
}


void
fr_window_extract_archive_and_continue (FrWindow      *window,
					GList         *file_list,
					GFile         *destination,
					const char    *base_dir,
					gboolean       skip_older,
					FrOverwrite    overwrite,
					gboolean       junk_paths)
{
	if (fr_window_batch_get_current_action_type (window) == FR_BATCH_ACTION_EXTRACT_ASK_OPTIONS) {

		/* no need to ask the user the extract options again if the
		 * action is re-executed (for example when asking the password) */

		fr_window_batch_replace_current_action (window,
							FR_BATCH_ACTION_EXTRACT,
							extract_data_new (window,
									  file_list,
									  destination,
									  base_dir,
									  skip_older,
									  FR_OVERWRITE_ASK,
									  junk_paths,
									  _fr_window_get_ask_to_open_destination (window),
									  FALSE),
							(GFreeFunc) extract_data_free);
		fr_window_batch_resume (window);
	}
	else
		fr_window_archive_extract (window,
					   file_list,
					   destination,
					   base_dir,
					   skip_older,
					   FR_OVERWRITE_ASK,
					   junk_paths,
					   _fr_window_get_ask_to_open_destination (window));
}


void
fr_window_focus_location (FrWindow *window)
{
	FrWindowPrivate *private = fr_window_get_instance_private (window);
	if (private->list_mode == FR_WINDOW_LIST_MODE_AS_DIR)
		gtk_widget_grab_focus (private->location_entry);
}
