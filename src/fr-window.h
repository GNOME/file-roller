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

#ifndef FR_WINDOW_H
#define FR_WINDOW_H

#include <gtk/gtk.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "typedefs.h"
#include "file-list.h"
#include "fr-archive.h"

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
	FR_BATCH_ACTION_NONE,
	FR_BATCH_ACTION_OPEN,
	FR_BATCH_ACTION_ADD,
	FR_BATCH_ACTION_OPEN_AND_ADD,
	FR_BATCH_ACTION_ADD_INTERACT,
	FR_BATCH_ACTION_EXTRACT,
	FR_BATCH_ACTION_EXTRACT_HERE,
	FR_BATCH_ACTION_EXTRACT_INTERACT,
	FR_BATCH_ACTION_RENAME,
	FR_BATCH_ACTION_PASTE,
	FR_BATCH_ACTION_VIEW,
	FR_BATCH_ACTION_SAVE_AS,
	FR_BATCH_ACTION_CLOSE,
	FR_BATCH_ACTION_QUIT,
	FR_BATCH_ACTIONS
} FRBatchAction;

typedef struct {
	FRBatchAction   action;
	void *          data;
	GFreeFunc       free_func;
} FRBatchActionDescription;

typedef struct {
	guint      converting : 1;
	char      *temp_dir;
	FrArchive *new_archive;
} FRConvertData;

typedef enum {
	FR_CLIPBOARD_OP_CUT,
	FR_CLIPBOARD_OP_COPY
} FRClipboardOp;

/* -- FrWindow -- */

#define FR_TYPE_WINDOW              (fr_window_get_type ())
#define FR_WINDOW(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_WINDOW, FrWindow))
#define FR_WINDOW_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), FR_WINDOW_TYPE, FrWindowClass))
#define FR_IS_WINDOW(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_WINDOW))
#define FR_IS_WINDOW_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_WINDOW))
#define FR_WINDOW_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_WINDOW, FrWindowClass))

typedef struct _FrWindow            FrWindow;
typedef struct _FrWindowClass       FrWindowClass;
typedef struct _FrWindowPrivateData FrWindowPrivateData;

struct _FrWindow
{
	GnomeApp __parent;

	FrArchive           *archive;
	FrWindowPrivateData *priv;
};

struct _FrWindowClass
{
	GnomeAppClass __parent_class;

	/*<signals>*/

	void (*archive_opened) (FrWindow *window);
};

GType       fr_window_get_type                  (void);
GtkWidget * fr_window_new                       (void);

/* archive operations */

gboolean    fr_window_archive_new               (FrWindow      *window,
						 const char    *uri);
gboolean    fr_window_archive_open              (FrWindow      *window,
						 const char    *uri,
						 GtkWindow     *parent);
void        fr_window_archive_close             (FrWindow      *window);
const char *fr_window_get_archive_uri           (FrWindow      *window);
gboolean    fr_window_archive_is_present        (FrWindow      *window);
void        fr_window_archive_save_as           (FrWindow      *window,
						 const char    *filename);
void        fr_window_archive_reload            (FrWindow      *window);
void        fr_window_archive_rename            (FrWindow      *window,
						 const char    *filename);
void        fr_window_archive_add_files         (FrWindow      *window,
						 GList         *file_list,
						 gboolean       update);
void        fr_window_archive_add_with_wildcard (FrWindow      *window,
						 const char    *include_files,
						 const char    *exclude_files,
						 const char    *base_dir,
						 const char    *dest_dir,
						 gboolean       update,
						 gboolean       recursive,
						 gboolean       follow_links);
void        fr_window_archive_add_directory     (FrWindow      *window,
						 const char    *directory,
						 const char    *base_dir,
						 const char    *dest_dir,
						 gboolean       update,
						 const char    *password,
						 FRCompression  compression);
void        fr_window_archive_add_items         (FrWindow      *window,
						 GList         *dir_list,
						 const char    *base_dir,
						 const char    *dest_dir,
						 gboolean       update,
						 const char    *password,
						 FRCompression  compression);
void        fr_window_archive_add_dropped_items (FrWindow      *window,
						 GList         *item_list,
						 gboolean       update);
void        fr_window_archive_remove            (FrWindow      *window,
						 GList         *file_list,
						 FRCompression  compression);
void        fr_window_archive_extract           (FrWindow      *window,
						 GList         *file_list,
						 const char    *extract_to_dir,
						 const char    *base_dir,
						 gboolean       skip_older,
						 gboolean       overwrite,
						 gboolean       junk_paths,
						 const char    *password);
void        fr_window_archive_extract_here      (FrWindow      *window,
						 GList         *file_list,
						 const char    *extract_to_dir,
						 const char    *base_dir,
						 gboolean       skip_older,
						 gboolean       overwrite,
						 gboolean       junk_paths,
						 const char    *password);
void        fr_window_set_password              (FrWindow      *window,
						 const char    *password);
const char *fr_window_get_password              (FrWindow      *window);
FRCompression
	    fr_window_get_compression           (FrWindow      *window);
/* FIXME */void       fr_window_view_folder_after_extract  (FrWindow       *window,
					 	 const char     *folder);

/**/

void       fr_window_go_to_location             (FrWindow       *window,
					 	 const char     *path);
const char*fr_window_get_current_location       (FrWindow       *window);
void       fr_window_go_up_one_level            (FrWindow       *window);
void       fr_window_go_back                    (FrWindow       *window);
void       fr_window_go_forward                 (FrWindow       *window);
void       fr_window_current_folder_activated   (FrWindow       *window);
void       fr_window_set_list_mode              (FrWindow       *window,
						 FRWindowListMode  list_mode);

/**/

void       fr_window_update_file_list             (FrWindow    *window);
void       fr_window_update_list_order            (FrWindow    *window);
GList *    fr_window_get_file_list_selection      (FrWindow    *window,
						   gboolean     recursive,
						   gboolean    *has_dirs);
GList *    fr_window_get_file_list_from_path_list (FrWindow    *window,
						   GList       *path_list,
						   gboolean    *has_dirs);
GList *    fr_window_get_file_list_pattern        (FrWindow    *window,
						   const char  *pattern);
int        fr_window_get_n_selected_files         (FrWindow    *window);
GtkListStore *
	   fr_window_get_list_store               (FrWindow    *window);
void       fr_window_select_all                   (FrWindow    *window);
void       fr_window_unselect_all                 (FrWindow    *window);
void       fr_window_set_sort_type                (FrWindow    *window,
						   GtkSortType  sort_type);

/**/

void       fr_window_rename_selection             (FrWindow    *window);
void       fr_window_cut_selection                (FrWindow    *window);
void       fr_window_copy_selection               (FrWindow    *window);
void       fr_window_paste_selection              (FrWindow    *window);

/**/

void       fr_window_stop                      (FrWindow    *window);
void       fr_window_start_activity_mode       (FrWindow    *window);
void       fr_window_stop_activity_mode        (FrWindow    *window);

/**/

void        fr_window_view_last_output            (FrWindow   *window,
						   const char *title);
void        fr_window_view_file                   (FrWindow   *window,
						   char       *file);
void        fr_window_open_files                  (FrWindow   *window,
						   GList      *file_list,
						   char       *command);
void        fr_window_open_files_with_application (FrWindow   *window,
						   GList      *file_list,
						   GnomeVFSMimeApplication *app);
void        fr_window_view_or_open_file           (FrWindow   *window,
						   char       *file);
void        fr_window_update_columns_visibility   (FrWindow   *window);
void        fr_window_update_history_list         (FrWindow   *window);
void        fr_window_set_default_dir             (FrWindow   *window,
						   const char *default_dir,
						   gboolean    freeze);
void        fr_window_set_open_default_dir        (FrWindow   *window,
						   const char *default_dir);
const char *fr_window_get_open_default_dir        (FrWindow   *window);
void        fr_window_set_add_default_dir         (FrWindow   *window,
						   const char *default_dir);
const char *fr_window_get_add_default_dir         (FrWindow   *window);
void        fr_window_set_extract_default_dir     (FrWindow   *window,
						   const char *default_dir,
						   gboolean    freeze);
const char *fr_window_get_extract_default_dir     (FrWindow   *window);
void        fr_window_push_message                (FrWindow   *window,
						   const char *msg);
void        fr_window_pop_message                 (FrWindow   *window);
void        fr_window_set_toolbar_visibility      (FrWindow   *window,
						   gboolean    value);
void        fr_window_set_statusbar_visibility    (FrWindow   *window,
						   gboolean    value);

/**/

void       fr_window_current_action_description_set   (FrWindow      *window,
						       FRBatchAction  action,
						       void          *data,
						       GFreeFunc      free_func);
void       fr_window_current_action_description_reset (FrWindow      *window);
void       fr_window_restart_current_action           (FrWindow      *window);

/* batch mode procedures. */

void       fr_window_batch_mode_clear            (FrWindow      *window);
void       fr_window_batch_mode_add_action       (FrWindow      *window,
						  FRBatchAction  action,
						  void          *data,
						  GFreeFunc      free_func);
void       fr_window_batch_mode_add_next_action  (FrWindow      *window,
						  FRBatchAction  action,
						  void          *data,
						  GFreeFunc      free_func);
void       fr_window_batch_mode_start            (FrWindow      *window);
void       fr_window_batch_mode_stop             (FrWindow      *window);
void       fr_window_batch_mode_resume           (FrWindow      *window);
gboolean   fr_window_is_batch_mode               (FrWindow      *window);
void       fr_window_archive__open_extract       (FrWindow      *window,
						  const char    *filename,
						  const char    *dest_dir);
void       fr_window_archive__open_extract_here  (FrWindow      *window,
						  const char    *filename,
						  const char    *dest_dir);
void       fr_window_archive__open_add           (FrWindow      *window,
						  const char    *archive,
						  GList         *file_list);
void       fr_window_archive__quit               (FrWindow      *window);


/**/

void       fr_window_convert_data_free       (FrWindow         *window);
gboolean   fr_window_file_list_drag_data_get (FrWindow         *window,
					      GdkDragContext   *context,
					      GtkSelectionData *selection_data,
					      GList            *path_list);

#endif /* FR_WINDOW_H */
