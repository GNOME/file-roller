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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkcursor.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "actions.h"
#include "dlg-batch-add.h"
#include "dlg-delete.h"
#include "dlg-extract.h"
#include "dlg-open-with.h"
#include "dlg-ask-password.h"
#include "eggtreemultidnd.h"
#include "fr-marshal.h"
#include "fr-list-model.h"
#include "fr-archive.h"
#include "fr-error.h"
#include "fr-stock.h"
#include "fr-window.h"
#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "main.h"
#include "gtk-utils.h"
#include "gconf-utils.h"
#include "typedefs.h"
#include "ui.h"
#include "utf8-fnmatch.h"


#define LAST_OUTPUT_DIALOG_NAME "last_output"
#define MAX_HISTORY_LEN 5
#define ACTIVITY_DELAY 100
#define ACTIVITY_PULSE_STEP (0.033)
#define FILES_TO_PROCESS_AT_ONCE 500
#define DISPLAY_TIMEOUT_INTERVAL_MSECS 300
#define MAX_MESSAGE_LENGTH 50
#define CHECK_CLIPBOARD_TIMEOUT 1000

#define PROGRESS_DIALOG_WIDTH 300
#define PROGRESS_TIMEOUT_MSECS 500     /* FIXME */
#define HIDE_PROGRESS_TIMEOUT_MSECS 500 /* FIXME */
#define NAME_COLUMN_WIDTH 250
#define OTHER_COLUMNS_WIDTH 100
#define RECENT_ITEM_MAX_WIDTH 25

#define DEF_WIN_WIDTH 600
#define DEF_WIN_HEIGHT 480

#define MIME_TYPE_DIRECTORY "application/directory-normal"
#define ICON_TYPE_DIRECTORY "gnome-fs-directory"
#define ICON_TYPE_REGULAR   "gnome-fs-regular"
#define ICON_GTK_SIZE GTK_ICON_SIZE_LARGE_TOOLBAR

#define BAD_CHARS "/\\*"

static GHashTable     *pixbuf_hash = NULL;
static GtkIconTheme   *icon_theme = NULL;
static int             icon_size = 0;

#define XDS_FILENAME "xds.txt"
#define MAX_XDS_ATOM_VAL_LEN 4096
#define XDS_ATOM   gdk_atom_intern  ("XdndDirectSave0", FALSE)
#define TEXT_ATOM  gdk_atom_intern  ("text/plain", FALSE)
#define OCTET_ATOM gdk_atom_intern  ("application/octet-stream", FALSE)
#define XFR_ATOM   gdk_atom_intern  ("XdndFileRoller0", FALSE)

#define FR_CLIPBOARD (gdk_atom_intern_static_string ("_FILE_ROLLER_SPECIAL_CLIPBOARD")) 
#define FR_SPECIAL_URI_LIST (gdk_atom_intern_static_string ("application/file-roller-uri-list"))

static GtkTargetEntry clipboard_targets[] = {
	{ "application/file-roller-uri-list", 0, 1 }
};

static GtkTargetEntry target_table[] = {
	{ "XdndFileRoller0", 0, 0 },
	{ "text/uri-list", 0, 1 },
};

typedef struct {
	FRBatchActionType type;
	void *            data;
	GFreeFunc         free_func;
} FRBatchAction;

typedef struct {
	guint      converting : 1;
	char      *temp_dir;
	FrArchive *new_archive;
} FRConvertData;

typedef enum {
	FR_CLIPBOARD_OP_CUT,
	FR_CLIPBOARD_OP_COPY
} FRClipboardOp;

typedef struct {
	GList    *file_list;
	char     *extract_to_dir;
	char     *base_dir;
	gboolean  skip_older;
	gboolean  overwrite;
	gboolean  junk_paths;
	char     *password;
	gboolean  extract_here;
} ExtractData;


/**/


typedef struct {
	char          *archive_filename;
	char          *archive_password;
	FRClipboardOp  op;
	char          *base_dir;
	GList         *files;
	char          *tmp_dir;
	char          *current_dir;
} FrClipboardData;


static FrClipboardData*
fr_clipboard_data_new (void)
{
	return g_new0 (FrClipboardData, 1);
}


static void
fr_clipboard_data_free (FrClipboardData *clipboard_data)
{
	if (clipboard_data == NULL)
		return;
		
	g_free (clipboard_data->archive_filename);
	g_free (clipboard_data->archive_password);
	g_free (clipboard_data->base_dir);
	g_free (clipboard_data->tmp_dir);
	g_free (clipboard_data->current_dir);
	g_list_foreach (clipboard_data->files, (GFunc) g_free, NULL);
	g_list_free (clipboard_data->files);
	g_free (clipboard_data);
}


/**/

enum {
	ARCHIVE_LOADED,
	LAST_SIGNAL
};

static GnomeAppClass *parent_class = NULL;
static guint fr_window_signals[LAST_SIGNAL] = { 0 };

struct _FrWindowPrivateData {
	GtkWidget *      list_view;
	GtkListStore *   list_store;
	GtkWidget *      toolbar;
	GtkWidget *      statusbar;
	GtkWidget *      progress_bar;
	GtkWidget *      location_bar;
	GtkWidget *      location_entry;
	GtkWidget *      location_label;
	GtkWidget *      up_button;
	GtkWidget *      home_button;
	GtkWidget *      back_button;
	GtkWidget *      fwd_button;
	GtkCellRenderer *name_renderer;
	GtkTreePath     *hover_path;

	gint             current_view_length;

	GtkTooltips     *tooltips;
	guint            help_message_cid;
	guint            list_info_cid;
	guint            progress_cid;

	GtkWidget *      up_arrows[5];
	GtkWidget *      down_arrows[5];

	FRAction         action;
	gboolean         archive_present;
	gboolean         archive_new;        /* A new archive has been created
					      * but it doesn't contain any
					      * file yet.  The real file will
					      * be created only when the user
					      * adds some file to the
					      * archive.*/

	char *           archive_uri;
	char *           open_default_dir;    /* default directory to be used
					       * in the Open dialog. */
	char *           add_default_dir;     /* default directory to be used
					       * in the Add dialog. */
	char *           extract_default_dir; /* default directory to be used
					       * in the Extract dialog. */
	gboolean         freeze_default_dir;
	gboolean         asked_for_password;

	gboolean         view_folder_after_extraction;

	FRBatchAction    current_batch_action;

	gboolean         give_focus_to_the_list;
	gboolean         single_click;
	GtkTreePath     *path_clicked;

	FRWindowSortMethod sort_method;
	GtkSortType      sort_type;

	FRWindowListMode list_mode;
	GList *          history;
	GList *          history_current;
	char *           password;
	char *           password_for_paste;
	FRCompression    compression;

	guint            activity_timeout_handle;   /* activity timeout
						     * handle. */
	gint             activity_ref;              /* when > 0 some activity
						     * is present. */

	guint            update_timeout_handle;     /* update file list
						     * timeout handle. */

	FRConvertData    convert_data;

	gboolean         stoppable;

	FrClipboardData *clipboard_data;
	FrClipboardData *copy_data;
	
	/*
	GList           *clipboard;
	guint            clipboard_op : 1;
	char            *clipboard_current_dir;
	*/
	FrArchive       *copy_from_archive;
	guint            check_clipboard;
	
	GtkActionGroup  *actions;

	GtkRecentManager *recent_manager;
	GtkWidget        *recent_chooser_menu;
	GtkWidget        *recent_chooser_toolbar;

	GtkWidget        *file_popup_menu;
	GtkWidget        *folder_popup_menu;
	GtkWidget        *mitem_recents_menu;
	GtkWidget        *recent_toolbar_menu;
	GtkAction        *open_action;

	/* dragged files data */

	char             *drag_destination_folder;
	GError           *drag_error;
	GList            *drag_file_list;        /* the list of files we are
					 	  * dragging*/

	/* progress dialog data */

	GtkWidget *progress_dialog;
	GtkWidget *pd_action;
	GtkWidget *pd_archive;
	GtkWidget *pd_message;
	GtkWidget *pd_progress_bar;
	gboolean   progress_pulse;
	guint      progress_timeout;  /* Timeout to display the progress dialog. */
	guint      hide_progress_timeout;  /* Timeout to hide the progress dialog. */
	FRAction   pd_last_action;
	char      *pd_last_archive;

	/* batch mode data */

	gboolean   batch_mode;          /* whether we are in a non interactive
					 * mode. */
	GList     *batch_action_list;   /* FRBatchAction * elements */
	GList     *batch_action;        /* current action. */

	guint      cnxn_id[GCONF_NOTIFICATIONS];

	gulong     theme_changed_handler_id;
	gboolean   non_interactive;
	char      *extract_here_dir;
	gboolean   extract_interact_use_default_dir;
	gboolean   update_dropped_files;
	gboolean   batch_adding_one_file;

	GtkWindow *load_error_parent_window;
};


/* -- fr_window_free_private_data -- */


static void
fr_window_remove_notifications (FrWindow *window)
{
	int i;

	for (i = 0; i < GCONF_NOTIFICATIONS; i++)
		if (window->priv->cnxn_id[i] != -1)
			eel_gconf_notification_remove (window->priv->cnxn_id[i]);
}


static void
fr_window_free_batch_data (FrWindow *window)
{
	GList *scan;

	for (scan = window->priv->batch_action_list; scan; scan = scan->next) {
		FRBatchAction *adata = scan->data;

		if ((adata->data != NULL) && (adata->free_func != NULL))
			(*adata->free_func) (adata->data);
		g_free (adata);
	}

	g_list_free (window->priv->batch_action_list);
	window->priv->batch_action_list = NULL;
	window->priv->batch_action = NULL;
}


static void
gh_unref_pixbuf (gpointer key,
		 gpointer value,
		 gpointer user_data)
{
	g_object_unref (value);
}


static void
fr_window_clipboard_remove_file_list (FrWindow *window,
				      GList    *file_list)
{
	GList *scan1;

	if (window->priv->copy_data == NULL)
		return;

	if (file_list == NULL) {
		fr_clipboard_data_free (window->priv->copy_data);
		window->priv->copy_data = NULL;
		return;
	}

	for (scan1 = file_list; scan1; scan1 = scan1->next) {
		const char *name1 = scan1->data;
		GList      *scan2;

		for (scan2 = window->priv->copy_data->files; scan2;) {
			const char *name2 = scan2->data;

			if (strcmp (name1, name2) == 0) {
				GList *tmp = scan2->next;
				window->priv->copy_data->files = g_list_remove_link (window->priv->copy_data->files, scan2);
				g_free (scan2->data);
				g_list_free (scan2);
				scan2 = tmp;
			}
			else
				scan2 = scan2->next;
		}
	}

	if (window->priv->copy_data->files == NULL) {
		fr_clipboard_data_free (window->priv->copy_data);
		window->priv->copy_data = NULL;
	}
}


static void
fr_window_history_clear (FrWindow *window)
{
	if (window->priv->history != NULL)
		path_list_free (window->priv->history);
	window->priv->history = NULL;
	window->priv->history_current = NULL;
}


static void
fr_window_free_private_data (FrWindow *window)
{
	FrWindowPrivateData *priv = window->priv;

	if (priv->update_timeout_handle != 0) {
		g_source_remove (priv->update_timeout_handle);
		priv->update_timeout_handle = 0;
	}

	fr_window_remove_notifications (window);

	if (priv->open_action != NULL) {
		g_object_unref (priv->open_action);
		priv->open_action = NULL;
	}

	if (priv->recent_toolbar_menu != NULL) {
		gtk_widget_destroy (priv->recent_toolbar_menu);
		priv->recent_toolbar_menu = NULL;
	}

	g_object_unref (G_OBJECT (priv->tooltips));

	while (priv->activity_ref > 0)
		fr_window_stop_activity_mode (window);

	if (priv->progress_timeout != 0) {
		g_source_remove (priv->progress_timeout);
		priv->progress_timeout = 0;
	}

	if (priv->hide_progress_timeout != 0) {
		g_source_remove (priv->hide_progress_timeout);
		priv->hide_progress_timeout = 0;
	}

	if (priv->theme_changed_handler_id != 0)
		g_signal_handler_disconnect (icon_theme, priv->theme_changed_handler_id);

	fr_window_history_clear (window);

	if (priv->open_default_dir != NULL)
		g_free (priv->open_default_dir);
	if (priv->add_default_dir != NULL)
		g_free (priv->add_default_dir);
	if (priv->extract_default_dir != NULL)
		g_free (priv->extract_default_dir);
	if (priv->archive_uri != NULL)
		g_free (priv->archive_uri);

	if (priv->password != NULL)
		g_free (priv->password);
	if (priv->password_for_paste != NULL)
		g_free (priv->password_for_paste);
		
	g_object_unref (priv->list_store);

	if (window->priv->clipboard_data != NULL) {
		fr_clipboard_data_free (window->priv->clipboard_data);
		window->priv->clipboard_data = NULL;
	}
	if (window->priv->copy_data != NULL) {
		fr_clipboard_data_free (window->priv->copy_data);
		window->priv->copy_data = NULL;
	}
	if (priv->copy_from_archive != NULL) {
		g_object_unref (priv->copy_from_archive);
		priv->copy_from_archive = NULL;
	}
	if (priv->check_clipboard != 0) {
		g_source_remove (priv->check_clipboard);
		priv->check_clipboard = 0;
	}
	
	g_clear_error (&priv->drag_error);
	path_list_free (priv->drag_file_list);
	priv->drag_file_list = NULL;

	if (priv->file_popup_menu != NULL) {
		gtk_widget_destroy (priv->file_popup_menu);
		priv->file_popup_menu = NULL;
	}

	if (priv->folder_popup_menu != NULL) {
		gtk_widget_destroy (priv->folder_popup_menu);
		priv->folder_popup_menu = NULL;
	}

	fr_window_free_batch_data (window);
	fr_window_reset_current_batch_action (window);

	g_free (priv->pd_last_archive);
	g_free (priv->extract_here_dir);

	preferences_set_sort_method (priv->sort_method);
	preferences_set_sort_type (priv->sort_type);
	preferences_set_list_mode (priv->list_mode);
}


static void
fr_window_finalize (GObject *object)
{
	FrWindow *window = FR_WINDOW (object);

	if (window->archive != NULL) {
		g_object_unref (window->archive);
		window->archive = NULL;
	}
		
	if (window->priv != NULL) {
		fr_window_free_private_data (window);
		g_free (window->priv);
		window->priv = NULL;
	}

	WindowList = g_list_remove (WindowList, window);

	G_OBJECT_CLASS (parent_class)->finalize (object);

	if (WindowList == NULL) {
		if (pixbuf_hash != NULL) {
			g_hash_table_foreach (pixbuf_hash,
					      gh_unref_pixbuf,
					      NULL);
			g_hash_table_destroy (pixbuf_hash);
		}

		gtk_main_quit ();
	}
}


void
fr_window_close (FrWindow *window)
{
	if (GTK_WIDGET_REALIZED (window)) {
		int width, height;
		
		gdk_drawable_get_size (GTK_WIDGET (window)->window, &width, &height);
		eel_gconf_set_integer (PREF_UI_WINDOW_WIDTH, width);
		eel_gconf_set_integer (PREF_UI_WINDOW_HEIGHT, height);
	}
	
	if (window->priv->check_clipboard != 0) {
		g_source_remove (window->priv->check_clipboard);
		window->priv->check_clipboard = 0;
	}
	
	gtk_widget_destroy (GTK_WIDGET (window));
}


static void
fr_window_class_init (FrWindowClass *class)
{
	GObjectClass   *gobject_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (class);

	fr_window_signals[ARCHIVE_LOADED] =
		g_signal_new ("archive-loaded",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrWindowClass, archive_loaded),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE, 1,
			      G_TYPE_INT);

	gobject_class = (GObjectClass*) class;
	gobject_class->finalize = fr_window_finalize;

	widget_class = (GtkWidgetClass*) class;
}


static void
fr_window_init (FrWindow *window)
{
	window->priv = g_new0 (FrWindowPrivateData, 1);
	window->priv->update_dropped_files = FALSE;

	WindowList = g_list_prepend (WindowList, window);
}


GType
fr_window_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo type_info = {
			sizeof (FrWindowClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_window_class_init,
			NULL,
			NULL,
			sizeof (FrWindow),
			0,
			(GInstanceInitFunc) fr_window_init
		};

		type = g_type_register_static (GNOME_TYPE_APP,
					       "FrWindow",
					       &type_info,
					       0);
	}

	return type;
}


/* -- window history -- */


static void
fr_window_history_add (FrWindow   *window,
		       const char *path)
{
	if (window->priv->history != NULL) {
		char *first_path = (char*) window->priv->history->data;
		if (strcmp (first_path, path) == 0) {
			window->priv->history_current = window->priv->history;
			return;
		}

		/* Add locations visited using the back button to the history
		 * list. */
		if ((window->priv->history_current != NULL)
		    && (window->priv->history != window->priv->history_current)) {
			GList *scan = window->priv->history->next;
			while (scan != window->priv->history_current->next) {
				window->priv->history = g_list_prepend (window->priv->history, g_strdup (scan->data));
				scan = scan->next;
			}
		}
	}

	window->priv->history = g_list_prepend (window->priv->history, g_strdup (path));
	window->priv->history_current = window->priv->history;
}


static void
fr_window_history_pop (FrWindow *window)
{
	GList *first;

	if (window->priv->history == NULL)
		return;

	first = window->priv->history;
	window->priv->history = g_list_remove_link (window->priv->history, first);
	if (window->priv->history_current == first)
		window->priv->history_current = window->priv->history;
	g_free (first->data);
	g_list_free (first);
}


#if 0
static void
fr_window_history_print (FrWindow *window)
{
	GList *list;

	debug (DEBUG_INFO, "history:\n");
	for (list = window->priv->history; list; list = list->next)
		debug (DEBUG_INFO, "\t%s %s\n",
			 (char*) list->data,
			 (list == window->priv->history_current)? "<-": "");
	debug (DEBUG_INFO, "\n");
}
#endif


/* -- window_update_file_list -- */


static GList *
fr_window_get_current_dir_list (FrWindow *window)
{
	GList *dir_list = NULL;
	GList *scan;

	for (scan = window->archive->command->file_list; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		
		if (fdata->list_name == NULL)
			continue;
		dir_list = g_list_prepend (dir_list, fdata);
	}

	return g_list_reverse (dir_list);
}


static gint
sort_by_name (gconstpointer  ptr1,
	      gconstpointer  ptr2)
{
	const FileData *fdata1 = ptr1, *fdata2 = ptr2;

	if (file_data_is_dir (fdata1) != file_data_is_dir (fdata2)) {
		if (file_data_is_dir (fdata1))
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

	if (file_data_is_dir (fdata1) != file_data_is_dir (fdata2)) {
		if (file_data_is_dir (fdata1))
			return -1;
		else
			return 1;
	}
	else if (file_data_is_dir (fdata1) && file_data_is_dir (fdata2))
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

	if (file_data_is_dir (fdata1) != file_data_is_dir (fdata2)) {
		if (file_data_is_dir (fdata1))
			return -1;
		else
			return 1;
	}
	else if (file_data_is_dir (fdata1) && file_data_is_dir (fdata2))
		return sort_by_name (ptr1, ptr2);

	desc1 = file_data_get_mime_type_description (fdata1);
	desc2 = file_data_get_mime_type_description (fdata2);

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

	if (file_data_is_dir (fdata1) != file_data_is_dir (fdata2)) {
		if (file_data_is_dir (fdata1))
			return -1;
		else
			return 1;
	}
	else if (file_data_is_dir (fdata1) && file_data_is_dir (fdata2))
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

	if (file_data_is_dir (fdata1) != file_data_is_dir (fdata2)) {
		if (file_data_is_dir (fdata1))
			return -1;
		else
			return 1;
	}
	else if (file_data_is_dir (fdata1) && file_data_is_dir (fdata2))
		return sort_by_name (ptr1, ptr2);

	result = strcasecmp (fdata1->path, fdata2->path);
	if (result == 0)
		return sort_by_name (ptr1, ptr2);
	else
		return result;
}


static GCompareFunc
get_compare_func_from_idx (int column_index)
{
	static GCompareFunc compare_funcs[] = {
		sort_by_name,
		sort_by_type,
		sort_by_size,
		sort_by_time,
		sort_by_path
	};

	column_index = CLAMP (column_index, 0, G_N_ELEMENTS (compare_funcs) - 1);

	return compare_funcs [column_index];
}


static void
compute_file_list_name (FrWindow   *window,
			FileData   *fdata,
			const char *current_dir,
			int         current_dir_len,
			GHashTable *names_hash)
{

	register char *scan, *end;

	g_free (fdata->list_name);
	fdata->list_name = NULL;
	fdata->list_dir = FALSE;

	if (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fdata->list_name = g_strdup (fdata->name);
		return;
	}

	if (strncmp (fdata->full_path, current_dir, current_dir_len) != 0)
		return;

	if (strlen (fdata->full_path) == current_dir_len)
		return;

	scan = fdata->full_path + current_dir_len;
	end = strchr (scan, '/');
	if ((end == NULL) && ! fdata->dir) /* file */
		fdata->list_name = g_strdup (scan);
	else { /* folder */
		char *dir_name;

		if (end != NULL)
			dir_name = g_strndup (scan, end - scan);
		else
			dir_name = g_strdup (scan);

		/* avoid to insert duplicated folders */
		if (g_hash_table_lookup (names_hash, dir_name) != NULL) {
			g_free (dir_name);
			return;
		}
		g_hash_table_insert (names_hash, dir_name, GINT_TO_POINTER (1));

		if (! fdata->dir)
			fdata->list_dir = TRUE;

		fdata->list_name = dir_name;
	}
}


static void
fr_window_compute_list_names (FrWindow *window,
			      GList    *file_list)
{
	const char *current_dir;
	int         current_dir_len;
	GHashTable *names_hash;
	GList      *scan;

	current_dir = fr_window_get_current_location (window);
	current_dir_len = strlen (current_dir);
	names_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (scan = file_list; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		compute_file_list_name (window, fdata, current_dir, current_dir_len, names_hash);
	}

	g_hash_table_destroy (names_hash);
}


static gboolean
fr_window_dir_exists_in_archive (FrWindow   *window,
				 const char *dir_name)
{
	int    dir_name_len;
	GList *scan;

	if (dir_name == NULL)
		return FALSE;

	dir_name_len = strlen (dir_name);
	if (dir_name_len == 0)
		return TRUE;

	if (strcmp (dir_name, "/") == 0)
		return TRUE;

	for (scan = window->archive->command->file_list; scan; scan = scan->next) {
		FileData *fdata = scan->data;
		if (strncmp (dir_name, fdata->full_path, dir_name_len) == 0)
			return TRUE;
	}

	return FALSE;
}


static void
fr_window_sort_file_list (FrWindow  *window,
			  GList    **file_list)
{
	*file_list = g_list_sort (*file_list, get_compare_func_from_idx (window->priv->sort_method));
	if (window->priv->sort_type == GTK_SORT_ASCENDING)
		*file_list = g_list_reverse (*file_list);
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
	new_dir = remove_level_from_path (dir);
	g_free (dir);

	if (new_dir[strlen (new_dir) - 1] == '/')
		retval = new_dir;
	else {
		retval = g_strconcat (new_dir, "/", NULL);
		g_free (new_dir);
	}

	return retval;
}


static void fr_window_update_statusbar_list_info (FrWindow *window);


static GdkPixbuf *
get_icon (GtkWidget *widget,
	  FileData  *fdata)
{
	GdkPixbuf   *pixbuf = NULL;
	char        *icon_name = NULL;
	gboolean     free_icon_name = FALSE;
	const char  *mime_type;

	if (file_data_is_dir (fdata))
		mime_type = MIME_TYPE_DIRECTORY;
	else
		mime_type = file_data_get_mime_type (fdata);

	/* look in the hash table. */

	pixbuf = g_hash_table_lookup (pixbuf_hash, mime_type);
	if (pixbuf != NULL) {
		g_object_ref (G_OBJECT (pixbuf));
		return pixbuf;
	}

	if (file_data_is_dir (fdata))
		icon_name = ICON_TYPE_DIRECTORY;
	else if (! eel_gconf_get_boolean (PREF_LIST_USE_MIME_ICONS, TRUE))
		icon_name = ICON_TYPE_REGULAR;
	else {
		icon_name = gnome_icon_lookup (icon_theme,
	                                       NULL,
	                                       NULL,
	                                       NULL,
	                                       NULL,
        	                               mime_type,
                	                       GNOME_ICON_LOOKUP_FLAGS_NONE,
                        	               NULL);
		free_icon_name = TRUE;
	}
	
	pixbuf = gtk_icon_theme_load_icon (icon_theme,
					   icon_name,
					   icon_size,
					   0,
					   NULL);
						
	if (free_icon_name)
		g_free (icon_name);						
						
	if (pixbuf == NULL)
		return NULL;

	pixbuf = gdk_pixbuf_copy (pixbuf);
	g_hash_table_insert (pixbuf_hash, (gpointer) mime_type, pixbuf);
	g_object_ref (G_OBJECT (pixbuf));

	return pixbuf;
}


static GdkPixbuf *
get_emblem (GtkWidget *widget,
	    FileData  *fdata)
{
	GdkPixbuf *pixbuf = NULL;

	if (! fdata->encrypted)
		return NULL;

	/* encrypted */

	pixbuf = g_hash_table_lookup (pixbuf_hash, "emblem-nowrite");
	if (pixbuf != NULL) {
		g_object_ref (G_OBJECT (pixbuf));
		return pixbuf;
	}
	
	pixbuf = gtk_icon_theme_load_icon (icon_theme,
					   "emblem-nowrite",
					   icon_size,
					   0,
					   NULL);
	if (pixbuf == NULL)
		return NULL;
		
	pixbuf = gdk_pixbuf_copy (pixbuf);
	g_hash_table_insert (pixbuf_hash, (gpointer) "emblem-nowrite", pixbuf);
	g_object_ref (G_OBJECT (pixbuf));
	
	return pixbuf;
}


static int
get_column_from_sort_method (FRWindowSortMethod sort_method)
{
	switch (sort_method) {
	case FR_WINDOW_SORT_BY_NAME: return COLUMN_NAME;
	case FR_WINDOW_SORT_BY_SIZE: return COLUMN_SIZE;
	case FR_WINDOW_SORT_BY_TYPE: return COLUMN_TYPE;
	case FR_WINDOW_SORT_BY_TIME: return COLUMN_TIME;
	case FR_WINDOW_SORT_BY_PATH: return COLUMN_PATH;
	default:
		break;
	}

	return COLUMN_NAME;
}


static int
get_sort_method_from_column (int column_id)
{
	switch (column_id) {
	case COLUMN_NAME: return FR_WINDOW_SORT_BY_NAME;
	case COLUMN_SIZE: return FR_WINDOW_SORT_BY_SIZE;
	case COLUMN_TYPE: return FR_WINDOW_SORT_BY_TYPE;
	case COLUMN_TIME: return FR_WINDOW_SORT_BY_TIME;
	case COLUMN_PATH: return FR_WINDOW_SORT_BY_PATH;
	default:
		break;
	}

	return FR_WINDOW_SORT_BY_NAME;
}


static const char *
get_action_from_sort_method (FRWindowSortMethod sort_method)
{
	switch (sort_method) {
	case FR_WINDOW_SORT_BY_NAME: return "SortByName";
	case FR_WINDOW_SORT_BY_SIZE: return "SortBySize";
	case FR_WINDOW_SORT_BY_TYPE: return "SortByType";
	case FR_WINDOW_SORT_BY_TIME: return "SortByDate";
	case FR_WINDOW_SORT_BY_PATH: return "SortByLocation";
	default:
		break;
	}

	return "SortByName";
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
	if (! fdata->list_dir)
		*list = g_list_prepend (*list, fdata);
}


static GList *
get_selection_as_fd (FrWindow *window)
{
	GtkTreeSelection *selection;
	GList            *list = NULL;

	if (! GTK_WIDGET_REALIZED (window->priv->list_view))
		return NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected_fd, &list);

	return list;
}


static void
fr_window_update_statusbar_list_info (FrWindow *window)
{
	char             *info, *archive_info, *selected_info;
	char             *size_txt, *sel_size_txt;
	int               tot_n, sel_n;
	GnomeVFSFileSize  tot_size, sel_size;
	GList            *scan;
	
	if (window == NULL)
		return;

	if ((window->archive == NULL) || (window->archive->command == NULL)) {
		gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar), window->priv->list_info_cid);
		return;
	}

	tot_n = 0;
	tot_size = 0;

	if (window->priv->archive_present) {
		GList *dir_list = fr_window_get_current_dir_list (window);
		
		for (scan = dir_list; scan; scan = scan->next) {
			FileData *fd = scan->data;
			
			tot_n++;
			if (! file_data_is_dir (fd)) 
				tot_size += fd->size;
		}
		g_list_free (dir_list);
	}

	sel_n = 0;
	sel_size = 0;

	if (window->priv->archive_present) {
		GList *selection = get_selection_as_fd (window);
		
		for (scan = selection; scan; scan = scan->next) {
			FileData *fd = scan->data;
			
			sel_n++;
			if (! file_data_is_dir (fd)) 
				sel_size += fd->size;
		}
		g_list_free (selection);
	}

	size_txt = gnome_vfs_format_file_size_for_display (tot_size);
	sel_size_txt = gnome_vfs_format_file_size_for_display (sel_size);

	if (tot_n == 0)
		archive_info = g_strdup ("");
	else
		archive_info = g_strdup_printf (ngettext ("%d object (%s)", "%d objects (%s)", tot_n), tot_n, size_txt);

	if (sel_n == 0)
		selected_info = g_strdup ("");
	else
		selected_info = g_strdup_printf (ngettext ("%d object selected (%s)", "%d objects selected (%s)", sel_n), sel_n, sel_size_txt);

	info = g_strconcat (archive_info,
			    ((sel_n == 0) ? NULL : ", "),
			    selected_info,
			    NULL);

	gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar), window->priv->list_info_cid, info);

	g_free (size_txt);
	g_free (sel_size_txt);
	g_free (archive_info);
	g_free (selected_info);
	g_free (info);
}


typedef struct {
	FrWindow *window;
	GList    *file_list;
} UpdateData;


static void
update_data_free (gpointer callback_data)
{
	UpdateData *data = callback_data;
	FrWindow   *window = data->window;

	g_return_if_fail (data != NULL);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->priv->list_store), get_column_from_sort_method (window->priv->sort_method), window->priv->sort_type);
	gtk_tree_view_set_model (GTK_TREE_VIEW (window->priv->list_view),
				 GTK_TREE_MODEL (window->priv->list_store));
	fr_window_stop_activity_mode (window);
	fr_window_update_statusbar_list_info (window);

	if (data->file_list != NULL)
		g_list_free (data->file_list);

	g_free (data);
}


static gboolean
update_file_list_idle (gpointer callback_data)
{
	UpdateData *data = callback_data;
	FrWindow   *window = data->window;
	GList      *file_list;
	GList      *scan;
	int         i;
	int         n = FILES_TO_PROCESS_AT_ONCE;

	if (window->priv->update_timeout_handle != 0) {
		g_source_remove (window->priv->update_timeout_handle);
		window->priv->update_timeout_handle = 0;
	}

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
		GdkPixbuf   *icon, *emblem;
		char        *utf8_name;

		if (fdata->list_name == NULL)
			continue;

		gtk_list_store_prepend (window->priv->list_store, &iter);

		icon = get_icon (GTK_WIDGET (window), fdata);
		utf8_name = g_filename_display_name (fdata->list_name);
		emblem = get_emblem (GTK_WIDGET (window), fdata); 
				
		if (file_data_is_dir (fdata)) {
			char *utf8_path;
			char *tmp;

			if (fdata->dir)
				tmp = remove_level_from_path (fdata->path);
			else
				tmp = remove_ending_separator (fr_window_get_current_location (window));
			utf8_path = g_filename_display_name (tmp);
			g_free (tmp);

			gtk_list_store_set (window->priv->list_store, &iter,
					    COLUMN_FILE_DATA, fdata,
					    COLUMN_ICON, icon,
					    COLUMN_NAME, utf8_name,
					    COLUMN_EMBLEM, emblem,
					    COLUMN_TYPE, _("Folder"),
					    COLUMN_SIZE, "",
					    COLUMN_TIME, "",
					    COLUMN_PATH, utf8_path,
					    -1);
			g_free (utf8_path);
		}
		else {
			char       *s_size;
			char       *s_time;
			const char *desc;
			char       *utf8_path;

			s_size = gnome_vfs_format_file_size_for_display (fdata->size);
			s_time = get_time_string (fdata->modified);
			desc = file_data_get_mime_type_description (fdata);

			utf8_path = g_filename_display_name (fdata->path);

			gtk_list_store_set (window->priv->list_store, &iter,
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
		}
		g_free (utf8_name);
		if (icon != NULL)
			g_object_unref (icon);
		if (emblem != NULL)
			g_object_unref (emblem);
	}

	if (gtk_events_pending ())
		gtk_main_iteration_do (TRUE);

	g_list_free (file_list);

	if (data->file_list == NULL) {
		update_data_free (data);
		return FALSE;
	}
	else
		window->priv->update_timeout_handle = g_timeout_add (DISPLAY_TIMEOUT_INTERVAL_MSECS,
								     update_file_list_idle,
								     data);

	return FALSE;
}


void
fr_window_update_file_list (FrWindow *window)
{
	GList      *dir_list = NULL;
	GList      *file_list;
	UpdateData *udata;

	if (GTK_WIDGET_REALIZED (window->priv->list_view))
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (window->priv->list_view), 0, 0);

	if (! window->priv->archive_present || window->priv->archive_new) {
		gtk_list_store_clear (window->priv->list_store);
		window->priv->current_view_length = 0;

		if (window->priv->archive_new) {
			gtk_widget_set_sensitive (window->priv->list_view, TRUE);
			gtk_widget_show_all (window->priv->list_view->parent);
		}
		else {
			gtk_widget_set_sensitive (window->priv->list_view, FALSE);
			gtk_widget_hide_all (window->priv->list_view->parent);
		}

		return;
	}
	else
		gtk_widget_set_sensitive (window->priv->list_view, TRUE);

	if (window->priv->give_focus_to_the_list) {
		gtk_widget_grab_focus (window->priv->list_view);
		window->priv->give_focus_to_the_list = FALSE;
	}

	gtk_list_store_clear (window->priv->list_store);
	if (! GTK_WIDGET_VISIBLE (window->priv->list_view))
		gtk_widget_show_all (window->priv->list_view->parent);

	fr_window_start_activity_mode (window);

	if (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fr_window_compute_list_names (window, window->archive->command->file_list);
		fr_window_sort_file_list (window, &window->archive->command->file_list);
		file_list = window->archive->command->file_list;
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

		fr_window_compute_list_names (window, window->archive->command->file_list);
		dir_list = fr_window_get_current_dir_list (window);

		g_free (current_dir);

		fr_window_sort_file_list (window, &dir_list);
		file_list = dir_list;
	}

	window->priv->current_view_length = g_list_length (file_list);

	udata = g_new0 (UpdateData, 1);
	udata->window = window;
	udata->file_list = g_list_copy (file_list);

	update_file_list_idle (udata);

	if (dir_list != NULL)
		g_list_free (dir_list);

	fr_window_update_statusbar_list_info (window);
}


void
fr_window_update_list_order (FrWindow *window)
{
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->priv->list_store), get_column_from_sort_method (window->priv->sort_method), window->priv->sort_type);
}


static void
fr_window_update_title (FrWindow *window)
{
	if (! window->priv->archive_present)
		gtk_window_set_title (GTK_WINDOW (window), _("Archive Manager"));
	else {
		char *title;
		char *name;

		name = gnome_vfs_unescape_string_for_display (file_name_from_path (fr_window_get_archive_uri (window)));
		title = g_strdup_printf ("%s %s",
					 name,
					 window->archive->read_only ? _("[read only]") : "");

		gtk_window_set_title (GTK_WINDOW (window), title);
		g_free (title);
		g_free (name);
	}
}


static void
check_whether_has_a_dir (GtkTreeModel *model,
			 GtkTreePath  *path,
			 GtkTreeIter  *iter,
			 gpointer      data)
{
	gboolean *has_a_dir = data;
	FileData *fdata;

	gtk_tree_model_get (model, iter,
			    COLUMN_FILE_DATA, &fdata,
			    -1);
	if (file_data_is_dir (fdata))
		*has_a_dir = TRUE;
}


static gboolean
selection_has_a_dir (FrWindow *window)
{
	GtkTreeSelection *selection;
	gboolean          has_a_dir = FALSE;

	if (! GTK_WIDGET_REALIZED (window->priv->list_view))
		return FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return FALSE;

	gtk_tree_selection_selected_foreach (selection,
					     check_whether_has_a_dir,
					     &has_a_dir);

	return has_a_dir;
}


static void
set_active (FrWindow   *window,
	    const char *action_name,
	    gboolean    is_active)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->priv->actions, action_name);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), is_active);
}




static void
set_sensitive (FrWindow   *window,
	       const char *action_name,
	       gboolean    sensitive)
{
	GtkAction *action;
	action = gtk_action_group_get_action (window->priv->actions, action_name);
	g_object_set (action, "sensitive", sensitive, NULL);
}


static gboolean
check_clipboard_cb (gpointer data)
{
	FrWindow     *window = data;
	GtkClipboard *clipboard;
	gboolean      running;
	gboolean      no_archive;
	gboolean      can_modify;
	gboolean      ro;
	gboolean      compr_file;
		
	running    = window->priv->activity_ref > 0;
	no_archive = (window->archive == NULL) || ! window->priv->archive_present;
	ro         = ! no_archive && window->archive->read_only;
	can_modify        = (window->archive != NULL) && (window->archive->command != NULL) && window->archive->command->propCanModify;
	compr_file = ! no_archive && window->archive->is_compressed_file;
	
	clipboard = gtk_clipboard_get (FR_CLIPBOARD);
	set_sensitive (window, "Paste", ! no_archive && ! ro && ! running && ! compr_file && can_modify && (window->priv->list_mode != FR_WINDOW_LIST_MODE_FLAT) && gtk_clipboard_wait_is_target_available (clipboard, FR_SPECIAL_URI_LIST));	
	
	return TRUE;
}


static void
fr_window_update_sensitivity (FrWindow *window)
{
	gboolean no_archive;
	gboolean ro;
	gboolean can_modify;
	gboolean file_op;
	gboolean running;
	gboolean compr_file;
	gboolean sel_not_null;
	gboolean one_file_selected;
	gboolean dir_selected;
	int      n_selected;

	if (window->priv->batch_mode)
		return;

	running           = window->priv->activity_ref > 0;
	no_archive        = (window->archive == NULL) || ! window->priv->archive_present;
	ro                = ! no_archive && window->archive->read_only;
	can_modify        = (window->archive != NULL) && (window->archive->command != NULL) && window->archive->command->propCanModify;
	file_op           = ! no_archive && ! window->priv->archive_new  && ! running;
	compr_file        = ! no_archive && window->archive->is_compressed_file;
	n_selected        = fr_window_get_n_selected_files (window);
	sel_not_null      = n_selected > 0;
	one_file_selected = n_selected == 1;
	dir_selected      = selection_has_a_dir (window);

	set_sensitive (window, "AddFiles", ! no_archive && ! ro && ! running && ! compr_file && can_modify);
	set_sensitive (window, "AddFiles_Toolbar", ! no_archive && ! ro && ! running && ! compr_file && can_modify);
	set_sensitive (window, "AddFolder", ! no_archive && ! ro && ! running && ! compr_file && can_modify);
	set_sensitive (window, "AddFolder_Toolbar", ! no_archive && ! ro && ! running && ! compr_file && can_modify);
	set_sensitive (window, "Copy", ! no_archive && ! ro && ! running && ! compr_file && can_modify && sel_not_null && (window->priv->list_mode != FR_WINDOW_LIST_MODE_FLAT));
	set_sensitive (window, "Cut", ! no_archive && ! ro && ! running && ! compr_file && can_modify && sel_not_null && (window->priv->list_mode != FR_WINDOW_LIST_MODE_FLAT));
	set_sensitive (window, "Delete", ! no_archive && ! ro && ! window->priv->archive_new && ! running && ! compr_file && can_modify);
	set_sensitive (window, "DeselectAll", ! no_archive && sel_not_null);
	set_sensitive (window, "Extract", file_op);
	set_sensitive (window, "Extract_Toolbar", file_op);
	set_sensitive (window, "LastOutput", ((window->archive != NULL)
					      && (window->archive->process != NULL)
					      && (window->archive->process->raw_output != NULL)));
	set_sensitive (window, "New", ! running);
	set_sensitive (window, "Open", ! running);
	set_sensitive (window, "Open_Toolbar", ! running);
	set_sensitive (window, "OpenSelection", file_op && sel_not_null && ! dir_selected);
	set_sensitive (window, "OpenFolder", file_op && one_file_selected && dir_selected);
	set_sensitive (window, "Password", ! running && (window->priv->asked_for_password || (! no_archive && window->archive->command->propPassword)));
	set_sensitive (window, "Properties", file_op);
	set_sensitive (window, "Close", !running || window->priv->stoppable);
	set_sensitive (window, "Reload", ! (no_archive || running));
	set_sensitive (window, "Rename", ! no_archive && ! ro && ! running && ! compr_file && can_modify && one_file_selected);
	set_sensitive (window, "SaveAs", ! no_archive && ! compr_file && ! running);
	set_sensitive (window, "SelectAll", ! no_archive);
	set_sensitive (window, "Stop", running && window->priv->stoppable);
	set_sensitive (window, "TestArchive", ! no_archive && ! running && window->archive->command->propTest);
	set_sensitive (window, "ViewSelection", file_op && one_file_selected && ! dir_selected);
	set_sensitive (window, "ViewSelection_Toolbar", file_op && one_file_selected && ! dir_selected);

	if (window->priv->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->priv->progress_dialog),
						   GTK_RESPONSE_OK,
						   running && window->priv->stoppable);


	set_sensitive (window, "SelectAll", (window->priv->current_view_length > 0) && (window->priv->current_view_length != n_selected));
	set_sensitive (window, "DeselectAll", n_selected > 0);
	set_sensitive (window, "OpenRecentMenu", ! running);
	
	/**/
	
	if (window->priv->check_clipboard == 0)
		window->priv->check_clipboard = g_timeout_add (CHECK_CLIPBOARD_TIMEOUT, check_clipboard_cb, window);
}


static gboolean
location_entry_key_press_event_cb (GtkWidget   *widget,
				   GdkEventKey *event,
				   FrWindow    *window)
{
	if ((event->keyval == GDK_Return)
	    || (event->keyval == GDK_KP_Enter)
	    || (event->keyval == GDK_ISO_Enter))
		fr_window_go_to_location (window, gtk_entry_get_text (GTK_ENTRY (window->priv->location_entry)));

	return FALSE;
}


static void
fr_window_update_current_location (FrWindow *window)
{
	const char *current_dir = fr_window_get_current_location (window);

	if (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		gtk_widget_hide (window->priv->location_bar);
		return;
	}

	gtk_widget_show (window->priv->location_bar);

	gtk_entry_set_text (GTK_ENTRY (window->priv->location_entry), window->priv->archive_present? current_dir: "");
	gtk_widget_set_sensitive (window->priv->home_button, window->priv->archive_present);
	gtk_widget_set_sensitive (window->priv->up_button, window->priv->archive_present && (current_dir != NULL) && (strcmp (current_dir, "/") != 0));
	gtk_widget_set_sensitive (window->priv->back_button, window->priv->archive_present && (current_dir != NULL) && (window->priv->history_current != NULL) && (window->priv->history_current->next != NULL));
	gtk_widget_set_sensitive (window->priv->fwd_button, window->priv->archive_present && (current_dir != NULL) && (window->priv->history_current != NULL) && (window->priv->history_current->prev != NULL));
	gtk_widget_set_sensitive (window->priv->location_entry, window->priv->archive_present);
	gtk_widget_set_sensitive (window->priv->location_label, window->priv->archive_present);

#if 0
	fr_window_history_print (window);
#endif
}


static gboolean
real_close_progress_dialog (gpointer data)
{
	FrWindow *window = data;

	if (window->priv->hide_progress_timeout != 0) {
		g_source_remove (window->priv->hide_progress_timeout);
		window->priv->hide_progress_timeout = 0;
	}

	if (window->priv->progress_dialog != NULL)
		gtk_widget_hide (window->priv->progress_dialog);

	return FALSE;
}


static void
close_progress_dialog (FrWindow *window)
{
	if (window->priv->progress_timeout != 0) {
		g_source_remove (window->priv->progress_timeout);
		window->priv->progress_timeout = 0;
	}

	if (! window->priv->batch_mode && GTK_WIDGET_MAPPED (window))
		gtk_widget_hide (window->priv->progress_bar);

	if (window->priv->hide_progress_timeout != 0)
		return;

	if (window->priv->progress_dialog != NULL)
		window->priv->hide_progress_timeout = g_timeout_add (HIDE_PROGRESS_TIMEOUT_MSECS,
								     real_close_progress_dialog,
								     window);
}


static gboolean
progress_dialog_delete_event (GtkWidget *caller,
			      GdkEvent  *event,
			      FrWindow  *window)
{
	if (window->priv->stoppable) {
		activate_action_stop (NULL, window);
		close_progress_dialog (window);
	}

	return TRUE;
}


static void
progress_dialog_response (GtkDialog *dialog,
			  int        response_id,
			  FrWindow  *window)
{
	if ((response_id == GTK_RESPONSE_CANCEL) && window->priv->stoppable) {
		activate_action_stop (NULL, window);
		close_progress_dialog (window);
	}
}


static const char*
get_message_from_action (FRAction action)
{
	char *message = "";

	switch (action) {
	case FR_ACTION_CREATING_NEW_ARCHIVE:
		message = _("Creating archive");
		break;
	case FR_ACTION_LOADING_ARCHIVE:
		message = _("Loading archive");
		break;
	case FR_ACTION_LISTING_CONTENT:
		message = _("Reading archive");
		break;
	case FR_ACTION_DELETING_FILES:
		message = _("Deleting files from archive");
		break;
	case FR_ACTION_TESTING_ARCHIVE:
		message = _("Testing archive");
		break;
	case FR_ACTION_GETTING_FILE_LIST:
		message = _("Getting the file list");
		break;
	case FR_ACTION_COPYING_FILES_FROM_REMOTE:
		message = _("Copying the file list");
		break;
	case FR_ACTION_ADDING_FILES:
		message = _("Adding files to archive");
		break;
	case FR_ACTION_EXTRACTING_FILES:
		message = _("Extracting files from archive");
		break;
	case FR_ACTION_COPYING_FILES_TO_REMOTE:
		message = _("Copying the file list");
		break;
	case FR_ACTION_CREATING_ARCHIVE:
		message = _("Creating archive");
		break;
	case FR_ACTION_SAVING_REMOTE_ARCHIVE:
		message = _("Saving archive");
		break;
	default:
		message = "";
		break;
	}

	return message;
}


static void
progress_dialog__set_last_action (FrWindow *window,
				  FRAction  action)
{
	const char *title;
	char       *markup;

	window->priv->pd_last_action = action;
	title = get_message_from_action (window->priv->pd_last_action);
	gtk_window_set_title (GTK_WINDOW (window->priv->progress_dialog), title);
	markup = g_markup_printf_escaped ("<span weight=\"bold\" size=\"larger\">%s</span>", title);
	gtk_label_set_markup (GTK_LABEL (window->priv->pd_action), markup);
	g_free (markup);
}


static gboolean
fr_window_message_cb (FrCommand  *command,
		      const char *msg,
		      FrWindow   *window)
{
	if (window->priv->progress_dialog == NULL)
		return TRUE;

	if (msg != NULL) {
		while (*msg == ' ')
			msg++;
		if (*msg == 0)
			msg = NULL;
	}

	if (msg == NULL) {
		gtk_label_set_text (GTK_LABEL (window->priv->pd_message), "");
	}
	else {
		char *utf8_msg;

		if (! g_utf8_validate (msg, -1, NULL))
			utf8_msg = g_locale_to_utf8 (msg, -1 , 0, 0, 0);
		else
			utf8_msg = g_strdup (msg);
		if (utf8_msg == NULL)
			return TRUE;

		if (g_utf8_validate (utf8_msg, -1, NULL))
			gtk_label_set_text (GTK_LABEL (window->priv->pd_message), utf8_msg);
		g_free (utf8_msg);
	}

	if (window->priv->convert_data.converting) {
		if (window->priv->pd_last_action != FR_ACTION_CREATING_ARCHIVE)
			progress_dialog__set_last_action (window, FR_ACTION_CREATING_ARCHIVE);
	}
	else if (window->priv->pd_last_action != window->priv->action)
		progress_dialog__set_last_action (window, window->priv->action);

	if (strcmp_null_tollerant (window->priv->pd_last_archive, window->priv->archive_uri) != 0) {
		g_free (window->priv->pd_last_archive);
		if (window->priv->archive_uri == NULL) {
			window->priv->pd_last_archive = NULL;
			gtk_label_set_text (GTK_LABEL (window->priv->pd_archive), "");
		}
		else {
			char *name;
			
			window->priv->pd_last_archive = g_strdup (window->priv->archive_uri);

			name = gnome_vfs_unescape_string_for_display (file_name_from_path (window->priv->archive_uri));
			gtk_label_set_text (GTK_LABEL (window->priv->pd_archive), name);
			g_free (name);
		}
	}

	return TRUE;
}


static gboolean
display_progress_dialog (gpointer data)
{
	FrWindow *window = data;

	if (window->priv->progress_timeout != 0)
		g_source_remove (window->priv->progress_timeout);

	if (window->priv->progress_dialog != NULL) {
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->priv->progress_dialog),
						   GTK_RESPONSE_OK,
						   window->priv->stoppable);
		if (! window->priv->batch_mode && ! window->priv->non_interactive)
			gtk_widget_show (GTK_WIDGET (window));

		gtk_widget_show (window->priv->progress_dialog);
		fr_window_message_cb (NULL, NULL, window);
	}

	window->priv->progress_timeout = 0;

	return FALSE;
}


static void
open_progress_dialog (FrWindow *window)
{
	if (! window->priv->batch_mode) {
		gtk_widget_show (window->priv->progress_bar);
		return;
	}

	if (window->priv->hide_progress_timeout != 0) {
		g_source_remove (window->priv->hide_progress_timeout);
		window->priv->hide_progress_timeout = 0;
	}

	if (window->priv->progress_timeout != 0)
		return;

	if (window->priv->progress_dialog == NULL) {
		GtkWindow     *parent;
		GtkDialog     *d;
		GtkWidget     *vbox;
		GtkWidget     *lbl;
		const char    *title;
		char          *markup;
		PangoAttrList *attr_list;

		if (window->priv->batch_mode)
			parent = NULL;
		else
			parent = GTK_WINDOW (window);

		window->priv->pd_last_action = window->priv->action;
		title = get_message_from_action (window->priv->pd_last_action);
		window->priv->progress_dialog = gtk_dialog_new_with_buttons (
						       title,
						       parent,
						       GTK_DIALOG_DESTROY_WITH_PARENT,
						       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						       NULL);
		d = GTK_DIALOG (window->priv->progress_dialog);
		gtk_dialog_set_has_separator (d, FALSE);
		gtk_window_set_resizable (GTK_WINDOW (d), TRUE);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);

		/* Main */
		
		vbox = gtk_vbox_new (FALSE, 5);
		gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
		gtk_box_pack_start (GTK_BOX (d->vbox), vbox, FALSE, FALSE, 10);

		/* action label */

		lbl = window->priv->pd_action = gtk_label_new ("");

		markup = g_markup_printf_escaped ("<span weight=\"bold\" size=\"larger\">%s</span>", title);
		gtk_label_set_markup (GTK_LABEL (lbl), markup);
		g_free (markup);

		gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
		gtk_label_set_ellipsize (GTK_LABEL (lbl), PANGO_ELLIPSIZE_END);
		gtk_box_pack_start (GTK_BOX (vbox), lbl, TRUE, TRUE, 0);

		/* archive name */

		g_free (window->priv->pd_last_archive);
		window->priv->pd_last_archive = NULL;
		if (window->priv->archive_uri != NULL) {
			GtkWidget *hbox;
			char      *name;

			hbox = gtk_hbox_new (FALSE, 6);
			gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 6);

			lbl = gtk_label_new ("");
			markup = g_markup_printf_escaped ("<b>%s</b>", _("Archive:"));
			gtk_label_set_markup (GTK_LABEL (lbl), markup);
			g_free (markup);
			gtk_box_pack_start (GTK_BOX (hbox), lbl, FALSE, FALSE, 0);

			window->priv->pd_last_archive = g_strdup (window->priv->archive_uri);
			name = gnome_vfs_unescape_string_for_display (file_name_from_path (window->priv->pd_last_archive));
			lbl = window->priv->pd_archive = gtk_label_new (name);
			g_free (name);

			gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
			gtk_label_set_ellipsize (GTK_LABEL (lbl), PANGO_ELLIPSIZE_END);
			gtk_box_pack_start (GTK_BOX (hbox), lbl, TRUE, TRUE, 0);
		}

		/* progress bar */

		window->priv->pd_progress_bar = gtk_progress_bar_new ();
		gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (window->priv->pd_progress_bar), ACTIVITY_PULSE_STEP);

		gtk_widget_set_size_request (window->priv->pd_progress_bar, PROGRESS_DIALOG_WIDTH, -1);
		gtk_box_pack_start (GTK_BOX (vbox), window->priv->pd_progress_bar, TRUE, TRUE, 0);

		/* details label */

		lbl = window->priv->pd_message = gtk_label_new ("");

		attr_list = pango_attr_list_new ();
		pango_attr_list_insert (attr_list, pango_attr_style_new (PANGO_STYLE_ITALIC));
		gtk_label_set_attributes (GTK_LABEL (lbl), attr_list);
		pango_attr_list_unref (attr_list);

		gtk_misc_set_alignment (GTK_MISC (lbl), 0.0, 0.5);
		gtk_label_set_ellipsize (GTK_LABEL (lbl), PANGO_ELLIPSIZE_END);
		gtk_box_pack_start (GTK_BOX (vbox), lbl, TRUE, TRUE, 0);

		/**/

		g_signal_connect (G_OBJECT (window->priv->progress_dialog),
				  "response",
				  G_CALLBACK (progress_dialog_response),
				  window);
		g_signal_connect (G_OBJECT (window->priv->progress_dialog),
				  "delete_event",
				  G_CALLBACK (progress_dialog_delete_event),
				  window);

		gtk_widget_show_all (vbox);
	}

	window->priv->progress_timeout = g_timeout_add (PROGRESS_TIMEOUT_MSECS,
						        display_progress_dialog,
						        window);
}


void
fr_window_push_message (FrWindow   *window,
			const char *msg)
{
	if (GTK_WIDGET_MAPPED (window))
		gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar), window->priv->progress_cid, msg);
}


void
fr_window_pop_message (FrWindow *window)
{
	if (GTK_WIDGET_MAPPED (window))
		gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar), window->priv->progress_cid);
	if (window->priv->progress_dialog != NULL)
		gtk_label_set_text (GTK_LABEL (window->priv->pd_message), "");
}


static void
action_started (FrArchive *archive,
		FRAction   action,
		gpointer   data)
{
	FrWindow   *window = data;
	const char *message;
	char       *full_msg;

	window->priv->action = action;
	fr_window_start_activity_mode (window);

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [START] (FR::Window)\n", action_names[action]);
#endif

	message = get_message_from_action (action);
	full_msg = g_strdup_printf ("%s, %s", message, _("wait please..."));
	fr_window_push_message (window, full_msg);
	open_progress_dialog (window);
	if (archive->command != NULL) {
		fr_command_progress (archive->command, -1.0);
		fr_command_message (archive->command, message);
	}

	g_free (full_msg);
}


static void
fr_window_add_to_recent_list (FrWindow *window,
			      char     *uri)
{
	if (window->priv->batch_mode)
		return;

	if (is_temp_work_dir (uri))
		return;

	if (window->archive->mime_type != NULL) {
		GtkRecentData *recent_data;

		recent_data = g_new0 (GtkRecentData, 1);
		recent_data->mime_type = window->archive->mime_type;
		recent_data->app_name = "File Roller";
		recent_data->app_exec = "file-roller";
		gtk_recent_manager_add_full (window->priv->recent_manager, uri, recent_data);

		g_free (recent_data);
	}
	else
		gtk_recent_manager_add_item (window->priv->recent_manager, uri);
}


static void
fr_window_remove_from_recent_list (FrWindow *window,
				   char     *filename)
{
	char *uri;

	if (filename == NULL)
		return;

	uri = get_uri_from_path (filename);
	gtk_recent_manager_remove_item (window->priv->recent_manager, uri, NULL);
	g_free (uri);
}


static void
open_folder (GtkWindow  *parent,
	     const char *folder)
{
	GError *err = NULL;

	if (folder == NULL)
		return;

	if (! gnome_url_show (folder, &err)) {
		GtkWidget *d;
		char      *utf8_name;
		char      *message;

		utf8_name = g_filename_display_name (folder);
		message = g_strdup_printf (_("Could not display the folder \"%s\""), utf8_name);
		g_free (utf8_name);

		d = _gtk_error_dialog_new (parent,
					   GTK_DIALOG_MODAL,
					   NULL,
					   message,
					   err->message);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);

		g_free (message);
		g_clear_error (&err);
	}
}


void
fr_window_convert_data_free (FrWindow *window)
{
	window->priv->convert_data.converting = FALSE;

	g_free (window->priv->convert_data.temp_dir);
	window->priv->convert_data.temp_dir = NULL;

	g_object_unref (window->priv->convert_data.new_archive);
	window->priv->convert_data.new_archive = NULL;
}


static void
error_dialog_response_cb (GtkDialog *dialog,
			  gint       arg1,
			  gpointer   user_data)
{
	GtkWindow *dialog_parent = user_data;

	if ((dialog_parent != NULL) && (gtk_widget_get_toplevel (GTK_WIDGET (dialog_parent)) != (GtkWidget*) dialog_parent))
		gtk_window_set_modal (dialog_parent, TRUE);
	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static gboolean
handle_errors (FrWindow    *window,
	       FrArchive   *archive,
	       FRAction     action,
	       FRProcError *error)
{
	if (error->type == FR_PROC_ERROR_ASK_PASSWORD) {
		dlg_ask_password (window);
		return FALSE;
	}
	else if (error->type == FR_PROC_ERROR_STOPPED) {
		/* nothing */
	}
	else if (error->type != FR_PROC_ERROR_NONE) {
		char      *msg = NULL;
		char      *utf8_name;
		char      *details = NULL;
		GtkWindow *dialog_parent;
		GtkWidget *dialog;
		FrProcess *process = archive->process;
		GList     *output = NULL;

		if (window->priv->batch_mode)
			dialog_parent = NULL;
		else
			dialog_parent = (GtkWindow *) window;

		if ((action == FR_ACTION_LISTING_CONTENT) || (action == FR_ACTION_LOADING_ARCHIVE))
			fr_window_archive_close (window);

		switch (action) {
		case FR_ACTION_CREATING_NEW_ARCHIVE:
			dialog_parent = window->priv->load_error_parent_window;
			msg = _("Could not create the archive");
			break;

		case FR_ACTION_EXTRACTING_FILES:
			msg = _("An error occurred while extracting files.");
			break;

		case FR_ACTION_LOADING_ARCHIVE:
			dialog_parent = window->priv->load_error_parent_window;
			utf8_name = g_filename_display_basename (window->priv->archive_uri);
			msg = g_strdup_printf (_("Could not open \"%s\""), utf8_name);
			g_free (utf8_name);
			break;

		case FR_ACTION_LISTING_CONTENT:
			msg = _("An error occurred while loading the archive.");
			break;

		case FR_ACTION_DELETING_FILES:
			msg = _("An error occurred while deleting files from the archive.");
			break;

		case FR_ACTION_ADDING_FILES:
			msg = _("An error occurred while adding files to the archive.");
			break;

		case FR_ACTION_TESTING_ARCHIVE:
			msg = _("An error occurred while testing archive.");
			break;

		default:
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
			if (error->gerror != NULL)
				details = error->gerror->message;
			else
				details = NULL;
			break;
		}

		if (error->type != FR_PROC_ERROR_GENERIC)
			output = (process->raw_error != NULL) ? process->raw_error : process->raw_output;

		if (dialog_parent != NULL)
			gtk_window_set_modal (dialog_parent, FALSE);
		dialog = _gtk_error_dialog_new (dialog_parent,
						0,
						output,
						msg,
						details);
		g_signal_connect (dialog,
				  "response",
				  (window->priv->batch_mode) ? G_CALLBACK (gtk_main_quit) : G_CALLBACK (error_dialog_response_cb),
				  dialog_parent);

		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
		gtk_widget_show (dialog);

		return FALSE;
	}

	return TRUE;
}


static void
convert__action_performed (FrArchive   *archive,
			   FRAction     action,
			   FRProcError *error,
			   gpointer     data)
{
	FrWindow *window = data;

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [CONVERT::DONE] (FR::Window)\n", action_names[action]);
#endif

	if ((action == FR_ACTION_GETTING_FILE_LIST) || (action == FR_ACTION_ADDING_FILES)) {
		fr_window_stop_activity_mode (window);
		fr_window_pop_message (window);
		close_progress_dialog (window);
	}

	if (action != FR_ACTION_ADDING_FILES)
		return;

	handle_errors (window, archive, action, error);

	remove_local_directory (window->priv->convert_data.temp_dir);
	fr_window_convert_data_free (window);

	fr_window_update_sensitivity (window);
	fr_window_update_statusbar_list_info (window);
}


static gboolean
view_extraction_destination_folder (gpointer data)
{
	FrWindow *window = data;
	
	open_folder (GTK_WINDOW (window), fr_archive_get_last_extraction_destination (window->archive));
	window->priv->view_folder_after_extraction = FALSE;
	
	return FALSE;
}


static void fr_window_exec_next_batch_action (FrWindow *window);


static void
action_performed (FrArchive   *archive,
		  FRAction     action,
		  FRProcError *error,
		  gpointer     data)
{
	FrWindow *window = data;
	gboolean  continue_batch = FALSE;
	char     *archive_dir;
	gboolean  temp_dir;

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	fr_window_stop_activity_mode (window);
	fr_window_pop_message (window);
	close_progress_dialog (window);

	continue_batch = handle_errors (window, archive, action, error);

	if (error->type == FR_PROC_ERROR_ASK_PASSWORD)
		return;

	switch (action) {
	case FR_ACTION_CREATING_NEW_ARCHIVE:
	case FR_ACTION_CREATING_ARCHIVE:
		if (error->type != FR_PROC_ERROR_STOPPED) {
			fr_window_history_clear (window);
			fr_window_history_add (window, "/");
			fr_window_update_file_list (window);
			fr_window_update_title (window);
			fr_window_update_sensitivity (window);
			fr_window_update_current_location (window);
		}
		break;

	case FR_ACTION_LOADING_ARCHIVE:
		if (error->type != FR_PROC_ERROR_NONE) {
			fr_window_remove_from_recent_list (window, window->priv->archive_uri);
			if (window->priv->batch_mode) {
				fr_window_archive_close (window);
				fr_window_stop_batch (window);
			}
		}
		else {
			fr_window_add_to_recent_list (window, window->priv->archive_uri);
			if (! window->priv->non_interactive)
				gtk_window_present (GTK_WINDOW (window));
		}
		continue_batch = FALSE;

		g_signal_emit (G_OBJECT (window),
			       fr_window_signals[ARCHIVE_LOADED],
			       0,
			       error->type == FR_PROC_ERROR_NONE);
		break;

	case FR_ACTION_LISTING_CONTENT:
		if (error->type != FR_PROC_ERROR_NONE) {
			fr_window_remove_from_recent_list (window, window->priv->archive_uri);
			fr_window_archive_close (window);
			break;
		}

		archive_dir = remove_level_from_path (window->priv->archive_uri);
		temp_dir = is_temp_work_dir (archive_dir);
		if (! window->priv->archive_present) {
			window->priv->archive_present = TRUE;

			fr_window_history_clear (window);
			fr_window_history_add (window, "/");

			if (! temp_dir) {
				fr_window_set_open_default_dir (window, archive_dir);
				fr_window_set_add_default_dir (window, archive_dir);
				if (! window->priv->freeze_default_dir)
					fr_window_set_extract_default_dir (window, archive_dir, FALSE);
			}

			window->priv->archive_new = FALSE;
		}
		g_free (archive_dir);

		if (! temp_dir)
			fr_window_add_to_recent_list (window, window->priv->archive_uri);

		fr_window_update_title (window);
		fr_window_update_current_location (window);
		fr_window_update_file_list (window);
		if (! window->priv->non_interactive)
			gtk_window_present (GTK_WINDOW (window));
		break;

	case FR_ACTION_DELETING_FILES:
		fr_window_archive_reload (window);
		return;

	case FR_ACTION_ADDING_FILES:
		if (error->type != FR_PROC_ERROR_NONE) {
			fr_window_archive_reload (window);
			return;
		}

		if (window->priv->archive_new) {
			window->priv->archive_new = FALSE;
			/* the archive file is created only when you add some
			 * file to it. */
			fr_window_add_to_recent_list (window, window->priv->archive_uri);
		}

		fr_window_add_to_recent_list (window, window->priv->archive_uri);
		if (! window->priv->batch_mode) {
			fr_window_archive_reload (window);
			return;
		}
		break;

	case FR_ACTION_TESTING_ARCHIVE:
		if (error->type == FR_PROC_ERROR_NONE)
			fr_window_view_last_output (window, _("Test Result"));
		return;

	case FR_ACTION_EXTRACTING_FILES:
		if (error->type != FR_PROC_ERROR_NONE) {
			if (window->priv->convert_data.converting) {
				remove_local_directory (window->priv->convert_data.temp_dir);
				fr_window_convert_data_free (window);
			}
			window->priv->view_folder_after_extraction = FALSE;
			break;
		}

		if (window->priv->convert_data.converting) {
			fr_archive_add_with_wildcard (
				  window->priv->convert_data.new_archive,
				  "*",
				  NULL,
				  window->priv->convert_data.temp_dir,
				  NULL,
				  FALSE,
				  TRUE,
				  FALSE,
				  window->priv->password,
				  window->priv->compression);
		}
		else if (window->priv->view_folder_after_extraction) {
			if (window->priv->batch_mode) {
				g_usleep (G_USEC_PER_SEC);
				view_extraction_destination_folder (window);
			}
			else
				g_timeout_add (500, view_extraction_destination_folder, window);
		}
		break;

	default:
		continue_batch = FALSE;
		break;
	}

	if (window->priv->batch_action == NULL) {
		fr_window_update_sensitivity (window);
		fr_window_update_statusbar_list_info (window);
	}

	if (continue_batch) {
		if (error->type != FR_PROC_ERROR_NONE)
			fr_window_stop_batch (window);
		else
			fr_window_exec_next_batch_action (window);
	}
}


static FileData *
fr_window_get_selected_folder (FrWindow *window)
{
	GtkTreeSelection *selection;
	GList            *selections = NULL, *scan;
	FileData         *fdata = NULL;

	g_return_val_if_fail (window != NULL, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected, &selections);

	for (scan = selections; scan; scan = scan->next) {
		FileData *fd = scan->data;
		if ((fd != NULL) && file_data_is_dir (fd)) {
			if (fdata != NULL) {
				file_data_free (fdata);
				fdata = NULL;
				break;
			}
			fdata = file_data_copy (fd);
		}
	}

	if (selections != NULL)
		g_list_free (selections);

	return fdata;
}


void
fr_window_current_folder_activated (FrWindow *window)
{
	FileData *fdata;
	char     *new_dir;

	fdata = fr_window_get_selected_folder (window);
	if (fdata == NULL)
		return;

	new_dir = g_strconcat (fr_window_get_current_location (window),
			       fdata->list_name,
			       "/",
			       NULL);
	fr_window_go_to_location (window, new_dir);
	g_free (new_dir);
}


static gboolean
row_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  gpointer           data)
{
	FrWindow    *window = data;
	FileData    *fdata;
	GtkTreeIter  iter;

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store),
				       &iter,
				       path))
		return FALSE;

	gtk_tree_model_get (GTK_TREE_MODEL (window->priv->list_store), &iter,
			    COLUMN_FILE_DATA, &fdata,
			    -1);

	if (file_data_is_dir (fdata)) {
		char *new_dir;
		new_dir = g_strconcat (fr_window_get_current_location (window),
				       fdata->list_name,
				       "/",
				       NULL);
		fr_window_go_to_location (window, new_dir);
		g_free (new_dir);
	}
	else
		fr_window_view_or_open_file (window, fdata->original_path);

	return FALSE;
}


static int
file_button_press_cb (GtkWidget      *widget,
		      GdkEventButton *event,
		      gpointer        data)
{
	FrWindow         *window = data;
	GtkTreeSelection *selection;

	if (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (window->priv->list_view)))
		return FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return FALSE;

	if (window->priv->path_clicked != NULL) {
		gtk_tree_path_free (window->priv->path_clicked);
		window->priv->path_clicked = NULL;
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
		GtkTreePath *path;
		GtkTreeIter  iter;
		int          n_selected;

		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->priv->list_view),
						   event->x, event->y,
						   &path, NULL, NULL, NULL)) {

			if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store), &iter, path)) {
				gtk_tree_path_free (path);
				return FALSE;
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
			gtk_menu_popup (GTK_MENU (window->priv->folder_popup_menu),
					NULL, NULL, NULL,
					window,
					event->button,
					event->time);
		else
			gtk_menu_popup (GTK_MENU (window->priv->file_popup_menu),
					NULL, NULL, NULL,
					window,
					event->button,
					event->time);
		return TRUE;
	}
	else if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		GtkTreePath *path = NULL;

		if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->priv->list_view),
						     event->x, event->y,
						     &path, NULL, NULL, NULL)) {
			gtk_tree_selection_unselect_all (selection);
		}

		if (window->priv->path_clicked != NULL) {
			gtk_tree_path_free (window->priv->path_clicked);
			window->priv->path_clicked = NULL;
		}

		if (path != NULL) {
			window->priv->path_clicked = gtk_tree_path_copy (path);
			gtk_tree_path_free (path);
		}

		return FALSE;
	}

	return FALSE;
}


static int
file_button_release_cb (GtkWidget      *widget,
			GdkEventButton *event,
			gpointer        data)
{
	FrWindow         *window = data;
	GtkTreeSelection *selection;

	if (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (window->priv->list_view)))
		return FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return FALSE;

	if (window->priv->path_clicked == NULL)
		return FALSE;

	if ((event->type == GDK_BUTTON_RELEASE)
	    && (event->button == 1)
	    && (window->priv->path_clicked != NULL)) {
		GtkTreePath *path = NULL;

		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->priv->list_view),
						   event->x, event->y,
						   &path, NULL, NULL, NULL)) {

			if ((gtk_tree_path_compare (window->priv->path_clicked, path) == 0)
			    && window->priv->single_click
			    && ! ((event->state & GDK_CONTROL_MASK) || (event->state & GDK_SHIFT_MASK))) {
				gtk_tree_view_set_cursor (GTK_TREE_VIEW (widget),
							  path,
							  NULL,
							  FALSE);
				gtk_tree_view_row_activated (GTK_TREE_VIEW (widget),
							     path,
							     NULL);
			}
		}

		if (path != NULL)
			gtk_tree_path_free (path);
	}

	if (window->priv->path_clicked != NULL) {
		gtk_tree_path_free (window->priv->path_clicked);
		window->priv->path_clicked = NULL;
	}

	return FALSE;
}


static gboolean
file_motion_notify_callback (GtkWidget *widget,
			     GdkEventMotion *event,
			     gpointer user_data)
{
	FrWindow    *window = user_data;
	GdkCursor   *cursor;
	GtkTreePath *last_hover_path;
	GtkTreeIter  iter;

	if (! window->priv->single_click)
		return FALSE;

	if (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (window->priv->list_view)))
		return FALSE;

	last_hover_path = window->priv->hover_path;

	gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
				       event->x, event->y,
				       &window->priv->hover_path,
				       NULL, NULL, NULL);

	if (window->priv->hover_path != NULL)
		cursor = gdk_cursor_new (GDK_HAND2);
	else
		cursor = NULL;

	gdk_window_set_cursor (event->window, cursor);

	/* only redraw if the hover row has changed */
	if (!(last_hover_path == NULL && window->priv->hover_path == NULL) &&
	    (!(last_hover_path != NULL && window->priv->hover_path != NULL) ||
	     gtk_tree_path_compare (last_hover_path, window->priv->hover_path))) {
		if (last_hover_path) {
			gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store),
						 &iter, last_hover_path);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (window->priv->list_store),
						    last_hover_path, &iter);
		}

		if (window->priv->hover_path) {
			gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store),
						 &iter, window->priv->hover_path);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (window->priv->list_store),
						    window->priv->hover_path, &iter);
		}
	}

	gtk_tree_path_free (last_hover_path);

 	return FALSE;
}


static gboolean
file_leave_notify_callback (GtkWidget *widget,
			    GdkEventCrossing *event,
			    gpointer user_data)
{
	FrWindow    *window = user_data;
	GtkTreeIter  iter;

	if (window->priv->single_click && (window->priv->hover_path != NULL)) {
		gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store),
					 &iter,
					 window->priv->hover_path);
		gtk_tree_model_row_changed (GTK_TREE_MODEL (window->priv->list_store),
					    window->priv->hover_path,
					    &iter);

		gtk_tree_path_free (window->priv->hover_path);
		window->priv->hover_path = NULL;
	}

	return FALSE;
}


/* -- drag and drop -- */


static GList *
get_uri_list_from_selection_data (char *uri_list)
{
	GList *uris = NULL, *scan;
	GList *list = NULL;

	if (uri_list == NULL)
		return NULL;

	uris = gnome_vfs_uri_list_parse (uri_list);
	for (scan = uris; scan; scan = g_list_next (scan)) {
		char *uri = gnome_vfs_uri_to_string (scan->data, GNOME_VFS_URI_HIDE_NONE);
		list = g_list_prepend (list, uri);
	}
	gnome_vfs_uri_list_free (uris);

	return g_list_reverse (list);
}


static gboolean
fr_window_drag_motion (GtkWidget      *widget,
		       GdkDragContext *drag_context,
		       gint            x,
		       gint            y,
		       guint           time,
		       gpointer        user_data)
{
	FrWindow  *window = user_data;

	if (gtk_drag_get_source_widget (drag_context) == window->priv->list_view) {
		gdk_drag_status (drag_context, 0, time);
		return FALSE;
	}

	return TRUE;
}


static void fr_window_paste_from_clipboard_data (FrWindow *window, FrClipboardData *data);


static FrClipboardData*
get_clipboard_data_from_selection_data (FrWindow   *window,
					const char *data)
{
	FrClipboardData  *clipboard_data;
	char            **uris;
	int               i;
	
	clipboard_data = fr_clipboard_data_new ();
	
	uris = g_strsplit (data, "\r\n", -1);
		
	clipboard_data->archive_filename = g_strdup (uris[0]);
	if (window->priv->password_for_paste != NULL)
		clipboard_data->archive_password = g_strdup (window->priv->password_for_paste);
	else if (strcmp (uris[1], "") != 0)
		clipboard_data->archive_password = g_strdup (uris[1]);
	clipboard_data->op = (strcmp (uris[2], "copy") == 0) ? FR_CLIPBOARD_OP_COPY : FR_CLIPBOARD_OP_CUT;
	clipboard_data->base_dir = g_strdup (uris[3]);
	for (i = 4; uris[i] != NULL; i++)
		if (uris[i][0] != '\0')
			clipboard_data->files = g_list_prepend (clipboard_data->files, g_strdup (uris[i]));
	clipboard_data->files = g_list_reverse (clipboard_data->files);
	
	g_strfreev (uris);
	
	return clipboard_data;
}


static void
fr_window_drag_data_received  (GtkWidget          *widget,
			       GdkDragContext     *context,
			       gint                x,
			       gint                y,
			       GtkSelectionData   *data,
			       guint               info,
			       guint               time,
			       gpointer            extra_data)
{
	FrWindow  *window = extra_data;
	GList     *list;
	gboolean   one_file;
	gboolean   is_an_archive;

	debug (DEBUG_INFO, "::DragDataReceived -->\n");

	if (gtk_drag_get_source_widget (context) == window->priv->list_view) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	if (! ((data->length >= 0) && (data->format == 8))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	if (window->priv->activity_ref > 0) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	if (data->target == XFR_ATOM) {
		FrClipboardData *dnd_data;
		
		dnd_data = fr_clipboard_data_new ();
		dnd_data = get_clipboard_data_from_selection_data (window, (char*) data->data);
		dnd_data->current_dir = g_strdup (fr_window_get_current_location (window));
		fr_window_paste_from_clipboard_data (window, dnd_data);		
		return;
	}

	list = get_uri_list_from_selection_data ((char*)data->data);
	if (list == NULL) {
		GtkWidget *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not perform the operation"),
					   NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy(d);

 		return;
	}

	one_file = (list->next == NULL);
	if (one_file)
		is_an_archive = fr_archive_utils__file_is_archive (list->data);
	else
		is_an_archive = FALSE;

	if (window->priv->archive_present
	    && (window->archive != NULL)
	    && ! window->archive->read_only
	    && ! window->archive->is_compressed_file
	    && ((window->archive->command != NULL)
		&& window->archive->command->propCanModify)) {
		if (one_file && is_an_archive) {
			GtkWidget *d;
			gint       r;

			d = _gtk_message_dialog_new (GTK_WINDOW (window),
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

			if (r == 0)  /* Add */
				fr_window_archive_add_dropped_items (window, list, FALSE);
			else if (r == 1)  /* Open */
				fr_window_archive_open (window, list->data, GTK_WINDOW (window));
 		} 
 		else 
			fr_window_archive_add_dropped_items (window, list, FALSE);
	}
	else {
		if (one_file && is_an_archive)
			fr_window_archive_open (window, list->data, GTK_WINDOW (window));
		else {
			GtkWidget *d;
			int        r;

			d = _gtk_message_dialog_new (GTK_WINDOW (window),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     _("Do you want to create a new archive with these files?"),
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Archive"), GTK_RESPONSE_YES,
						     NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			if (r == GTK_RESPONSE_YES) {
				char       *first_item;
				char       *folder;
				const char *archive_name;

				fr_window_free_batch_data (window);
				fr_window_append_batch_action (window,
							       FR_BATCH_ACTION_ADD,
							       path_list_dup (list),
							       (GFreeFunc) path_list_free);

				first_item = (char*) list->data;
				folder = remove_level_from_path (first_item);
				if (folder != NULL) 
					fr_window_set_open_default_dir (window, folder);
					
				if ((list->next != NULL) && (folder != NULL))
					archive_name = file_name_from_path (folder);
				else
					archive_name = file_name_from_path (first_item);
					
				show_new_archive_dialog (window, archive_name);
				
				g_free (folder);
			}
		}
	}

	path_list_free (list);

	debug (DEBUG_INFO, "::DragDataReceived <--\n");
}


static gboolean
file_list_drag_begin (GtkWidget          *widget,
		      GdkDragContext     *context,
		      gpointer            data)
{
	FrWindow *window = data;

	debug (DEBUG_INFO, "::DragBegin -->\n");

	if (window->priv->activity_ref > 0)
		return FALSE;

	g_free (window->priv->drag_destination_folder);
	window->priv->drag_destination_folder = NULL;

	gdk_property_change (context->source_window,
			     XDS_ATOM, TEXT_ATOM,
			     8, GDK_PROP_MODE_REPLACE,
			     (guchar *) XDS_FILENAME,
			     strlen (XDS_FILENAME));

	return TRUE;
}


static void
file_list_drag_end (GtkWidget      *widget,
		    GdkDragContext *context,
		    gpointer        data)
{
	FrWindow *window = data;

	debug (DEBUG_INFO, "::DragEnd -->\n");

	gdk_property_delete (context->source_window, XDS_ATOM);

	if (window->priv->drag_error != NULL) {
		_gtk_error_dialog_run (GTK_WINDOW (window),
				       _("Extraction not performed"),
				       "%s",
				       window->priv->drag_error->message);
		g_clear_error (&window->priv->drag_error);
	}
	else if (window->priv->drag_destination_folder != NULL) {
		fr_window_archive_extract (window,
					   window->priv->drag_file_list,
					   window->priv->drag_destination_folder,
					   fr_window_get_current_location (window),
					   FALSE,
					   TRUE,
					   FALSE,
					   window->priv->password);
		path_list_free (window->priv->drag_file_list);
		window->priv->drag_file_list = NULL;
	}

	debug (DEBUG_INFO, "::DragEnd <--\n");
}


/* The following three functions taken from bugzilla
 * (http://bugzilla.gnome.org/attachment.cgi?id=49362&action=view)
 * Author: Christian Neumair
 * Copyright: 2005 Free Software Foundation, Inc
 * License: GPL */
static char *
get_xds_atom_value (GdkDragContext *context)
{
	char *ret;

	g_return_val_if_fail (context != NULL, NULL);
	g_return_val_if_fail (context->source_window != NULL, NULL);

	gdk_property_get (context->source_window,
			  XDS_ATOM, TEXT_ATOM,
			  0, MAX_XDS_ATOM_VAL_LEN,
			  FALSE, NULL, NULL, NULL,
			  (unsigned char **) &ret);

	return ret;
}


static gboolean
context_offers_target (GdkDragContext *context,
		       GdkAtom target)
{
	return (g_list_find (context->targets, target) != NULL);
}


static gboolean
nautilus_xds_dnd_is_valid_xds_context (GdkDragContext *context)
{
	char *tmp;
	gboolean ret;

	g_return_val_if_fail (context != NULL, FALSE);

	tmp = NULL;
	if (context_offers_target (context, XDS_ATOM)) {
		tmp = get_xds_atom_value (context);
	}

	ret = (tmp != NULL);
	g_free (tmp);

	return ret;
}


static char *
get_selection_data_from_clipboard_data (FrWindow        *window,
		      			FrClipboardData *data)
{
	GString *list;
	GList   *scan;
	
	list = g_string_new (NULL);
	g_string_append (list, window->archive->local_filename);
	g_string_append (list, "\r\n");
	if (window->priv->password != NULL)
		g_string_append (list, window->priv->password);
	g_string_append (list, "\r\n");
	g_string_append (list, (data->op == FR_CLIPBOARD_OP_COPY) ? "copy" : "cut");
	g_string_append (list, "\r\n");
	g_string_append (list, data->base_dir);
	g_string_append (list, "\r\n");
	for (scan = data->files; scan; scan = scan->next) {
		g_string_append (list, scan->data);
		g_string_append (list, "\r\n");
	}
	
	return g_string_free (list, FALSE);
}


gboolean
fr_window_file_list_drag_data_get (FrWindow         *window,
				   GdkDragContext   *context,
				   GtkSelectionData *selection_data,
				   GList            *path_list)
{
	char *destination;
	char *destination_folder;
	char *destination_folder_display_name;

	debug (DEBUG_INFO, "::DragDataGet -->\n");

	if (window->priv->path_clicked != NULL) {
		gtk_tree_path_free (window->priv->path_clicked);
		window->priv->path_clicked = NULL;
	}

	if (window->priv->activity_ref > 0)
		return FALSE;

	if (context_offers_target (context, XFR_ATOM)) {
		FrClipboardData *tmp;
		char            *data;
		
		tmp = fr_clipboard_data_new ();
		tmp->files = fr_window_get_file_list_selection (window, TRUE, NULL);
		tmp->op = FR_CLIPBOARD_OP_COPY;
		tmp->base_dir = g_strdup (fr_window_get_current_location (window));
		
		data = get_selection_data_from_clipboard_data (window, tmp);
		gtk_selection_data_set (selection_data, XFR_ATOM, 8, (guchar *) data, strlen (data));
		
		fr_clipboard_data_free (tmp);
		g_free (data);
			
		return TRUE;
	}		

	if (! nautilus_xds_dnd_is_valid_xds_context (context)) 
		return FALSE;

	destination = get_xds_atom_value (context);
	g_return_val_if_fail (destination != NULL, FALSE);

	destination_folder = remove_level_from_path (destination);
	g_free (destination);

	/* check whether the extraction can be performed in the destination
	 * folder */

	g_clear_error (&window->priv->drag_error);
	destination_folder_display_name = g_filename_display_name (destination_folder);

	if (! check_permissions (destination_folder, R_OK | W_OK | X_OK))
		window->priv->drag_error = g_error_new (FR_ERROR, 0, _("You don't have the right permissions to extract archives in the folder \"%s\""), destination_folder_display_name);

	else if (! uri_is_local (destination_folder))
		window->priv->drag_error = g_error_new (FR_ERROR, 0, _("Cannot extract archives in a remote folder \"%s\""), destination_folder_display_name);

	g_free (destination_folder_display_name);

	if (window->priv->drag_error == NULL) {
		g_free (window->priv->drag_destination_folder);
		window->priv->drag_destination_folder = gnome_vfs_get_local_path_from_uri (destination_folder);
		path_list_free (window->priv->drag_file_list);
		window->priv->drag_file_list = fr_window_get_file_list_from_path_list (window, path_list, NULL);
	}

	g_free (destination_folder);

	/* sends back the response */

	gtk_selection_data_set (selection_data, selection_data->target, 8, (guchar *) ((window->priv->drag_error == NULL) ? "S" : "E"), 1);

	debug (DEBUG_INFO, "::DragDataGet <--\n");

	return TRUE;
}


/* -- window_new -- */


static gboolean
key_press_cb (GtkWidget   *widget,
	      GdkEventKey *event,
	      gpointer     data)
{
	FrWindow *window = data;
	gboolean  retval = FALSE;
	gboolean  alt;

	if (GTK_WIDGET_HAS_FOCUS (window->priv->location_entry))
		return FALSE;

	alt = (event->state & GDK_MOD1_MASK) == GDK_MOD1_MASK;

	switch (event->keyval) {
	case GDK_Escape:
		activate_action_stop (NULL, window);
		retval = TRUE;
		break;

	case GDK_Delete:
		if (window->priv->activity_ref == 0)
			dlg_delete (NULL, window);
		retval = TRUE;
		break;

	case GDK_F10:
		if (event->state & GDK_SHIFT_MASK) {
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
			if (selection == NULL)
				return FALSE;

			gtk_menu_popup (GTK_MENU (window->priv->file_popup_menu),
					NULL, NULL, NULL,
					window,
					3,
					GDK_CURRENT_TIME);
		}
		retval = TRUE;
		break;

	case GDK_Up:
	case GDK_KP_Up:
		if (alt) {
			fr_window_go_up_one_level (window);
			retval = TRUE;
		}
		break;

	case GDK_BackSpace:
		fr_window_go_up_one_level (window);
		retval = TRUE;
		break;

	case GDK_Right:
	case GDK_KP_Right:
		if (alt) {
			fr_window_go_forward (window);
			retval = TRUE;
		}
		break;

	case GDK_Left:
	case GDK_KP_Left:
		if (alt) {
			fr_window_go_back (window);
			retval = TRUE;
		}
		break;

	case GDK_Home:
	case GDK_KP_Home:
		if (alt) {
			fr_window_go_to_location (window, "/");
			retval = TRUE;
		}
		break;

	default:
		break;
	}

	return retval;
}


static gboolean
selection_changed_cb (GtkTreeSelection *selection,
		      gpointer          user_data)
{
	FrWindow *window = user_data;

	fr_window_update_statusbar_list_info (window);
	fr_window_update_sensitivity (window);

	return FALSE;
}


static void
fr_window_delete_event_cb (GtkWidget *caller,
			   GdkEvent  *event,
			   FrWindow  *window)
{
	fr_window_close (window);
}


static gboolean
is_single_click_policy (void)
{
	char     *value;
	gboolean  result = FALSE;

	value = eel_gconf_get_string (PREF_NAUTILUS_CLICK_POLICY, "double");
	result = strncmp (value, "single", 6) == 0;
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
	char           *text;
	GtkTreePath    *path;
	PangoUnderline  underline;

	gtk_tree_model_get (model, iter,
			    COLUMN_NAME, &text,
			    -1);

	if (window->priv->single_click) {
		path = gtk_tree_model_get_path (model, iter);

		if ((window->priv->hover_path == NULL)
		    || gtk_tree_path_compare (path, window->priv->hover_path))
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
add_columns (FrWindow    *window,
	     GtkTreeView *treeview)
{
	static char       *titles[] = {N_("Size"),
				       N_("Type"),
				       N_("Date Modified"),
				       N_("Location")};
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	GValue             value = { 0, };
	int                i, j;

	/* First column. */

	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, _("Name"));

	/* emblem */						 
						 
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_end (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "pixbuf", COLUMN_EMBLEM,
					     NULL);

	/* icon */
	
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "pixbuf", COLUMN_ICON,
					     NULL);

	/* name */

	window->priv->name_renderer = renderer = gtk_cell_renderer_text_new ();

	window->priv->single_click = is_single_click_policy ();

	g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
	g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
	g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
	g_value_unset (&value);

	gtk_tree_view_column_pack_start (column,
					 renderer,
					 TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "text", COLUMN_NAME,
					     NULL);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (column, NAME_COLUMN_WIDTH);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_NAME);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) filename_cell_data_func,
						 window, NULL);				
						 
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Other columns */
	
	for (j = 0, i = COLUMN_SIZE; i < NUMBER_OF_COLUMNS; i++, j++) {
		GValue  value = { 0, };

		renderer = gtk_cell_renderer_text_new ();
		column = gtk_tree_view_column_new_with_attributes (_(titles[j]),
								   renderer,
								   "text", i,
								   NULL);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width (column, OTHER_COLUMNS_WIDTH);
		gtk_tree_view_column_set_resizable (column, TRUE);

		gtk_tree_view_column_set_sort_column_id (column, i);

		g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
		g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
		g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
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


static void
sort_column_changed_cb (GtkTreeSortable *sortable,
			gpointer         user_data)
{
	FrWindow    *window = user_data;
	GtkSortType  order;
	int          column_id;

	if (! gtk_tree_sortable_get_sort_column_id (sortable,
						    &column_id,
						    &order))
		return;

	window->priv->sort_method = get_sort_method_from_column (column_id);
	window->priv->sort_type = order;

	set_active (window, get_action_from_sort_method (window->priv->sort_method), TRUE);
	set_active (window, "SortReverseOrder", (window->priv->sort_type == GTK_SORT_DESCENDING));
}


static gboolean
fr_window_show_cb (GtkWidget *widget,
		   FrWindow  *window)
{
	gboolean view_foobar;

	fr_window_update_current_location (window);

	view_foobar = eel_gconf_get_boolean (PREF_UI_TOOLBAR, TRUE);
	set_active (window, "ViewToolbar", view_foobar);

	view_foobar = eel_gconf_get_boolean (PREF_UI_STATUSBAR, TRUE);
	set_active (window, "ViewStatusbar", view_foobar);

	return TRUE;
}


/* preferences changes notification callbacks */


static void
pref_history_len_changed (GConfClient *client,
			  guint        cnxn_id,
			  GConfEntry  *entry,
			  gpointer     user_data)
{
	FrWindow *window = user_data;

	gtk_recent_chooser_set_limit (GTK_RECENT_CHOOSER (window->priv->recent_chooser_menu), eel_gconf_get_integer (PREF_UI_HISTORY_LEN, MAX_HISTORY_LEN));
	gtk_recent_chooser_set_limit (GTK_RECENT_CHOOSER (window->priv->recent_chooser_toolbar), eel_gconf_get_integer (PREF_UI_HISTORY_LEN, MAX_HISTORY_LEN));
}


static void
pref_view_toolbar_changed (GConfClient *client,
			   guint        cnxn_id,
			   GConfEntry  *entry,
			   gpointer     user_data)
{
	FrWindow *window = user_data;

	g_return_if_fail (window != NULL);

	fr_window_set_toolbar_visibility (window, gconf_value_get_bool (gconf_entry_get_value (entry)));
}


static void
pref_view_statusbar_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
	FrWindow *window = user_data;

	fr_window_set_statusbar_visibility (window, gconf_value_get_bool (gconf_entry_get_value (entry)));
}


static void
pref_show_field_changed (GConfClient *client,
			 guint        cnxn_id,
			 GConfEntry  *entry,
			 gpointer     user_data)
{
	FrWindow *window = user_data;

	fr_window_update_columns_visibility (window);
}


static void
pref_click_policy_changed (GConfClient *client,
			   guint        cnxn_id,
			   GConfEntry  *entry,
			   gpointer     user_data)
{
	FrWindow   *window = user_data;
	GdkWindow  *win = gtk_tree_view_get_bin_window (GTK_TREE_VIEW (window->priv->list_view));
	GdkDisplay *display;

	window->priv->single_click = is_single_click_policy ();

	gdk_window_set_cursor (win, NULL);
	display = gtk_widget_get_display (GTK_WIDGET (window->priv->list_view));
	if (display != NULL)
		gdk_display_flush (display);
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
	FrWindow *window = user_data;

	if (pixbuf_hash != NULL) {
		g_hash_table_foreach (pixbuf_hash,
				      gh_unref_pixbuf,
				      NULL);
		g_hash_table_destroy (pixbuf_hash);
		pixbuf_hash = g_hash_table_new (g_str_hash, g_str_equal);
	}

	fr_window_update_file_list (window);
}


static void
theme_changed_cb (GtkIconTheme *theme, FrWindow *window)
{
	int icon_width, icon_height;

	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (GTK_WIDGET (window)),
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

	fr_window_update_file_list (window);
}


static gboolean
fr_window_progress_cb (FrCommand  *command,
		       double      fraction,
		       FrWindow   *window)
{
	if (fraction < 0.0)
		window->priv->progress_pulse = TRUE;

	else {
		window->priv->progress_pulse = FALSE;
		if (window->priv->progress_dialog != NULL)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->pd_progress_bar), CLAMP (fraction, 0.0, 1.0));
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->progress_bar), CLAMP (fraction, 0.0, 1.0));
	}

	return TRUE;
}




static gboolean
fr_window_stoppable_cb (FrCommand  *command,
			gboolean    stoppable,
			FrWindow   *window)
{
	window->priv->stoppable = stoppable;
	set_sensitive (window, "Stop", stoppable);
	if (window->priv->progress_dialog != NULL)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (window->priv->progress_dialog),
						   GTK_RESPONSE_OK,
						   stoppable);
	return TRUE;
}


static gboolean
fr_window_fake_load (FrArchive *archive,
		     gpointer   data)
{
	FrWindow *window = data;
	gboolean  add_after_opening = FALSE;
	gboolean  extract_after_opening = FALSE;
	GList    *scan;

	/* fake loads are used only in batch mode to avoid unnecessary
	 * archive loadings. */

	if (! window->priv->batch_mode)
		return FALSE;

	/* Check whether there is an ADD or EXTRACT action in the batch list. */

	for (scan = window->priv->batch_action; scan; scan = scan->next) {
		FRBatchAction *action;

		action = (FRBatchAction *) scan->data;
		if (action->type == FR_BATCH_ACTION_ADD) {
			add_after_opening = TRUE;
			break;
		}
		if (action->type == FR_BATCH_ACTION_EXTRACT) {
			extract_after_opening = TRUE;
			break;
		}
	}

	/* use fake load when in batch mode and the archive type supports all
	 * of the required features */

	return (window->priv->batch_mode
		&& ! (add_after_opening && window->priv->update_dropped_files && ! archive->command->propAddCanUpdate)
		&& ! (add_after_opening && ! window->priv->update_dropped_files && ! archive->command->propAddCanReplace)
		&& ! (extract_after_opening && !archive->command->propCanExtractAll));
}


static gboolean
fr_window_add_is_stoppable (FrArchive *archive,
			    gpointer   data)
{
	FrWindow *window = data;
	return window->priv->archive_new;
}


static GtkWidget*
create_locationbar_button (const char *stock_id,
			   gboolean    view_text)
{
	GtkWidget *button;
	GtkWidget *box;
	GtkWidget *image;

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button),
			       GTK_RELIEF_NONE);

	box = gtk_hbox_new (FALSE, 1);
	image = gtk_image_new ();
	gtk_image_set_from_stock (GTK_IMAGE (image),
				  stock_id,
				  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_box_pack_start (GTK_BOX (box), image, !view_text, FALSE, 0);

	if (view_text) {
		GtkStockItem  stock_item;
		const char   *text;
		GtkWidget    *label;

		if (gtk_stock_lookup (stock_id, &stock_item))
			text = stock_item.label;
		else
			text = "";
		label = gtk_label_new_with_mnemonic (text);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
	}

	gtk_container_add (GTK_CONTAINER (button), box);

	return button;
}


static void
menu_item_select_cb (GtkMenuItem *proxy,
		     FrWindow    *window)
{
	GtkAction *action;
	char      *message;

	action = g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);

	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message) {
		gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
				    window->priv->help_message_cid, message);
		g_free (message);
	}
}


static void
menu_item_deselect_cb (GtkMenuItem *proxy,
		       FrWindow    *window)
{
	gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
			   window->priv->help_message_cid);
}


static void
disconnect_proxy_cb (GtkUIManager *manager,
		     GtkAction    *action,
		     GtkWidget    *proxy,
		     FrWindow     *window)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_select_cb), window);
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_deselect_cb), window);
	}
}


static void
connect_proxy_cb (GtkUIManager *manager,
		  GtkAction    *action,
		  GtkWidget    *proxy,
		  FrWindow     *window)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_connect (proxy, "select",
				  G_CALLBACK (menu_item_select_cb), window);
		g_signal_connect (proxy, "deselect",
				  G_CALLBACK (menu_item_deselect_cb), window);
	}
}


static void
view_as_radio_action (GtkAction      *action,
		      GtkRadioAction *current,
		      gpointer        data)
{
	FrWindow *window = data;
	fr_window_set_list_mode (window, gtk_radio_action_get_current_value (current));
}


static void
sort_by_radio_action (GtkAction      *action,
		      GtkRadioAction *current,
		      gpointer        data)
{
	FrWindow *window = data;

	window->priv->sort_method = gtk_radio_action_get_current_value (current);
	window->priv->sort_type = GTK_SORT_ASCENDING;
	fr_window_update_list_order (window);
}


void
go_up_one_level_cb (GtkWidget *widget,
		    void      *data)
{
	fr_window_go_up_one_level ((FrWindow*) data);
}


void
go_home_cb (GtkWidget *widget,
	    void      *data)
{
	fr_window_go_to_location ((FrWindow*) data, "/");
}


void
go_back_cb (GtkWidget *widget,
	    void      *data)
{
	fr_window_go_back ((FrWindow*) data);
}


void
go_forward_cb (GtkWidget *widget,
	       void      *data)
{
	fr_window_go_forward ((FrWindow*) data);
}


static void
recent_chooser_item_activated_cb (GtkRecentChooser *chooser,
				  FrWindow         *window)
{
	char *uri;

	uri = gtk_recent_chooser_get_current_uri (chooser);
	if (uri != NULL) {
		fr_window_archive_open (window, uri, GTK_WINDOW (window));
		g_free (uri);
	}
}


static void
fr_window_init_recent_chooser (FrWindow         *window,
			       GtkRecentChooser *chooser)
{
	GtkRecentFilter *filter;
	int              i;

	g_return_if_fail (chooser != NULL);

	filter = gtk_recent_filter_new ();
	gtk_recent_filter_set_name (filter, _("All archives"));
	for (i = 0; open_type[i] != FR_FILE_TYPE_NULL; i++)
		gtk_recent_filter_add_mime_type (filter, file_type_desc[open_type[i]].mime_type);
	gtk_recent_filter_add_application (filter, "File Roller");
	gtk_recent_chooser_add_filter (chooser, filter);

	gtk_recent_chooser_set_local_only (chooser, FALSE);
	gtk_recent_chooser_set_limit (chooser, eel_gconf_get_integer (PREF_UI_HISTORY_LEN, MAX_HISTORY_LEN));
	gtk_recent_chooser_set_show_not_found (chooser, TRUE);
	gtk_recent_chooser_set_sort_type (chooser, GTK_RECENT_SORT_MRU);
	 
	g_signal_connect (G_OBJECT (chooser),
			  "item_activated",
			  G_CALLBACK (recent_chooser_item_activated_cb),
			  window);
}


static void
fr_window_construct (FrWindow *window)
{
	GtkWidget        *toolbar;
	GtkWidget        *scrolled_window;
	GtkWidget        *vbox;
	GtkWidget        *location_box;
	GtkTreeSelection *selection;
	int               i;
	int               icon_width, icon_height;
	GtkActionGroup   *actions;
	GtkUIManager     *ui;
	GtkToolItem      *open_recent_tool_item;
	GtkWidget        *menu_item;
	GError           *error = NULL;

	/* data common to all windows. */

	if (pixbuf_hash == NULL)
		pixbuf_hash = g_hash_table_new (g_str_hash, g_str_equal);

	if (icon_theme == NULL) 
		icon_theme = gtk_icon_theme_get_default ();

	/* Create the application. */

	gnome_app_construct (GNOME_APP (window), "main", _("Archive Manager"));
	gnome_window_icon_set_from_default (GTK_WINDOW (window));

	g_signal_connect (G_OBJECT (window),
			  "delete_event",
			  G_CALLBACK (fr_window_delete_event_cb),
			  window);

	g_signal_connect (G_OBJECT (window),
			  "show",
			  G_CALLBACK (fr_window_show_cb),
			  window);

	window->priv->theme_changed_handler_id =
		g_signal_connect (icon_theme,
				  "changed",
				  G_CALLBACK (theme_changed_cb),
				  window);

	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (GTK_WIDGET (window)),
					   ICON_GTK_SIZE,
					   &icon_width, &icon_height);

	icon_size = MAX (icon_width, icon_height);

	gtk_window_set_default_size (GTK_WINDOW (window),
				     eel_gconf_get_integer (PREF_UI_WINDOW_WIDTH, DEF_WIN_WIDTH),
				     eel_gconf_get_integer (PREF_UI_WINDOW_HEIGHT, DEF_WIN_HEIGHT));

	gtk_drag_dest_set (GTK_WIDGET (window),
			   GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table),
			   GDK_ACTION_COPY);

	g_signal_connect (G_OBJECT (window),
			  "drag_data_received",
			  G_CALLBACK (fr_window_drag_data_received),
			  window);
	g_signal_connect (G_OBJECT (window),
			  "drag_motion",
			  G_CALLBACK (fr_window_drag_motion),
			  window);

	g_signal_connect (G_OBJECT (window),
			  "key_press_event",
			  G_CALLBACK (key_press_cb),
			  window);

	/* Initialize Data. */

	window->archive = fr_archive_new ();
	g_signal_connect (G_OBJECT (window->archive),
			  "start",
			  G_CALLBACK (action_started),
			  window);
	g_signal_connect (G_OBJECT (window->archive),
			  "done",
			  G_CALLBACK (action_performed),
			  window);
	g_signal_connect (G_OBJECT (window->archive),
			  "progress",
			  G_CALLBACK (fr_window_progress_cb),
			  window);
	g_signal_connect (G_OBJECT (window->archive),
			  "message",
			  G_CALLBACK (fr_window_message_cb),
			  window);
	g_signal_connect (G_OBJECT (window->archive),
			  "stoppable",
			  G_CALLBACK (fr_window_stoppable_cb),
			  window);

	fr_archive_set_fake_load_func (window->archive,
				       fr_window_fake_load,
				       window);
	fr_archive_set_add_is_stoppable_func (window->archive,
					      fr_window_add_is_stoppable,
					      window);

	window->priv->sort_method = preferences_get_sort_method ();
	window->priv->sort_type = preferences_get_sort_type ();

	window->priv->list_mode = preferences_get_list_mode ();
	window->priv->history = NULL;
	window->priv->history_current = NULL;

	window->priv->action = FR_ACTION_NONE;

	eel_gconf_set_boolean (PREF_LIST_SHOW_PATH, (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT));

	window->priv->open_default_dir = g_strdup (get_home_uri ());
	window->priv->add_default_dir = g_strdup (get_home_uri ());
	window->priv->extract_default_dir = g_strdup (get_home_uri ());
	window->priv->view_folder_after_extraction = FALSE;

	window->priv->give_focus_to_the_list = FALSE;

	window->priv->activity_ref = 0;
	window->priv->activity_timeout_handle = 0;

	window->priv->update_timeout_handle = 0;

	window->priv->archive_present = FALSE;
	window->priv->archive_new = FALSE;
	window->priv->archive_uri = NULL;

	window->priv->drag_destination_folder = NULL;
	window->priv->drag_error = NULL;
	window->priv->drag_file_list = NULL;

	window->priv->batch_mode = FALSE;
	window->priv->batch_action_list = NULL;
	window->priv->batch_action = NULL;
	window->priv->extract_interact_use_default_dir = FALSE;
	window->priv->non_interactive = FALSE;

	window->priv->password = NULL;
	window->priv->compression = preferences_get_compression_level ();

	window->priv->convert_data.converting = FALSE;
	window->priv->convert_data.temp_dir = NULL;
	window->priv->convert_data.new_archive = NULL;

	window->priv->stoppable = TRUE;

	window->priv->batch_adding_one_file = FALSE;

	window->priv->path_clicked = NULL;

	window->priv->current_view_length = 0;

	window->priv->current_batch_action.type = FR_BATCH_ACTION_NONE;
	window->priv->current_batch_action.data = NULL;
	window->priv->current_batch_action.free_func = NULL;

	window->priv->pd_last_archive = NULL;

	/* Create the widgets. */

	/* * File list. */

	window->priv->list_store = fr_list_model_new (NUMBER_OF_COLUMNS,
						      G_TYPE_POINTER,
						      GDK_TYPE_PIXBUF,
						      G_TYPE_STRING,
						      GDK_TYPE_PIXBUF,
						      G_TYPE_STRING,
						      G_TYPE_STRING,
						      G_TYPE_STRING,
						      G_TYPE_STRING);
	g_object_set_data (G_OBJECT (window->priv->list_store), "FrWindow", window);
	window->priv->list_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (window->priv->list_store));

	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (window->priv->list_view), TRUE);
	add_columns (window, GTK_TREE_VIEW (window->priv->list_view));
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (window->priv->list_view),
					 TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (window->priv->list_view),
					 COLUMN_NAME);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_NAME, name_column_sort_func,
					 NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_SIZE, size_column_sort_func,
					 NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_TYPE, type_column_sort_func,
					 NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_TIME, time_column_sort_func,
					 NULL, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_PATH, path_column_sort_func,
					 NULL, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (selection_changed_cb),
			  window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "row_activated",
			  G_CALLBACK (row_activated_cb),
			  window);

	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "button_press_event",
			  G_CALLBACK (file_button_press_cb),
			  window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "button_release_event",
			  G_CALLBACK (file_button_release_cb),
			  window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "motion_notify_event",
			  G_CALLBACK (file_motion_notify_callback),
			  window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "leave_notify_event",
			  G_CALLBACK (file_leave_notify_callback),
			  window);

	g_signal_connect (G_OBJECT (window->priv->list_store),
			  "sort_column_changed",
			  G_CALLBACK (sort_column_changed_cb),
			  window);

	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "drag_begin",
			  G_CALLBACK (file_list_drag_begin),
			  window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "drag_end",
			  G_CALLBACK (file_list_drag_end),
			  window);

	egg_tree_multi_drag_add_drag_support (GTK_TREE_VIEW (window->priv->list_view));

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled_window), window->priv->list_view);

	/* * Location bar. */

	location_box = gtk_hbox_new (FALSE, 1);
	gtk_container_set_border_width (GTK_CONTAINER (location_box), 3);

	window->priv->location_bar = gnome_app_add_docked (GNOME_APP (window),
						     location_box,
						     "LocationBar",
						     (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL
						      | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE
						      | (eel_gconf_get_boolean (PREF_DESKTOP_TOOLBAR_DETACHABLE, TRUE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
						     BONOBO_DOCK_TOP,
						     3, 1, 0);

	/* buttons. */

	window->priv->tooltips = gtk_tooltips_new ();
	g_object_ref (G_OBJECT (window->priv->tooltips));
	gtk_object_sink (GTK_OBJECT (window->priv->tooltips));

	window->priv->back_button = create_locationbar_button (GTK_STOCK_GO_BACK, TRUE);
	gtk_tooltips_set_tip (window->priv->tooltips, window->priv->back_button, _("Go to the previous visited location"), NULL);
	gtk_box_pack_start (GTK_BOX (location_box), window->priv->back_button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (window->priv->back_button),
			  "clicked",
			  G_CALLBACK (go_back_cb),
			  window);

	window->priv->fwd_button = create_locationbar_button (GTK_STOCK_GO_FORWARD, FALSE);
	gtk_tooltips_set_tip (window->priv->tooltips, window->priv->fwd_button, _("Go to the next visited location"), NULL);
	gtk_box_pack_start (GTK_BOX (location_box), window->priv->fwd_button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (window->priv->fwd_button),
			  "clicked",
			  G_CALLBACK (go_forward_cb),
			  window);

	window->priv->up_button = create_locationbar_button (GTK_STOCK_GO_UP, FALSE);
	gtk_tooltips_set_tip (window->priv->tooltips, window->priv->up_button, _("Go up one level"), NULL);
	gtk_box_pack_start (GTK_BOX (location_box), window->priv->up_button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (window->priv->up_button),
			  "clicked",
			  G_CALLBACK (go_up_one_level_cb),
			  window);

	window->priv->home_button = create_locationbar_button (GTK_STOCK_HOME, FALSE);
	gtk_tooltips_set_tip (window->priv->tooltips, window->priv->home_button, _("Go to the home location"), NULL);
	gtk_box_pack_start (GTK_BOX (location_box), window->priv->home_button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (window->priv->home_button),
			  "clicked",
			  G_CALLBACK (go_home_cb),
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

	window->priv->location_label = gtk_label_new (_("Location:"));
	gtk_box_pack_start (GTK_BOX (location_box),
			    window->priv->location_label, FALSE, FALSE, 5);

	window->priv->location_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (location_box),
			    window->priv->location_entry, TRUE, TRUE, 5);

	g_signal_connect (G_OBJECT (window->priv->location_entry),
			  "key_press_event",
			  G_CALLBACK (location_entry_key_press_event_cb),
			  window);

	gtk_widget_show_all (window->priv->location_bar);

	gnome_app_set_contents (GNOME_APP (window), scrolled_window);
	gtk_widget_show_all (scrolled_window);

	/* Build the menu and the toolbar. */

	ui = gtk_ui_manager_new ();

	window->priv->actions = actions = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (actions, NULL);
	gtk_action_group_add_actions (actions,
				      action_entries,
				      n_action_entries,
				      window);
	gtk_action_group_add_toggle_actions (actions,
					     action_toggle_entries,
					     n_action_toggle_entries,
					     window);
	gtk_action_group_add_radio_actions (actions,
					    view_as_entries,
					    n_view_as_entries,
					    window->priv->list_mode,
					    G_CALLBACK (view_as_radio_action),
					    window);
	gtk_action_group_add_radio_actions (actions,
					    sort_by_entries,
					    n_sort_by_entries,
					    window->priv->sort_type,
					    G_CALLBACK (sort_by_radio_action),
					    window);

	g_signal_connect (ui, "connect_proxy",
			  G_CALLBACK (connect_proxy_cb), window);
	g_signal_connect (ui, "disconnect_proxy",
			  G_CALLBACK (disconnect_proxy_cb), window);

	gtk_ui_manager_insert_action_group (ui, actions, 0);
	gtk_window_add_accel_group (GTK_WINDOW (window),
				    gtk_ui_manager_get_accel_group (ui));

	if (!gtk_ui_manager_add_ui_from_string (ui, ui_info, -1, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}

	gnome_app_add_docked (GNOME_APP (window),
			      gtk_ui_manager_get_widget (ui, "/MenuBar"),
			      "MenuBar",
			      (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL
			       | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE
			       | (eel_gconf_get_boolean (PREF_DESKTOP_MENUBAR_DETACHABLE, TRUE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
			      BONOBO_DOCK_TOP,
			      1, 1, 0);
	window->priv->toolbar = toolbar = gtk_ui_manager_get_widget (ui, "/ToolBar");
	gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), TRUE);

	{
		GtkAction *action;

		action = gtk_ui_manager_get_action (ui, "/ToolBar/Extract_Toolbar");
		g_object_set (action, "is_important", TRUE, NULL);
		g_object_unref (action);
	}

	/* Recent manager */

	window->priv->recent_manager = gtk_recent_manager_get_default ();

	window->priv->recent_chooser_menu = gtk_recent_chooser_menu_new_for_manager (window->priv->recent_manager);
	gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (window->priv->recent_chooser_menu), GTK_RECENT_SORT_MRU);
	fr_window_init_recent_chooser (window, GTK_RECENT_CHOOSER (window->priv->recent_chooser_menu));
	menu_item = gtk_ui_manager_get_widget (ui, "/MenuBar/Archive/OpenRecentMenu");
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), window->priv->recent_chooser_menu);

	window->priv->recent_chooser_toolbar = gtk_recent_chooser_menu_new_for_manager (window->priv->recent_manager);
	fr_window_init_recent_chooser (window, GTK_RECENT_CHOOSER (window->priv->recent_chooser_toolbar));

	/* Add the recent menu tool item */

	open_recent_tool_item = gtk_menu_tool_button_new_from_stock (GTK_STOCK_OPEN);
	gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (open_recent_tool_item), window->priv->recent_chooser_toolbar);
	gtk_tool_item_set_homogeneous (open_recent_tool_item, FALSE);
	gtk_tool_item_set_tooltip (open_recent_tool_item, window->priv->tooltips, _("Open archive"), NULL);
	gtk_menu_tool_button_set_arrow_tooltip (GTK_MENU_TOOL_BUTTON (open_recent_tool_item), window->priv->tooltips,	_("Open a recently used archive"), NULL);

	window->priv->open_action = gtk_action_new ("Toolbar_Open", _("Open"), _("Open archive"), GTK_STOCK_OPEN);
	g_object_set (window->priv->open_action, "is_important", TRUE, NULL);
	g_signal_connect (window->priv->open_action,
			  "activate",
			  G_CALLBACK (activate_action_open),
			  window);
	gtk_action_connect_proxy (window->priv->open_action, GTK_WIDGET (open_recent_tool_item));

	gtk_widget_show (GTK_WIDGET (open_recent_tool_item));
	gtk_toolbar_insert (GTK_TOOLBAR (window->priv->toolbar), open_recent_tool_item, 1);

	/**/

	/*
	open_recent_tool_item = gtk_menu_tool_button_new_from_stock (FR_STOCK_ADD);
	gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (open_recent_tool_item),
				       gtk_ui_manager_get_widget (ui, "/AddMenu"));
	gtk_tool_item_set_homogeneous (open_recent_tool_item, FALSE);
	gtk_tool_item_set_tooltip (open_recent_tool_item, window->priv->tooltips, _("Add files to the archive"), NULL);
	gtk_menu_tool_button_set_arrow_tooltip (GTK_MENU_TOOL_BUTTON (open_recent_tool_item), window->priv->tooltips,  _("Add files to the archive"), NULL);
	gtk_action_connect_proxy (gtk_ui_manager_get_action (ui, "/Toolbar/AddFiles_Toolbar"),
				  GTK_WIDGET (open_recent_tool_item));

	gtk_widget_show (GTK_WIDGET (open_recent_tool_item));
	gtk_toolbar_insert (GTK_TOOLBAR (window->priv->toolbar),
			    open_recent_tool_item,
			    4);
	*/

	/**/

	gnome_app_add_docked (GNOME_APP (window),
			      toolbar,
			      "ToolBar",
			      (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL
			       | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE
			       | (eel_gconf_get_boolean (PREF_DESKTOP_TOOLBAR_DETACHABLE, TRUE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
			      BONOBO_DOCK_TOP,
			      2, 1, 0);

	window->priv->file_popup_menu = gtk_ui_manager_get_widget (ui, "/FilePopupMenu");
	window->priv->folder_popup_menu = gtk_ui_manager_get_widget (ui, "/FolderPopupMenu");

	/* Create the statusbar. */

	window->priv->statusbar = gtk_statusbar_new ();
	window->priv->help_message_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->priv->statusbar), "help_message");
	window->priv->list_info_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->priv->statusbar), "list_info");
	window->priv->progress_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->priv->statusbar), "progress");

	window->priv->progress_bar = gtk_progress_bar_new ();
	gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (window->priv->progress_bar), ACTIVITY_PULSE_STEP);
	gtk_box_pack_end (GTK_BOX (window->priv->statusbar), window->priv->progress_bar, FALSE, FALSE, 0);
	gnome_app_set_statusbar (GNOME_APP (window), window->priv->statusbar);
	gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (window->priv->statusbar), TRUE);

	/**/

	fr_window_update_title (window);
	fr_window_update_sensitivity (window);
	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
	fr_window_update_columns_visibility (window);

	/* Add notification callbacks. */

	i = 0;

	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_HISTORY_LEN,
					   pref_history_len_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_TOOLBAR,
					   pref_view_toolbar_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_STATUSBAR,
					   pref_view_statusbar_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_TYPE,
					   pref_show_field_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_SIZE,
					   pref_show_field_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_TIME,
					   pref_show_field_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_SHOW_PATH,
					   pref_show_field_changed,
					   window);
	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_LIST_USE_MIME_ICONS,
					   pref_use_mime_icons_changed,
					   window);

	window->priv->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_NAUTILUS_CLICK_POLICY,
					   pref_click_policy_changed,
					   window);

	/* Give focus to the list. */

	gtk_widget_grab_focus (window->priv->list_view);
}


GtkWidget *
fr_window_new (void)
{
	GtkWidget *window;

	window = g_object_new (FR_TYPE_WINDOW, NULL);
	fr_window_construct ((FrWindow*) window);

	return window;
}


gboolean
fr_window_archive_new (FrWindow   *window,
		       const char *uri)
{
	g_return_val_if_fail (window != NULL, FALSE);

	if (! fr_archive_create (window->archive, uri)) {
		GtkWindow *file_sel = g_object_get_data (G_OBJECT (window), "fr_file_sel");

		window->priv->load_error_parent_window = file_sel;
		fr_archive_action_completed (window->archive,
					     FR_ACTION_CREATING_NEW_ARCHIVE,
					     FR_PROC_ERROR_GENERIC,
					     _("Archive type not supported."));

		return FALSE;
	}

	if (window->priv->archive_uri != NULL)
		g_free (window->priv->archive_uri);
	window->priv->archive_uri = g_strdup (uri);
	window->priv->archive_present = TRUE;
	window->priv->archive_new = TRUE;

	fr_archive_action_completed (window->archive,
				     FR_ACTION_CREATING_NEW_ARCHIVE,
				     FR_PROC_ERROR_NONE,
				     NULL);

	return TRUE;
}


FrWindow *
fr_window_archive_open (FrWindow   *current_window,
			const char *uri,
			GtkWindow  *parent)
{
	FrWindow *window = current_window;
	gboolean  new_window_created = FALSE;

	if (current_window->priv->archive_present) {
		new_window_created = TRUE;
		window = (FrWindow *) fr_window_new ();
	}

	g_return_val_if_fail (window != NULL, FALSE);

	fr_window_archive_close (window);

	g_free (window->priv->archive_uri);
	if (! uri_has_scheme (uri)) {
		char *path;
		
		if (! g_path_is_absolute (uri)) {
			char *current_dir;
			
			current_dir = g_get_current_dir ();
			path = g_strconcat (current_dir, "/", uri, NULL);
			g_free (current_dir);
		}
		else 
			path = g_strdup (uri);
		
		window->priv->archive_uri = get_uri_from_local_path (path);
		 
		g_free (path);
	}
	else
		window->priv->archive_uri = g_strdup (uri);

	window->priv->archive_present = FALSE;
	window->priv->give_focus_to_the_list = TRUE;
	window->priv->load_error_parent_window = parent;

	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_LOAD,
					    g_strdup (window->priv->archive_uri),
					    (GFreeFunc) g_free);

	fr_archive_load (window->archive, window->priv->archive_uri, window->priv->password);

	return window;
}


void
fr_window_archive_close (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (! window->priv->archive_new && ! window->priv->archive_present)
		return;

	fr_clipboard_data_free (window->priv->copy_data);
	window->priv->copy_data = NULL;
	
	fr_window_set_password (window, NULL);

	window->priv->archive_new = FALSE;
	window->priv->archive_present = FALSE;

	fr_window_update_title (window);
	fr_window_update_sensitivity (window);
	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
	fr_window_update_statusbar_list_info (window);
}


const char *
fr_window_get_archive_uri (FrWindow *window)
{
	g_return_val_if_fail (window != NULL, NULL);

	return window->priv->archive_uri;
}


const char *
fr_window_get_paste_archive_uri (FrWindow *window)
{
	g_return_val_if_fail (window != NULL, NULL);

	if (window->priv->clipboard_data != NULL)
		return window->priv->clipboard_data->archive_filename;
	else
		return NULL;
}


gboolean
fr_window_archive_is_present (FrWindow *window)
{
	g_return_val_if_fail (window != NULL, FALSE);

	return window->priv->archive_present;
}


void
fr_window_archive_save_as (FrWindow   *window,
			   const char *uri)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (uri != NULL);
	g_return_if_fail (window->archive != NULL);

	if (window->priv->convert_data.temp_dir != NULL) {
		g_free (window->priv->convert_data.temp_dir);
		window->priv->convert_data.temp_dir = NULL;
	}
	
	if (window->priv->convert_data.new_archive != NULL) {
		g_object_unref (window->priv->convert_data.new_archive);
		window->priv->convert_data.new_archive = NULL;
	}

	/* create the new archive */

	window->priv->convert_data.new_archive = fr_archive_new ();
	if (! fr_archive_create (window->priv->convert_data.new_archive, uri)) {
		GtkWidget *d;
		char      *utf8_name;
		char      *message;

		utf8_name = g_filename_display_basename (uri);
		message = g_strdup_printf (_("Could not save the archive \"%s\""), file_name_from_path (uri));
		g_free (utf8_name);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   message,
					   _("Archive type not supported."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);

		g_free (message);

		g_object_unref (window->priv->convert_data.new_archive);
		window->priv->convert_data.new_archive = NULL;

		return;
	}

	g_return_if_fail (window->priv->convert_data.new_archive->command != NULL);

	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_SAVE_AS,
					    g_strdup (uri),
					    (GFreeFunc) g_free);

	g_signal_connect (G_OBJECT (window->priv->convert_data.new_archive),
			  "start",
			  G_CALLBACK (action_started),
			  window);
	g_signal_connect (G_OBJECT (window->priv->convert_data.new_archive),
			  "done",
			  G_CALLBACK (convert__action_performed),
			  window);
	g_signal_connect (G_OBJECT (window->priv->convert_data.new_archive),
			  "progress",
			  G_CALLBACK (fr_window_progress_cb),
			  window);
	g_signal_connect (G_OBJECT (window->priv->convert_data.new_archive),
			  "message",
			  G_CALLBACK (fr_window_message_cb),
			  window);
	g_signal_connect (G_OBJECT (window->priv->convert_data.new_archive),
			  "stoppable",
			  G_CALLBACK (fr_window_stoppable_cb),
			  window);

	window->priv->convert_data.converting = TRUE;
	window->priv->convert_data.temp_dir = get_temp_work_dir ();

	fr_process_clear (window->archive->process);
	fr_archive_extract (window->archive,
			    NULL,
			    window->priv->convert_data.temp_dir,
			    NULL,
			    TRUE,
			    FALSE,
			    FALSE,
			    window->priv->password);
	fr_process_start (window->archive->process);
}


void
fr_window_archive_reload (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->priv->activity_ref > 0)
		return;

	if (window->priv->archive_new)
		return;

	fr_archive_reload (window->archive, window->priv->password);
}


void
fr_window_archive_rename (FrWindow   *window,
			  const char *uri)
{
	g_return_if_fail (window != NULL);

	if (window->priv->archive_new) {
		fr_window_archive_new (window, uri);
		return;
	}

	fr_archive_rename (window->archive, uri);

	if (window->priv->archive_uri != NULL)
		g_free (window->priv->archive_uri);
	window->priv->archive_uri = g_strdup (uri);

	fr_window_update_title (window);
	fr_window_add_to_recent_list (window, window->priv->archive_uri);
}


/**/


void
fr_window_archive_add_files (FrWindow *window,
			     GList    *file_list,
			     gboolean  update)
{
	GList *files = NULL;
	GList *scan;
	char  *base_dir;
	int    base_len;

	base_dir = remove_level_from_path (file_list->data);

	base_len = 0;
	if (strcmp (base_dir, "/") != 0)
		base_len = strlen (base_dir);

	for (scan = file_list; scan; scan = scan->next) {
		char *path = scan->data;
		char *rel_path;

		rel_path = g_strdup (path + base_len + 1);
		files = g_list_prepend (files, rel_path);
	}

	fr_archive_add_files (window->archive,
			      files,
			      base_dir,
			      fr_window_get_current_location (window),
			      update,
			      window->priv->password,
			      window->priv->compression);

	path_list_free (files);
	g_free (base_dir);
}


void
fr_window_archive_add_with_wildcard (FrWindow      *window,
				     const char    *include_files,
				     const char    *exclude_files,
				     const char    *base_dir,
				     const char    *dest_dir,
				     gboolean       update,
				     gboolean       recursive,
				     gboolean       follow_links)
{
	fr_archive_add_with_wildcard (window->archive,
				      include_files,
				      exclude_files,
				      base_dir,
				      (dest_dir == NULL)? fr_window_get_current_location (window): dest_dir,
				      update,
				      recursive,
				      follow_links,
				      window->priv->password,
				      window->priv->compression);
}


void
fr_window_archive_add_directory (FrWindow      *window,
				 const char    *directory,
				 const char    *base_dir,
				 const char    *dest_dir,
				 gboolean       update,
				 const char    *password,
				 FRCompression  compression)
{
	fr_archive_add_directory (window->archive,
				  directory,
				  base_dir,
				  (dest_dir == NULL)? fr_window_get_current_location (window): dest_dir,
				  update,
				  password,
				  compression);
}


void
fr_window_archive_add_items (FrWindow      *window,
			     GList         *item_list,
			     const char    *base_dir,
			     const char    *dest_dir,
			     gboolean       update,
			     const char    *password,
			     FRCompression  compression)
{
	fr_archive_add_items (window->archive,
			      item_list,
			      base_dir,
			      (dest_dir == NULL)? fr_window_get_current_location (window): dest_dir,
			      update,
			      password,
			      compression);
}


void
fr_window_archive_add_dropped_items (FrWindow *window,
				     GList    *item_list,
				     gboolean  update)
{
	fr_archive_add_dropped_items (window->archive,
				      item_list,
				      fr_window_get_current_location (window),
				      fr_window_get_current_location (window),
				      update,
				      window->priv->password,
				      window->priv->compression);
}


void
fr_window_archive_remove (FrWindow      *window,
			  GList         *file_list,
			  FRCompression  compression)
{
	fr_window_clipboard_remove_file_list (window, file_list);

	fr_process_clear (window->archive->process);
	fr_archive_remove (window->archive, file_list, compression);
	fr_process_start (window->archive->process);
}


/* -- window_archive_extract -- */


static ExtractData*
extract_data_new (GList      *file_list,
		  const char *extract_to_dir,
		  const char *base_dir,
		  gboolean    skip_older,
		  gboolean    overwrite,
		  gboolean    junk_paths,
		  const char *password,
		  gboolean    extract_here)
{
	ExtractData *edata;

	edata = g_new0 (ExtractData, 1);
	edata->file_list = path_list_dup (file_list);
	if (extract_to_dir != NULL)
		edata->extract_to_dir = g_strdup (extract_to_dir);
	edata->skip_older = skip_older;
	edata->overwrite = overwrite;
	edata->junk_paths = junk_paths;
	if (base_dir != NULL)
		edata->base_dir = g_strdup (base_dir);
	if (password != NULL)
		edata->password = g_strdup (password);
	edata->extract_here = extract_here;

	return edata;
}


static ExtractData*
extract_to_data_new (const char *extract_to_dir)
{
	return extract_data_new (NULL,
				 extract_to_dir,
				 NULL,
				 FALSE,
				 TRUE,
				 FALSE,
				 NULL,
				 FALSE);
}


static void
extract_data_free (ExtractData *edata)
{
	g_return_if_fail (edata != NULL);

	path_list_free (edata->file_list);
	g_free (edata->extract_to_dir);
	g_free (edata->base_dir);
	g_free (edata->password);

	g_free (edata);
}


void
fr_window_archive_extract_here (FrWindow   *window,
				gboolean    skip_older,
				gboolean    overwrite,
				gboolean    junk_paths,
				const char *password)
{
	ExtractData *edata;
		
	edata = extract_data_new (NULL,
				  NULL,
				  NULL,
				  skip_older,
				  overwrite,
				  junk_paths,
				  password,
				  TRUE);

	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_EXTRACT,
					    edata,
					    (GFreeFunc) extract_data_free);
					    	
	fr_process_clear (window->archive->process);
	if (fr_archive_extract_here (window->archive,
			             edata->skip_older,
			             edata->overwrite,
			             edata->junk_paths,
			             edata->password))
		fr_process_start (window->archive->process);
}


void
fr_window_archive_extract (FrWindow   *window,
			   GList      *file_list,
			   const char *extract_to_dir,
			   const char *base_dir,
			   gboolean    skip_older,
			   gboolean    overwrite,
			   gboolean    junk_paths,
			   const char *password)
{
	ExtractData *edata;
	gboolean     do_not_extract = FALSE;

	edata = extract_data_new (file_list,
				  extract_to_dir,
				  base_dir,
				  skip_older,
				  overwrite,
				  junk_paths,
				  password,
				  FALSE);

	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_EXTRACT,
					    edata,
					    (GFreeFunc) extract_data_free);

	if (! path_is_dir (edata->extract_to_dir)) {
		if (! ForceDirectoryCreation) {
			GtkWidget *d;
			int        r;
			char      *folder_name;
			char      *msg;

			folder_name = g_filename_display_name (edata->extract_to_dir);
			msg = g_strdup_printf (_("Destination folder \"%s\" does not exist.\n\nDo you want to create it?"), folder_name);
			g_free (folder_name);

			d = _gtk_message_dialog_new (GTK_WINDOW (window),
						     GTK_DIALOG_MODAL,
						     GTK_STOCK_DIALOG_QUESTION,
						     msg,
						     NULL,
						     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						     _("Create _Folder"), GTK_RESPONSE_YES,
						     NULL);

			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
			r = gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));

			g_free (msg);

			if (r != GTK_RESPONSE_YES)
				do_not_extract = TRUE;
		}

		if (! do_not_extract && ! ensure_dir_exists (edata->extract_to_dir, 0755)) {
			GtkWidget  *d;

			d = _gtk_error_dialog_new (GTK_WINDOW (window),
						   GTK_DIALOG_MODAL,
						   NULL,
						   _("Extraction not performed"),
						   _("Could not create the destination folder: %s."),
						   gnome_vfs_result_to_string (gnome_vfs_result_from_errno ()));
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (GTK_WIDGET (d));
			
			fr_window_stop_batch (window);

			return;
		}
	}

	if (do_not_extract) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_WARNING,
					     _("Extraction not performed"),
					     NULL,
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		
		fr_window_stop_batch (window);

		return;
	}

	fr_process_clear (window->archive->process);

	fr_archive_extract (window->archive,
			    edata->file_list,
			    edata->extract_to_dir,
			    edata->base_dir,
			    edata->skip_older,
			    edata->overwrite,
			    edata->junk_paths,
			    edata->password);
				    
	fr_process_start (window->archive->process);
}


void
fr_window_archive_test (FrWindow *window)
{
	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_TEST,
					    NULL,
					    NULL);
					    
	fr_archive_test (window->archive, window->priv->password);
}


void
fr_window_set_password (FrWindow   *window,
			const char *password)
{
	g_return_if_fail (window != NULL);

	if (window->priv->password != NULL) {
		g_free (window->priv->password);
		window->priv->password = NULL;
	}

	if ((password != NULL) && (password[0] != '\0'))
		window->priv->password = g_strdup (password);
}

void
fr_window_set_password_for_paste (FrWindow   *window,
				  const char *password)
{
	g_return_if_fail (window != NULL);

	if (window->priv->password_for_paste != NULL) {
		g_free (window->priv->password_for_paste);
		window->priv->password_for_paste = NULL;
	}

	if ((password != NULL) && (password[0] != '\0'))
		window->priv->password_for_paste = g_strdup (password);
}

const char *
fr_window_get_password (FrWindow *window)
{
	g_return_val_if_fail (window != NULL, NULL);

	return window->priv->password;
}


FRCompression
fr_window_get_compression (FrWindow *window)
{
	return window->priv->compression;
}


void
fr_window_view_folder_after_extract (FrWindow *window,
				     gboolean  view)
{
	window->priv->view_folder_after_extraction = view;
}


void
fr_window_go_to_location (FrWindow   *window,
			  const char *path)
{
	char *dir;

	g_return_if_fail (window != NULL);
	g_return_if_fail (path != NULL);

	if (path[strlen (path) - 1] != '/')
		dir = g_strconcat (path, "/", NULL);
	else
		dir = g_strdup (path);
	fr_window_history_add (window, dir);
	g_free (dir);

	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
}


const char *
fr_window_get_current_location (FrWindow *window)
{
	if (window->priv->history_current == NULL) {
		fr_window_history_add (window, "/");
		return window->priv->history_current->data;
	}
	else
		return (const char*) window->priv->history_current->data;
}


void
fr_window_go_up_one_level (FrWindow *window)
{
	const char *current_dir;
	char       *parent_dir;

	g_return_if_fail (window != NULL);

	current_dir = fr_window_get_current_location (window);
	parent_dir = get_parent_dir (current_dir);
	fr_window_history_add (window, parent_dir);
	g_free (parent_dir);

	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
}


void
fr_window_go_back (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->priv->history == NULL)
		return;
	if (window->priv->history_current == NULL)
		return;
	if (window->priv->history_current->next == NULL)
		return;
	window->priv->history_current = window->priv->history_current->next;

	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
}


void
fr_window_go_forward (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->priv->history == NULL)
		return;
	if (window->priv->history_current == NULL)
		return;
	if (window->priv->history_current->prev == NULL)
		return;
	window->priv->history_current = window->priv->history_current->prev;

	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
}


void
fr_window_set_list_mode (FrWindow         *window,
			 FRWindowListMode  list_mode)
{
	g_return_if_fail (window != NULL);

	window->priv->list_mode = list_mode;
	if (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
		fr_window_history_clear (window);
		fr_window_history_add (window, "/");
	}

	preferences_set_list_mode (window->priv->list_mode);
	eel_gconf_set_boolean (PREF_LIST_SHOW_PATH, (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT));

	fr_window_update_file_list (window);
	fr_window_update_current_location (window);
}


/* -- window_get_file_list_selection -- */


static GList *
get_dir_list (FrWindow *window,
	      FileData *fdata)
{
	GList *list;
	GList *scan;
	char  *dirname;
	int    dirname_l;

	dirname = g_strconcat (fr_window_get_current_location (window),
			       fdata->list_name,
			       "/",
			       NULL);
	dirname_l = strlen (dirname);

	list = NULL;
	for (scan = window->archive->command->file_list; scan; scan = scan->next) {
		FileData *fd = scan->data;

		if (strncmp (dirname, fd->full_path, dirname_l) == 0)
			list = g_list_prepend (list, g_strdup (fd->original_path));
	}

	g_free (dirname);

	return g_list_reverse (list);
}


GList *
fr_window_get_file_list_selection (FrWindow *window,
				   gboolean  recursive,
				   gboolean *has_dirs)
{
	GtkTreeSelection *selection;
	GList            *selections = NULL, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return NULL;
	gtk_tree_selection_selected_foreach (selection, add_selected, &selections);

	list = NULL;
	for (scan = selections; scan; scan = scan->next) {
		FileData *fd = scan->data;

		if (!fd)
			continue;

		if (file_data_is_dir (fd)) {
			if (has_dirs != NULL)
				*has_dirs = TRUE;

			if (recursive)
				list = g_list_concat (list, get_dir_list (window, fd));
		}
		else
			list = g_list_prepend (list, g_strdup (fd->original_path));
	}
	if (selections)
		g_list_free (selections);

	return g_list_reverse (list);
}


/* -- -- */


GList *
fr_window_get_file_list_from_path_list (FrWindow *window,
					GList    *path_list,
					gboolean *has_dirs)
{
	GtkTreeModel *model;
	GList        *selections, *list, *scan;

	g_return_val_if_fail (window != NULL, NULL);

	model = GTK_TREE_MODEL (window->priv->list_store);
	selections = NULL;

	if (has_dirs != NULL)
		*has_dirs = FALSE;

	for (scan = path_list; scan; scan = scan->next) {
		GtkTreeRowReference *reference = scan->data;
		GtkTreePath         *path;
		GtkTreeIter          iter;
		FileData            *fdata;

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
		FileData *fd = scan->data;

		if (!fd)
			continue;

		if (file_data_is_dir (fd)) {
			if (has_dirs != NULL)
				*has_dirs = TRUE;
			list = g_list_concat (list, get_dir_list (window, fd));
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
	GList  *list, *scan;
	char  **patterns;

	g_return_val_if_fail (window != NULL, NULL);

	patterns = search_util_get_patterns (pattern);

	list = NULL;
	for (scan = window->archive->command->file_list; scan; scan = scan->next) {
		FileData *fd = scan->data;
		char     *utf8_name;

		/* FIXME: only files in the current location ? */

		if (fd == NULL)
			continue;

		utf8_name = g_filename_to_utf8 (fd->name, -1, NULL, NULL, NULL);
		if (match_patterns (patterns, utf8_name, 0))
			list = g_list_prepend (list, g_strdup (fd->original_path));
		g_free (utf8_name);
	}

	if (patterns != NULL)
		g_strfreev (patterns);

	return g_list_reverse (list);
}


int
fr_window_get_n_selected_files (FrWindow *window)
{
	return _gtk_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view)));
}


GtkListStore *
fr_window_get_list_store (FrWindow *window)
{
	return window->priv->list_store;
}


void
fr_window_select_all (FrWindow *window)
{
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view)));
}


void
fr_window_unselect_all (FrWindow *window)
{
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view)));
}


void
fr_window_set_sort_type (FrWindow     *window,
			 GtkSortType   sort_type)
{
	window->priv->sort_type = sort_type;
	fr_window_update_list_order (window);
}


void
fr_window_stop (FrWindow *window)
{
	if (! window->priv->stoppable)
		return;

	if (window->priv->activity_ref > 0)
		fr_archive_stop (window->archive);
		
	if (window->priv->convert_data.converting)
		fr_window_convert_data_free (window);		
}


/* -- start/stop activity mode -- */


static int
activity_cb (gpointer data)
{
	FrWindow *window = data;

	if ((window->priv->pd_progress_bar != NULL) && window->priv->progress_pulse)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (window->priv->pd_progress_bar));
	if (window->priv->progress_pulse)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (window->priv->progress_bar));

	return TRUE;
}


void
fr_window_start_activity_mode (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->priv->activity_ref++ > 0)
		return;

	window->priv->activity_timeout_handle = gtk_timeout_add (ACTIVITY_DELAY,
								 activity_cb,
								 window);
	fr_window_update_sensitivity (window);
}


void
fr_window_stop_activity_mode (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (--window->priv->activity_ref > 0)
		return;

	if (window->priv->activity_timeout_handle == 0)
		return;

	gtk_timeout_remove (window->priv->activity_timeout_handle);
	window->priv->activity_timeout_handle = 0;

	if (window->priv->progress_dialog != NULL)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->pd_progress_bar), 0.0);

	if (! window->priv->batch_mode) {
		if (window->priv->progress_bar != NULL)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->progress_bar), 0.0);
		fr_window_update_sensitivity (window);
	}
}


static gboolean
last_output_window__unrealize_cb (GtkWidget  *widget,
				  gpointer    data)
{
	pref_util_save_window_geometry (GTK_WINDOW (widget), LAST_OUTPUT_DIALOG_NAME);
	return FALSE;
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

	dialog = gtk_dialog_new_with_buttons (title,
					      GTK_WINDOW (window),
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

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

	text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buffer, "monospace",
				    "family", "monospace", NULL);

	text_view = gtk_text_view_new_with_buffer (text_buffer);
	g_object_unref (text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);

	/**/

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

	gtk_container_add (GTK_CONTAINER (scrolled), text_view);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled,
			    TRUE, TRUE, 0);

	gtk_widget_show_all (vbox);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    vbox,
			    TRUE, TRUE, 0);

	/* signals */

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	g_signal_connect (G_OBJECT (dialog),
			  "unrealize",
			  G_CALLBACK (last_output_window__unrealize_cb),
			  NULL);

	/**/

	gtk_text_buffer_get_iter_at_offset (text_buffer, &iter, 0);
	scan = window->archive->process->raw_output;
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

	pref_util_restore_window_geometry (GTK_WINDOW (dialog), LAST_OUTPUT_DIALOG_NAME);
}


/* -- fr_window_rename_selection -- */


typedef struct {
	GList    *file_list;
	char     *old_name;
	char     *new_name;
	gboolean  is_dir;
	char     *current_dir;
} RenameData;


static RenameData*
rename_data_new (GList      *file_list,
		 const char *old_name,
		 const char *new_name,
		 gboolean    is_dir,
		 const char *current_dir)
{
	RenameData *rdata;

	rdata = g_new0 (RenameData, 1);
	rdata->file_list = path_list_dup (file_list);
	if (old_name != NULL)
		rdata->old_name = g_strdup (old_name);
	if (new_name != NULL)
		rdata->new_name = g_strdup (new_name);
	rdata->is_dir = is_dir;
	if (current_dir != NULL)
		rdata->current_dir = g_strdup (current_dir);

	return rdata;
}


static void
rename_data_free (RenameData *rdata)
{
	g_return_if_fail (rdata != NULL);

	path_list_free (rdata->file_list);
	g_free (rdata->old_name);
	g_free (rdata->new_name);
	g_free (rdata->current_dir);

	g_free (rdata);
}


static void
rename_selection (FrWindow   *window,
		  GList      *file_list,
		  const char *old_name,
		  const char *new_name,
		  gboolean    is_dir,
		  const char *current_dir)
{
	char       *tmp_dir;
	char       *e_tmp_dir;
	FrArchive  *archive = window->archive;
	GList      *scan, *new_file_list = NULL;
	RenameData *rdata;

	rdata = rename_data_new (file_list,
				 old_name,
				 new_name,
				 is_dir,
				 current_dir);
	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_RENAME,
					    rdata,
					    (GFreeFunc) rename_data_free);

	fr_process_clear (archive->process);

	tmp_dir = get_temp_work_dir ();
	e_tmp_dir = shell_escape (tmp_dir);

	fr_archive_extract (archive,
			    rdata->file_list,
			    tmp_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    window->priv->password);

	fr_archive_remove (archive,
			   rdata->file_list,
			   window->priv->compression);

	fr_window_clipboard_remove_file_list (window, rdata->file_list);

	/* rename files. */

	if (rdata->is_dir) {
		char *old_path, *new_path;
		char *e_old_path, *e_new_path;

		old_path = g_build_filename (tmp_dir, rdata->current_dir, rdata->old_name, NULL);
		new_path = g_build_filename (tmp_dir, rdata->current_dir, rdata->new_name, NULL);

		e_old_path = shell_escape (old_path);
		e_new_path = shell_escape (new_path);

		fr_process_begin_command (archive->process, "mv");
		fr_process_add_arg (archive->process, "-f");
		fr_process_add_arg (archive->process, e_old_path);
		fr_process_add_arg (archive->process, e_new_path);
		fr_process_end_command (archive->process);

		g_free (old_path);
		g_free (new_path);
		g_free (e_old_path);
		g_free (e_new_path);
	}

	for (scan = rdata->file_list; scan; scan = scan->next) {
		const char *current_dir_relative = rdata->current_dir + 1;
		const char *filename = (char*) scan->data;
		char       *old_path = NULL, *common = NULL, *new_path = NULL;

		old_path = g_build_filename (tmp_dir, filename, NULL);

		if (strlen (filename) > (strlen (rdata->current_dir) + strlen (rdata->old_name)))
			common = g_strdup (filename + strlen (rdata->current_dir) + strlen (rdata->old_name));
		new_path = g_build_filename (tmp_dir, rdata->current_dir, rdata->new_name, common, NULL);

		if (! rdata->is_dir) {
			char *e_old_path, *e_new_path;

			e_old_path = shell_escape (old_path);
			e_new_path = shell_escape (new_path);

			fr_process_begin_command (archive->process, "mv");
			fr_process_add_arg (archive->process, "-f");
			fr_process_add_arg (archive->process, e_old_path);
			fr_process_add_arg (archive->process, e_new_path);
			fr_process_end_command (archive->process);

			g_free (e_old_path);
			g_free (e_new_path);
		}

		new_file_list = g_list_prepend (new_file_list, g_build_filename (current_dir_relative, rdata->new_name, common, NULL));

		g_free (old_path);
		g_free (common);
		g_free (new_path);
	}

	new_file_list = g_list_reverse (new_file_list);

	fr_archive_add (archive,
			new_file_list,
			tmp_dir,
			NULL,
			FALSE,
			window->priv->password,
			window->priv->compression);

	/* remove the tmp dir */

	fr_process_begin_command (archive->process, "rm");
	fr_process_set_working_dir (archive->process, g_get_tmp_dir());
	fr_process_set_sticky (archive->process, TRUE);
	fr_process_add_arg (archive->process, "-rf");
	fr_process_add_arg (archive->process, e_tmp_dir);
	fr_process_end_command (archive->process);

	fr_process_start (archive->process);

	g_free (tmp_dir);
	g_free (e_tmp_dir);
}


static gboolean
valid_name (const char  *new_name,
	    const char  *old_name,
	    char       **reason)
{
	char     *utf8_new_name;
	gboolean  retval = TRUE;

	new_name = eat_spaces (new_name);
	utf8_new_name = g_filename_display_name (new_name);

	if (*new_name == '\0') {
		*reason = g_strdup_printf ("%s\n\n%s", _("The new name is void."), _("Please use a different name."));
		retval = FALSE;
	}
	else if (strcmp (new_name, old_name) == 0) {
		*reason = g_strdup_printf ("%s\n\n%s", _("The new name is equal to the old one."), _("Please use a different name."));
		retval = FALSE;
	}
	else if (strchrs (new_name, BAD_CHARS)) {
		*reason = g_strdup_printf (_("The name \"%s\" is not valid because it cannot contain the characters: %s\n\n%s"), utf8_new_name, BAD_CHARS, _("Please use a different name."));
		retval = FALSE;
	}

	g_free (utf8_new_name);

	return retval;
}


static char *
get_first_level_dir (const char *path,
		     const char *current_dir)
{
	const char *from_current;
	const char *first_sep;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (current_dir != NULL, NULL);

	from_current = path + strlen (current_dir) - 1;
	first_sep = strchr (from_current, G_DIR_SEPARATOR);

	if (first_sep == NULL)
		return g_strdup (from_current);
	else
		return g_strndup (from_current, first_sep - from_current);
}


static gboolean
name_is_present (FrWindow    *window,
		 const char  *current_dir,
		 const char  *new_name,
		 char       **reason)
{
	gboolean  retval = FALSE;
	GList    *file_list, *scan;
	char     *new_filename;
	int       new_filename_l;

	*reason = NULL;

	new_filename = g_build_filename (current_dir, new_name, NULL);
	new_filename_l = strlen (new_filename);

	file_list = window->archive->command->file_list;
	for (scan = file_list; scan; scan = scan->next) {
		FileData   *file_data = (FileData *) scan->data;
		const char *filename = file_data->full_path;

		if ((strncmp (filename, new_filename, new_filename_l) == 0)
		    && ((filename[new_filename_l] == '\0')
			|| (filename[new_filename_l] == G_DIR_SEPARATOR))) {
			char *utf8_name = g_filename_display_name (new_name);

			if (filename[new_filename_l] == G_DIR_SEPARATOR)
				*reason = g_strdup_printf (_("A folder named \"%s\" already exists.\n\n%s"), utf8_name, _("Please use a different name."));
			else
				*reason = g_strdup_printf (_("A file named \"%s\" already exists.\n\n%s"), utf8_name, _("Please use a different name."));

			retval = TRUE;
			break;
		}
	}

	g_free (new_filename);

	return retval;
}


void
fr_window_rename_selection (FrWindow *window)
{
	GList    *selection, *selection_fd;
	gboolean  has_dir;
	char     *old_name, *utf8_old_name, *new_name, *utf8_new_name;
	char     *reason = NULL;
	char     *current_dir = NULL;

	selection = fr_window_get_file_list_selection (window, TRUE, &has_dir);
	if (selection == NULL)
		return;

	selection_fd = get_selection_as_fd (window);

	if (has_dir)
		old_name = get_first_level_dir ((char*) selection->data, fr_window_get_current_location (window));
	else
		old_name = g_strdup (file_name_from_path ((char*) selection->data));

 retry__rename_selection:
	utf8_old_name = g_locale_to_utf8 (old_name, -1 ,0 ,0 ,0);
	utf8_new_name = _gtk_request_dialog_run (GTK_WINDOW (window),
						 (GTK_DIALOG_DESTROY_WITH_PARENT
						  | GTK_DIALOG_MODAL),
						 _("Rename"),
						 (has_dir? _("New folder name"): _("New file name")),
						 utf8_old_name,
						 1024,
						 GTK_STOCK_CANCEL,
						 _("_Rename"));
	g_free (utf8_old_name);

	if (utf8_new_name == NULL)
		goto free_data__rename_selection;

	new_name = g_filename_from_utf8 (utf8_new_name, -1, 0, 0, 0);
	g_free (utf8_new_name);

	if (! valid_name (new_name, old_name, &reason)) {
		char      *utf8_name = g_filename_display_name (new_name);
		GtkWidget *dlg;

		dlg = _gtk_error_dialog_new (GTK_WINDOW (window),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     NULL,
					     (has_dir? _("Could not rename the folder"): _("Could not rename the file")),
					     reason);
		gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);

		g_free (reason);
		g_free (utf8_name);
		g_free (new_name);

		goto retry__rename_selection;
	}

	if (has_dir)
		current_dir = g_strdup (fr_window_get_current_location (window));
	else {
		FileData *fd = (FileData*) selection_fd->data;
		current_dir = g_strdup (fd->path);
	}

	if (name_is_present (window, current_dir, new_name, &reason)) {
		GtkWidget *dlg;
		int        r;

		dlg = _gtk_message_dialog_new (GTK_WINDOW (window),
					       GTK_DIALOG_MODAL,
					       GTK_STOCK_DIALOG_QUESTION,
					       (has_dir? _("Could not rename the folder"): _("Could not rename the file")),
					       reason,
					       GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
					       NULL);
		r = gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		g_free (reason);
		g_free (new_name);
		goto retry__rename_selection;
	}

	rename_selection (window, selection, old_name, new_name, has_dir, current_dir);

	g_free (current_dir);
	g_free (new_name);

free_data__rename_selection:
	g_free (old_name);
	path_list_free (selection);
	g_list_free (selection_fd);
}


static void
fr_clipboard_get (GtkClipboard     *clipboard,
                  GtkSelectionData *selection_data,
                  guint             info,
                  gpointer          user_data_or_owner)
{
	FrWindow *window = user_data_or_owner;
	char     *data;
	
	if (selection_data->target != FR_SPECIAL_URI_LIST)
		return;
	
	data = get_selection_data_from_clipboard_data (window, window->priv->copy_data);
	gtk_selection_data_set (selection_data,
				selection_data->target, 
				8, 
				(guchar *) data, 
				strlen (data));
	g_free (data);
}


static void
fr_clipboard_clear (GtkClipboard *clipboard,
                    gpointer      user_data_or_owner)
{
	FrWindow *window = user_data_or_owner;
	
	if (window->priv->copy_data != NULL) {
		fr_clipboard_data_free (window->priv->copy_data);
		window->priv->copy_data = NULL;
	}
}


static void
fr_window_copy_or_cut_selection (FrWindow      *window,
				 FRClipboardOp  op)
{
	GtkClipboard *clipboard;
	
	if (window->priv->copy_data != NULL)
		fr_clipboard_data_free (window->priv->copy_data);
	window->priv->copy_data = fr_clipboard_data_new ();
	window->priv->copy_data->files = fr_window_get_file_list_selection (window, TRUE, NULL);
	window->priv->copy_data->op = op;
	window->priv->copy_data->current_dir = g_strdup (fr_window_get_current_location (window));

	clipboard = gtk_clipboard_get (FR_CLIPBOARD);
	gtk_clipboard_set_with_owner (clipboard,
				      clipboard_targets,
				      G_N_ELEMENTS (clipboard_targets),
				      fr_clipboard_get,
				      fr_clipboard_clear,
				      G_OBJECT (window));
	
	fr_window_update_sensitivity (window);	
}


void
fr_window_copy_selection (FrWindow *window)
{
	fr_window_copy_or_cut_selection (window, FR_CLIPBOARD_OP_COPY);
}


void
fr_window_cut_selection (FrWindow *window)
{
	fr_window_copy_or_cut_selection (window, FR_CLIPBOARD_OP_CUT);
}


static gboolean
always_fake_load (FrArchive *archive,
	          gpointer   data)
{
	return TRUE;
}


static void 
add_pasted_files (FrWindow        *window,
		  FrClipboardData *data)
{
	const char *current_dir_relative = data->current_dir + 1;
	GList      *scan;
	char       *e_tmp_dir;
	GList      *new_file_list = NULL;
	
	if (window->priv->password_for_paste != NULL) {
		g_free (window->priv->password_for_paste);
		window->priv->password_for_paste = NULL;
	}
	
	fr_process_clear (window->archive->process);
	for (scan = data->files; scan; scan = scan->next) {
		const char *old_name = (char*) scan->data;
		char       *new_name = g_build_filename (current_dir_relative, old_name + strlen (data->base_dir) - 1, NULL);

		/* skip folders */

		if ((strcmp (old_name, new_name) != 0)
		    && (old_name[strlen (old_name) - 1] != '/')) {
			char *e_old_name = shell_escape (old_name);
			char *e_new_name = shell_escape (new_name);

			fr_process_begin_command (window->archive->process, "mv");
			fr_process_set_working_dir (window->archive->process, data->tmp_dir);
			fr_process_add_arg (window->archive->process, "-f");
			fr_process_add_arg (window->archive->process, e_old_name);
			fr_process_add_arg (window->archive->process, e_new_name);
			fr_process_end_command (window->archive->process);

			g_free (e_old_name);
			g_free (e_new_name);
		}

		new_file_list = g_list_prepend (new_file_list, new_name);
	}

	fr_archive_add (window->archive,
			new_file_list,
			data->tmp_dir,
			NULL,
			FALSE,
			window->priv->password,
			window->priv->compression);

	path_list_free (new_file_list);

	/* remove the tmp dir */

	e_tmp_dir = shell_escape (data->tmp_dir);
	fr_process_begin_command (window->archive->process, "rm");
	fr_process_set_working_dir (window->archive->process, g_get_tmp_dir ());
	fr_process_set_sticky (window->archive->process, TRUE);
	fr_process_add_arg (window->archive->process, "-rf");
	fr_process_add_arg (window->archive->process, e_tmp_dir);
	fr_process_end_command (window->archive->process);
	g_free (e_tmp_dir);
	
	fr_process_start (window->archive->process);
}


static void
copy_from_archive_action_performed_cb (FrArchive   *archive,
		  	   	       FRAction     action,
		  	   	       FRProcError *error,
		  	   	       gpointer     data)
{
	FrWindow *window = data;
	gboolean  continue_batch = FALSE;

#ifdef DEBUG
	debug (DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

	fr_window_stop_activity_mode (window);
	fr_window_pop_message (window);
	close_progress_dialog (window);

	if (error->type == FR_PROC_ERROR_ASK_PASSWORD) {
		dlg_ask_password_for_paste_operation (window);
		return;
	}

	continue_batch = handle_errors (window, archive, action, error);

	if (error->type != FR_PROC_ERROR_NONE) {
		fr_clipboard_data_free (window->priv->clipboard_data);
		window->priv->clipboard_data = NULL;
		return;
	}
	
	switch (action) {
	case FR_ACTION_LISTING_CONTENT:
		fr_process_clear (window->priv->copy_from_archive->process);
		fr_archive_extract (window->priv->copy_from_archive,
				    window->priv->clipboard_data->files,
				    window->priv->clipboard_data->tmp_dir,
				    NULL,
				    FALSE,
				    TRUE,
				    FALSE,
				    window->priv->clipboard_data->archive_password);
		fr_process_start (window->priv->copy_from_archive->process);
		break;
		
	case FR_ACTION_EXTRACTING_FILES:			    
		if (window->priv->clipboard_data->op == FR_CLIPBOARD_OP_CUT) {
			fr_process_clear (window->priv->copy_from_archive->process);
			fr_archive_remove (window->priv->copy_from_archive,
					   window->priv->clipboard_data->files,
					   window->priv->compression);
			fr_process_start (window->priv->copy_from_archive->process);
		} 
		else
			add_pasted_files (window, window->priv->clipboard_data);
		break;
		
	case FR_ACTION_DELETING_FILES:
		add_pasted_files (window, window->priv->clipboard_data);
		break;
		
	default:
		break;
	}
}


static void
fr_window_paste_from_clipboard_data (FrWindow        *window,
				     FrClipboardData *data)
{
	const char *current_dir_relative;
	GHashTable *created_dirs;
	GList      *scan;    
	
	if (window->priv->clipboard_data != data) {
		fr_clipboard_data_free (window->priv->clipboard_data);
		window->priv->clipboard_data = data;
	}
	
	current_dir_relative = data->current_dir + 1;
	
	data->tmp_dir = get_temp_work_dir ();
	created_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (scan = data->files; scan; scan = scan->next) {
		const char *old_name = (char*) scan->data;
		char       *new_name = g_build_filename (current_dir_relative, old_name + strlen (data->base_dir) - 1, NULL);
		char       *dir = remove_level_from_path (new_name);

		if (g_hash_table_lookup (created_dirs, dir) == NULL) {
			char *dir_path = g_build_filename (data->tmp_dir, dir, NULL);

			debug (DEBUG_INFO, "mktree %s\n", dir_path);

			ensure_dir_exists (dir_path, 0700);
			g_free (dir_path);
			g_hash_table_replace (created_dirs, g_strdup (dir), "1");
		}

		g_free (dir);
		g_free (new_name);
	}
	g_hash_table_destroy (created_dirs);

	/**/

	if (window->priv->copy_from_archive == NULL) {
		window->priv->copy_from_archive = fr_archive_new ();
		g_signal_connect (G_OBJECT (window->priv->copy_from_archive),
				  "start",
				  G_CALLBACK (action_started),
				  window);
		g_signal_connect (G_OBJECT (window->priv->copy_from_archive),
				  "done",
				  G_CALLBACK (copy_from_archive_action_performed_cb),
				  window);
		g_signal_connect (G_OBJECT (window->priv->copy_from_archive),
				  "progress",
				  G_CALLBACK (fr_window_progress_cb),
				  window);
		g_signal_connect (G_OBJECT (window->priv->copy_from_archive),
				  "message",
				  G_CALLBACK (fr_window_message_cb),
				  window);
		g_signal_connect (G_OBJECT (window->priv->copy_from_archive),
				  "stoppable",
				  G_CALLBACK (fr_window_stoppable_cb),
				  window);
		fr_archive_set_fake_load_func (window->priv->copy_from_archive, always_fake_load, NULL);
	}

	fr_archive_load_local (window->priv->copy_from_archive,
		               data->archive_filename,
			       data->archive_password);	
}


static void
fr_window_paste_selection_to (FrWindow   *window,
			      const char *current_dir)
{
	GtkClipboard      *clipboard;
	GtkSelectionData  *selection_data;       
	FrClipboardData   *paste_data;
	
	clipboard = gtk_clipboard_get (FR_CLIPBOARD);
	selection_data = gtk_clipboard_wait_for_contents (clipboard, FR_SPECIAL_URI_LIST);
	if (selection_data == NULL)
		return;

	paste_data = fr_clipboard_data_new ();
	paste_data = get_clipboard_data_from_selection_data (window, (char*) selection_data->data);
	paste_data->current_dir = g_strdup (current_dir);
	
	gtk_selection_data_free (selection_data);
	
	fr_window_paste_from_clipboard_data (window, paste_data);
}


void
fr_window_paste_selection (FrWindow *window)
{
	char *utf8_path, *utf8_old_path, *destination;
	char *current_dir;

	if (window->priv->list_mode == FR_WINDOW_LIST_MODE_FLAT)
		return;

	/**/

	utf8_old_path = g_filename_to_utf8 (fr_window_get_current_location (window), -1, NULL, NULL, NULL);
	utf8_path = _gtk_request_dialog_run (GTK_WINDOW (window),
					       (GTK_DIALOG_DESTROY_WITH_PARENT
						| GTK_DIALOG_MODAL),
					       _("Paste Selection"),
					       _("Destination folder"),
					       utf8_old_path,
					       1024,
					       GTK_STOCK_CANCEL,
					       GTK_STOCK_PASTE);
	g_free (utf8_old_path);
	if (utf8_path == NULL)
		return;

	destination = g_filename_from_utf8 (utf8_path, -1, NULL, NULL, NULL);
	g_free (utf8_path);

	if (destination[0] != '/')
		current_dir = g_build_path (G_DIR_SEPARATOR_S, fr_window_get_current_location (window), destination, NULL);
	else
		current_dir = g_strdup (destination);
	g_free (destination);

	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_PASTE,
					    g_strdup (current_dir),
					    (GFreeFunc) g_free);
	fr_window_paste_selection_to (window, current_dir);

	g_free (current_dir);
}


/* -- fr_window_open_files -- */


static void
fr_window_open_files__extract_done_cb (FrArchive   *archive,
				       FRAction     action,
				       FRProcError *error,
				       gpointer     callback_data)
{
	CommandData *cdata = callback_data;

	g_signal_handlers_disconnect_matched (G_OBJECT (archive),
					      G_SIGNAL_MATCH_DATA,
					      0,
					      0, NULL,
					      0,
					      cdata);

	if (error->type != FR_PROC_ERROR_NONE) {
		if (error->type != FR_PROC_ERROR_ASK_PASSWORD)
			command_done (cdata);
		return;
	}

	if (cdata->command != NULL) {
		FrProcess  *proc;
		GList      *scan;

		proc = fr_process_new ();
		fr_process_use_standard_locale (proc, FALSE);
		proc->term_on_stop = FALSE;
		cdata->process = proc;

		fr_process_begin_command (proc, cdata->command);
		for (scan = cdata->file_list; scan; scan = scan->next) {
			char *filename = shell_escape (scan->data);
			fr_process_add_arg (proc, filename);
			g_free (filename);
		}
		fr_process_end_command (proc);

		CommandList = g_list_prepend (CommandList, cdata);
		fr_process_start (proc);
	}
	else if (cdata->app != NULL) {
		GList *uris = NULL, *scan;
		GnomeVFSResult result;

		for (scan = cdata->file_list; scan; scan = scan->next) {
			char *filename = gnome_vfs_get_uri_from_local_path (scan->data);
			uris = g_list_prepend (uris, filename);
		}

		CommandList = g_list_prepend (CommandList, cdata);
		result = gnome_vfs_mime_application_launch (cdata->app, uris);
		if (result != GNOME_VFS_OK)
			_gtk_error_dialog_run (GTK_WINDOW (cdata->window),
					       _("Could not perform the operation"),
					       "%s",
					       gnome_vfs_result_to_string (result));

		path_list_free (uris);
	}
}


typedef struct {
	GList                   *file_list;
	char                    *command;
	GnomeVFSMimeApplication *app;
} ViewData;


static ViewData*
view_data_new (GList                   *file_list,
	       char                    *command,
	       GnomeVFSMimeApplication *app)

{
	ViewData *vdata;

	vdata = g_new0 (ViewData, 1);
	vdata->file_list = path_list_dup (file_list);
	if (command != NULL)
		vdata->command = g_strdup (command);
	if (app != NULL)
		vdata->app = gnome_vfs_mime_application_copy (app);

	return vdata;
}


static void
view_data_free (ViewData *vdata)
{
	g_return_if_fail (vdata != NULL);

	path_list_free (vdata->file_list);
	g_free (vdata->command);
	gnome_vfs_mime_application_free (vdata->app);

	g_free (vdata);
}


static void
fr_window_open_files_common (FrWindow                *window,
			     GList                   *file_list,
			     char                    *command,
			     GnomeVFSMimeApplication *app)
{
	CommandData *cdata;
	GList       *scan;
	ViewData    *vdata;

	g_return_if_fail (window != NULL);

	vdata = view_data_new (file_list, command, app);
	fr_window_set_current_batch_action (window,
					    FR_BATCH_ACTION_VIEW,
					    vdata,
					    (GFreeFunc) view_data_free);

	cdata = g_new0 (CommandData, 1);
	cdata->window = window;
	cdata->process = NULL;
	if (command != NULL)
		cdata->command = g_strdup (vdata->command);
	if (vdata->app != NULL)
		cdata->app = gnome_vfs_mime_application_copy (vdata->app);
	cdata->temp_dir = get_temp_work_dir ();

	cdata->file_list = NULL;
	for (scan = vdata->file_list; scan; scan = scan->next) {
		char *file = scan->data;
		char *filename;
		filename = g_strconcat (cdata->temp_dir,
					"/",
					file,
					NULL);
		cdata->file_list = g_list_prepend (cdata->file_list, filename);
	}

	g_signal_connect (G_OBJECT (window->archive),
			  "done",
			  G_CALLBACK (fr_window_open_files__extract_done_cb),
			  cdata);

	fr_process_clear (window->archive->process);
	fr_archive_extract (window->archive,
			    vdata->file_list,
			    cdata->temp_dir,
			    NULL,
			    FALSE,
			    TRUE,
			    FALSE,
			    window->priv->password);
	fr_process_start (window->archive->process);
}


void
fr_window_open_files (FrWindow *window,
		      GList    *file_list,
		      char     *command)
{
	fr_window_open_files_common (window, file_list, command, NULL);
}


void
fr_window_open_files_with_application (FrWindow                *window,
				       GList                   *file_list,
				       GnomeVFSMimeApplication *app)
{
	fr_window_open_files_common (window, file_list, NULL, app);
}


void
fr_window_view_or_open_file (FrWindow *window,
			     char     *filename)
{
	const char              *mime_type = NULL;
	GnomeVFSMimeApplication *application = NULL;
	GList                   *file_list = NULL;

	if (window->priv->activity_ref > 0)
		return;

	mime_type = get_mime_type (filename);
	if ((mime_type != NULL) && (strcmp (mime_type, GNOME_VFS_MIME_TYPE_UNKNOWN) != 0))
		application = gnome_vfs_mime_get_default_application (mime_type);
	file_list = g_list_append (NULL, filename);

	if (application != NULL)
		fr_window_open_files_with_application (window, file_list, application);
	else
		dlg_open_with (window, file_list);

	g_list_free (file_list);
	if (application != NULL)
		gnome_vfs_mime_application_free (application);
}


void
fr_window_set_open_default_dir (FrWindow   *window,
				const char *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	if (window->priv->open_default_dir != NULL)
		g_free (window->priv->open_default_dir);
	window->priv->open_default_dir = get_uri_from_path (default_dir);
}


const char *
fr_window_get_open_default_dir (FrWindow *window)
{
	if (window->priv->open_default_dir == NULL)
		return get_home_uri ();
	else
		return  window->priv->open_default_dir;
}


void
fr_window_set_add_default_dir (FrWindow   *window,
			       const char *default_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	if (window->priv->add_default_dir != NULL)
		g_free (window->priv->add_default_dir);
	window->priv->add_default_dir = get_uri_from_path (default_dir);
}


const char *
fr_window_get_add_default_dir (FrWindow *window)
{
	if (window->priv->add_default_dir == NULL)
		return get_home_uri ();
	else
		return  window->priv->add_default_dir;
}


void
fr_window_set_extract_default_dir (FrWindow   *window,
				   const char *default_dir,
				   gboolean    freeze)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	/* do not change this dir while it's used by the non-interactive
	 * extraction operation. */
	if (window->priv->extract_interact_use_default_dir)
		return;

	window->priv->extract_interact_use_default_dir = freeze;

	if (window->priv->extract_default_dir != NULL)
		g_free (window->priv->extract_default_dir);
	window->priv->extract_default_dir = get_uri_from_path (default_dir);
}


const char *
fr_window_get_extract_default_dir (FrWindow *window)
{
	if (window->priv->extract_default_dir == NULL)
		return get_home_uri ();
	else
		return  window->priv->extract_default_dir;
}


void
fr_window_set_default_dir (FrWindow   *window,
			   const char *default_dir,
			   gboolean    freeze)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (default_dir != NULL);

	window->priv->freeze_default_dir = freeze;

	fr_window_set_open_default_dir (window, default_dir);
	fr_window_set_add_default_dir (window, default_dir);
	fr_window_set_extract_default_dir (window, default_dir, FALSE);
}


void
fr_window_update_columns_visibility (FrWindow *window)
{
	GtkTreeView       *tree_view = GTK_TREE_VIEW (window->priv->list_view);
	GtkTreeViewColumn *column;

	column = gtk_tree_view_get_column (tree_view, 1);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_SIZE, TRUE));

	column = gtk_tree_view_get_column (tree_view, 2);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_TYPE, TRUE));

	column = gtk_tree_view_get_column (tree_view, 3);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_TIME, TRUE));

	column = gtk_tree_view_get_column (tree_view, 4);
	gtk_tree_view_column_set_visible (column, eel_gconf_get_boolean (PREF_LIST_SHOW_PATH, TRUE));
}


void
fr_window_set_toolbar_visibility (FrWindow *window,
				  gboolean  visible)
{
	g_return_if_fail (window != NULL);

	if (visible)
		gtk_widget_show (window->priv->toolbar->parent);
	else
		gtk_widget_hide (window->priv->toolbar->parent);

	set_active (window, "ViewToolbar", visible);
}


void
fr_window_set_statusbar_visibility  (FrWindow *window,
				     gboolean  visible)
{
	g_return_if_fail (window != NULL);

	if (visible)
		gtk_widget_show (window->priv->statusbar);
	else
		gtk_widget_hide (window->priv->statusbar);

	set_active (window, "ViewStatusbar", visible);
}


/* -- batch mode procedures -- */


static void fr_window_exec_current_batch_action (FrWindow *window);


static void
fr_window_exec_batch_action (FrWindow      *window,
			     FRBatchAction *action)
{
	ExtractData *edata;
	RenameData  *rdata;
	ViewData    *vdata;

	switch (action->type) {
	case FR_BATCH_ACTION_LOAD:
		debug (DEBUG_INFO, "[BATCH] LOAD\n");

		if (! path_is_file ((char*) action->data))
			fr_window_archive_new (window, (char*) action->data);
		else
			fr_window_archive_open (window, (char*) action->data, GTK_WINDOW (window));
		break;

	case FR_BATCH_ACTION_ADD:
		debug (DEBUG_INFO, "[BATCH] ADD\n");

		fr_window_archive_add_dropped_items (window, (GList*) action->data, FALSE);
		break;

	case FR_BATCH_ACTION_OPEN:
		debug (DEBUG_INFO, "[BATCH] OPEN\n");

		fr_window_push_message (window, _("Add files to an archive"));
		dlg_batch_add_files (window, (GList*) action->data);
		break;

	case FR_BATCH_ACTION_EXTRACT:
		debug (DEBUG_INFO, "[BATCH] EXTRACT\n");

		edata = action->data;
		fr_window_archive_extract (window,
					   edata->file_list,
					   edata->extract_to_dir,
					   edata->base_dir,
					   edata->skip_older,
					   edata->overwrite,
					   edata->junk_paths,
					   window->priv->password);
		break;

	case FR_BATCH_ACTION_EXTRACT_HERE:
		debug (DEBUG_INFO, "[BATCH] EXTRACT HERE\n");

		edata = action->data;
		fr_window_archive_extract_here (window,
						FALSE,
						TRUE,
						FALSE,
						window->priv->password);
		break;

	case FR_BATCH_ACTION_EXTRACT_INTERACT:
		debug (DEBUG_INFO, "[BATCH] EXTRACT_INTERACT\n");

		if (window->priv->extract_interact_use_default_dir
		    && (window->priv->extract_default_dir != NULL))
			fr_window_archive_extract (window,
						   NULL,
						   window->priv->extract_default_dir,
						   NULL,
						   FALSE,
						   TRUE,
						   FALSE,
						   window->priv->password);
		else {
			fr_window_push_message (window, _("Extract archive"));
			dlg_extract (NULL, window);
		}
		break;

	case FR_BATCH_ACTION_RENAME:
		debug (DEBUG_INFO, "[BATCH] RENAME\n");

		rdata = action->data;
		rename_selection (window,
				  rdata->file_list,
				  rdata->old_name,
				  rdata->new_name,
				  rdata->is_dir,
				  rdata->current_dir);
		break;

	case FR_BATCH_ACTION_PASTE:
		debug (DEBUG_INFO, "[BATCH] PASTE\n");

		fr_window_paste_selection_to (window, (char*) action->data);
		break;

	case FR_BATCH_ACTION_VIEW:
		debug (DEBUG_INFO, "[BATCH] VIEW\n");

		vdata = action->data;
		fr_window_open_files_common (window,
					     vdata->file_list,
					     vdata->command,
					     vdata->app);
		break;

	case FR_BATCH_ACTION_SAVE_AS:
		debug (DEBUG_INFO, "[BATCH] SAVE_AS\n");

		fr_window_archive_save_as (window, (char*) action->data);
		break;

	case FR_BATCH_ACTION_TEST:
		debug (DEBUG_INFO, "[BATCH] TEST\n");
		
		fr_window_archive_test (window);
		break;

	case FR_BATCH_ACTION_CLOSE:
		debug (DEBUG_INFO, "[BATCH] CLOSE\n");

		fr_window_archive_close (window);
		fr_window_exec_next_batch_action (window);
		break;

	case FR_BATCH_ACTION_QUIT:
		debug (DEBUG_INFO, "[BATCH] QUIT\n");

		gtk_widget_destroy (GTK_WIDGET (window));
		break;

	default:
		break;
	}
}


void
fr_window_reset_current_batch_action (FrWindow *window)
{
	FRBatchAction *adata = &window->priv->current_batch_action;

	if ((adata->data != NULL) && (adata->free_func != NULL))
		(*adata->free_func) (adata->data);
	adata->type = FR_BATCH_ACTION_NONE;
	adata->data = NULL;
	adata->free_func = NULL;
}


void
fr_window_set_current_batch_action (FrWindow          *window,
				    FRBatchActionType  action,
				    void              *data,
				    GFreeFunc          free_func)
{
	FRBatchAction *adata = &window->priv->current_batch_action;

	fr_window_reset_current_batch_action (window);

	adata->type = action;
	adata->data = data;
	adata->free_func = free_func;
}


void
fr_window_restart_current_batch_action (FrWindow *window)
{
	fr_window_exec_batch_action (window, &window->priv->current_batch_action);
}


void
fr_window_append_batch_action (FrWindow          *window,
			       FRBatchActionType  action,
			       void              *data,
			       GFreeFunc          free_func)
{
	FRBatchAction *a_desc;

	g_return_if_fail (window != NULL);

	a_desc = g_new0 (FRBatchAction, 1);
	a_desc->type = action;
	a_desc->data = data;
	a_desc->free_func = free_func;

	window->priv->batch_action_list = g_list_append (window->priv->batch_action_list, a_desc);
}


static void
fr_window_exec_current_batch_action (FrWindow *window)
{
	FRBatchAction *action;

	if (window->priv->batch_action == NULL) {
		window->priv->batch_mode = FALSE;
		return;
	}
	action = (FRBatchAction *) window->priv->batch_action->data;
	fr_window_exec_batch_action (window, action);
}


static void
fr_window_exec_next_batch_action (FrWindow *window)
{
	if (window->priv->batch_action != NULL)
		window->priv->batch_action = g_list_next (window->priv->batch_action);
	else
		window->priv->batch_action = window->priv->batch_action_list;
	fr_window_exec_current_batch_action (window);
}


void
fr_window_start_batch (FrWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->priv->batch_mode)
		return;

	if (window->priv->batch_action_list == NULL)
		return;

	window->priv->batch_mode = TRUE;
	window->priv->batch_action = window->priv->batch_action_list;
	window->archive->can_create_compressed_file = window->priv->batch_adding_one_file;

	fr_window_exec_current_batch_action (window);
}


void
fr_window_stop_batch (FrWindow *window)
{
	if (! window->priv->batch_mode)
		return;

	window->priv->extract_interact_use_default_dir = FALSE;
	window->priv->batch_mode = FALSE;
	window->archive->can_create_compressed_file = FALSE;

	if (window->priv->non_interactive)
		gtk_widget_destroy (GTK_WIDGET (window));
	else {
		gtk_window_present (GTK_WINDOW (window));
		fr_window_archive_close (window);
	}
}


void
fr_window_resume_batch (FrWindow *window)
{
	fr_window_exec_current_batch_action (window);
}


gboolean
fr_window_is_batch_mode (FrWindow *window)
{
	return window->priv->batch_mode;
}


void
fr_window_new_batch (FrWindow *window)
{
	fr_window_free_batch_data (window);
	window->priv->non_interactive = TRUE;
}


void
fr_window_set_batch__extract_here (FrWindow   *window,
				   const char *filename,
				   const char *dest_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (filename != NULL);
	g_return_if_fail (dest_dir != NULL);

	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_LOAD,
				       g_strdup (filename),
				       (GFreeFunc) g_free);
	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_EXTRACT_HERE,
				       extract_to_data_new (dest_dir),
				       (GFreeFunc) extract_data_free);
	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);			       
}


void
fr_window_set_batch__extract (FrWindow   *window,
			      const char *filename,
			      const char *dest_dir)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (filename != NULL);

	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_LOAD,
				       g_strdup (filename),
				       (GFreeFunc) g_free);
	if (dest_dir != NULL)
		fr_window_append_batch_action (window,
					       FR_BATCH_ACTION_EXTRACT,
					       extract_to_data_new (dest_dir),
					       (GFreeFunc) extract_data_free);
	else
		fr_window_append_batch_action (window,
					       FR_BATCH_ACTION_EXTRACT_INTERACT,
					       NULL,
					       NULL);
	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);
}


void
fr_window_set_batch__add (FrWindow   *window,
			  const char *archive,
			  GList      *file_list)
{
	window->priv->batch_adding_one_file = (file_list->next == NULL) && (path_is_file (file_list->data));

	if (archive != NULL)
		fr_window_append_batch_action (window,
					       FR_BATCH_ACTION_LOAD,
					       g_strdup (archive),
					       (GFreeFunc) g_free);
	else
		fr_window_append_batch_action (window,
					       FR_BATCH_ACTION_OPEN,
					       file_list,
					       NULL);
	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_ADD,
				       file_list,
				       NULL);
	fr_window_append_batch_action (window,
				       FR_BATCH_ACTION_CLOSE,
				       NULL,
				       NULL);
}
