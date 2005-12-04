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

#ifndef FR_WINDOW_H
#define FR_WINDOW_H

#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "egg-recent.h"
#include "fr-archive.h"
#include "typedefs.h"
#include "file-list.h"

#define GCONF_NOTIFICATIONS 9

enum {
	COLUMN_FILE_DATA,
	COLUMN_ICON,
	COLUMN_NAME,
	COLUMN_SIZE,
	COLUMN_TYPE,
	COLUMN_TIME,
	COLUMN_PATH,
	NUMBER_OF_COLUMNS
};

typedef enum {
	FR_BATCH_ACTION_OPEN,
	FR_BATCH_ACTION_ADD,
	FR_BATCH_ACTION_OPEN_AND_ADD,
	FR_BATCH_ACTION_ADD_INTERACT,
	FR_BATCH_ACTION_EXTRACT,
	FR_BATCH_ACTION_EXTRACT_INTERACT,
	FR_BATCH_ACTION_CLOSE,
	FR_BATCH_ACTION_QUIT
} FRBatchAction;

typedef struct {
	FRBatchAction   action;
	void *          data;
	GFreeFunc       free_func;
} FRBatchActionDescription;

typedef struct {
	guint      converting : 1;
	char      *temp_dir;
	FRArchive *new_archive;
} FRConvertData;

typedef enum {
	FR_CLIPBOARD_OP_CUT,
	FR_CLIPBOARD_OP_COPY
} FRClipboardOp;

typedef struct {
        GtkWidget *      app;
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

	FRArchive *      archive;
	FRAction         current_action;
	gboolean         archive_present;
	gboolean         archive_new;        /* A new archive has been created
					      * but it doesn't contain any 
					      * file yet.  The real file will
					      * be created only when the user
					      * adds some file to the 
					      * archive.*/

	char *           archive_filename;
	char *           open_default_dir;    /* default directory to be used
					       * in the Open dialog. */
	char *           add_default_dir;     /* default directory to be used
					       * in the Add dialog. */
	char *           extract_default_dir; /* default directory to be used
					       * in the Extract dialog. */
	gboolean         freeze_default_dir;

	gboolean         view_folder_after_extraction;
	char *           folder_to_view;

	gboolean         give_focus_to_the_list;
	gboolean         single_click;
	GtkTreePath     *path_clicked;

	WindowSortMethod sort_method;
	GtkSortType      sort_type;

	WindowListMode   list_mode;
	GList *          history;
	GList *          history_current;
	char *           password;
	FRCompression    compression;

	guint            activity_timeout_handle;   /* activity timeout 
						     * handle. */
	gint             activity_ref;              /* when > 0 some activity
                                                     * is present. */

	guint            update_timeout_handle;     /* update file list 
						     * timeout handle. */

	VisitDirHandle  *vd_handle;

	FRConvertData    convert_data;

	gboolean         stoppable;

	GList           *clipboard;
	guint            clipboard_op : 1;
	char            *clipboard_current_dir;

	GtkActionGroup  *actions;

	EggRecentViewGtk *recent_view_menu;
	EggRecentViewGtk *recent_view_toolbar;
	EggRecentModel   *recent_model;
	GtkWidget        *file_popup_menu;
	GtkWidget        *mitem_recents_menu;
	GtkWidget        *recent_toolbar_menu;
	GtkAction        *open_action;

	/* drag data */

	GList *  drag_file_list;        /* the list of files we are 
					 * dragging*/
	GList *  drag_file_list_names;  /* the list of files (only the name 
					 * without the path) of the
					 * files we are dragging.  Used 
					 * when dragging directories. */
	char *   drag_temp_dir;         /* the temporary directory used to
					 * extract the dragged files. */
	GList *  drag_temp_dirs;        /* the list of temporary directories
					 * used to extract dragged files.
					 * These directories will be deleted
					 * when the window is closed. */
	gboolean dragging_dirs;         /* whether we are dragging directories
					 * too .*/

	GList *  dropped_file_list;     /* the list of dropped files. */
	gboolean add_after_creation;    /* whether we must add dropped files
					 * after creating an archive. */
	gboolean add_after_opening;     /* whether we must add dropped files
					 * after opening an archive. Used in 
					 * batch mode to avoid unnecessary 
					 * archive loading. */

	gboolean adding_dropped_files;  /* whether we are adding dropped 
					 * files. */
	gboolean update_dropped_files;  /* the update flag of the add 
					 * operation.  */

	gboolean extracting_dragged_files;
	gboolean extracting_dragged_files_interrupted;
	gboolean batch_adding_one_file;

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
	GList     *batch_action_list;   /* FRActionDescription * elements */
	GList     *batch_action;        /* current action. */
	gboolean   extract_interact_use_default_dir;

	guint      cnxn_id[GCONF_NOTIFICATIONS];

	gulong     theme_changed_handler_id;
	gboolean   non_interactive;
} FRWindow;


FRWindow * window_new                       (void);

void       window_close                     (FRWindow      *window);

/* archive operations */

gboolean   window_archive_new               (FRWindow      *window, 
					     const char    *filename);

gboolean   window_archive_open              (FRWindow      *window, 
					     const char    *filename,
					     GtkWindow     *parent);

void       window_archive_save_as           (FRWindow      *window, 
					     const char    *filename);

void       window_archive_reload            (FRWindow      *window);

void       window_archive_rename            (FRWindow      *window, 
					     const char    *filename);

void       window_archive_add               (FRWindow      *window,
					     GList         *file_list,
					     const char    *base_dir,
					     const char    *dest_dir,
					     gboolean       update,
					     const char    *password,
					     FRCompression  compression);

void       window_archive_add_with_wildcard (FRWindow      *window,
					     const char    *include_files,
					     const char    *exclude_files,
					     const char    *base_dir,
					     const char    *dest_dir,
					     gboolean       update,
					     gboolean       recursive,
					     gboolean       follow_links,
					     const char    *password,
					     FRCompression  compression);

void       window_archive_add_directory     (FRWindow      *window,
					     const char    *directory,
					     const char    *base_dir,
					     const char    *dest_dir,
					     gboolean       update,
					     const char    *password,
					     FRCompression  compression);

void       window_archive_add_items         (FRWindow      *window,
					     GList         *dir_list,
					     const char    *base_dir,
					     const char    *dest_dir,
					     gboolean       update,
					     const char    *password,
					     FRCompression  compression);

void       window_archive_add_dropped_items (FRWindow      *window,
					     GList         *item_list,
					     gboolean       update);

void       window_archive_remove            (FRWindow      *window,
					     GList         *file_list,
					     FRCompression  compression);

void       window_archive_extract           (FRWindow      *window,
					     GList         *file_list,
					     const char    *extract_to_dir,
					     const char    *base_dir,
					     gboolean       skip_older,
					     gboolean       overwrite,
					     gboolean       junk_paths,
					     const char    *password);

void       window_archive_close             (FRWindow      *window);

void       window_set_password              (FRWindow      *window,
					     const char    *password);

/**/

void       window_go_to_location            (FRWindow       *window, 
					     const char     *path);

const char*window_get_current_location      (FRWindow       *window);

void       window_go_up_one_level           (FRWindow       *window);

void       window_go_back                   (FRWindow       *window);

void       window_go_forward                (FRWindow       *window);

void       window_set_list_mode             (FRWindow       *window, 
					     WindowListMode  list_mode);

/**/

void       window_update_file_list          (FRWindow    *window);

void       window_update_list_order         (FRWindow    *window);

GList *    window_get_file_list_selection   (FRWindow    *window, 
					     gboolean     recursive,
					     gboolean    *has_dirs);

GList *    window_get_file_list_pattern     (FRWindow    *window,
					     const char  *pattern);

void       window_rename_selection          (FRWindow    *window);

void       window_cut_selection             (FRWindow    *window);

void       window_copy_selection            (FRWindow    *window);

void       window_paste_selection           (FRWindow    *window);

/**/

void       window_stop                      (FRWindow    *window);

void       window_start_activity_mode       (FRWindow    *window);

void       window_stop_activity_mode        (FRWindow    *window);

/**/

void       window_view_last_output          (FRWindow *window,
					     const char *title); 

void       window_view_file                 (FRWindow *window, 
					     char     *file);

void       window_open_files                (FRWindow *window, 
					     GList    *file_list,
					     char     *command);

void       window_open_files_with_application (FRWindow *window, 
					       GList    *file_list,
					       GnomeVFSMimeApplication *app);

void       window_view_or_open_file         (FRWindow *window, 
					     char     *file);

void       window_update_columns_visibility (FRWindow *window);

void       window_update_history_list       (FRWindow *window);

void       window_set_default_dir           (FRWindow *window,
					     char     *default_dir,
					     gboolean  freeze);

void       window_set_open_default_dir      (FRWindow *window,
					     char     *default_dir);

void       window_set_add_default_dir       (FRWindow *window,
					     char     *default_dir);

void       window_set_extract_default_dir   (FRWindow *window,
					     char     *default_dir);

void       window_push_message              (FRWindow   *window, 
					     const char *msg);

void       window_pop_message               (FRWindow   *window);

void       window_set_toolbar_visibility    (FRWindow   *window,
					     gboolean    value);

void       window_set_statusbar_visibility  (FRWindow   *window,
					     gboolean    value);

/* batch mode procedures. */

void       window_batch_mode_clear            (FRWindow      *window);

void       window_batch_mode_add_action       (FRWindow      *window,
					       FRBatchAction  action,
					       void          *data,
					       GFreeFunc      free_func);

void       window_batch_mode_add_next_action  (FRWindow      *window,
					       FRBatchAction  action,
					       void          *data,
					       GFreeFunc      free_func);

void       window_batch_mode_start            (FRWindow      *window);

void       window_batch_mode_stop             (FRWindow      *window);

void       window_archive__open_extract       (FRWindow      *window, 
					       const char    *filename,
					       const char    *dest_dir);

void       window_archive__open_add           (FRWindow      *window, 
					       const char    *archive,
					       GList         *file_list);

void       window_archive__close              (FRWindow      *window);

void       window_archive__quit               (FRWindow      *window);


/**/

void       window_convert_data_free           (FRWindow *window);

void       fr_window_file_list_drag_data_get  (FRWindow         *window,
					       GList            *path_list,
					       GtkSelectionData *selection_data);

#endif /* FR_WINDOW_H */
