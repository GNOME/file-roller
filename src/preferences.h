/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2010 Free Software Foundation, Inc.
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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <gtk/gtk.h>
#include "typedefs.h"
#include "fr-window.h"

#define FILE_ROLLER_SCHEMA                "org.gnome.FileRoller"
#define FILE_ROLLER_SCHEMA_LISTING        FILE_ROLLER_SCHEMA ".Listing"
#define FILE_ROLLER_SCHEMA_UI             FILE_ROLLER_SCHEMA ".UI"
#define FILE_ROLLER_SCHEMA_GENERAL        FILE_ROLLER_SCHEMA ".General"
#define FILE_ROLLER_SCHEMA_DIALOGS        FILE_ROLLER_SCHEMA ".Dialogs"
#define FILE_ROLLER_SCHEMA_NEW            FILE_ROLLER_SCHEMA_DIALOGS ".New"
#define FILE_ROLLER_SCHEMA_ADD            FILE_ROLLER_SCHEMA_DIALOGS ".Add"
#define FILE_ROLLER_SCHEMA_EXTRACT        FILE_ROLLER_SCHEMA_DIALOGS ".Extract"
#define FILE_ROLLER_SCHEMA_LAST_OUTPUT    FILE_ROLLER_SCHEMA_DIALOGS ".LastOutput"

#define PREF_LISTING_SORT_METHOD          "sort-method"
#define PREF_LISTING_SORT_TYPE            "sort-type"
#define PREF_LISTING_LIST_MODE            "list-mode"
#define PREF_LISTING_SHOW_TYPE            "show-type"
#define PREF_LISTING_SHOW_SIZE            "show-size"
#define PREF_LISTING_SHOW_TIME            "show-time"
#define PREF_LISTING_SHOW_PATH            "show-path"
#define PREF_LISTING_NAME_COLUMN_WIDTH    "name-column-width"

#define PREF_UI_WINDOW_WIDTH              "window-width"
#define PREF_UI_WINDOW_HEIGHT             "window-height"
#define PREF_UI_SIDEBAR_WIDTH             "sidebar-width"
#define PREF_UI_VIEW_SIDEBAR              "view-sidebar"

#define PREF_GENERAL_EDITORS              "editors"
#define PREF_GENERAL_COMPRESSION_LEVEL    "compression-level"
#define PREF_GENERAL_ENCRYPT_HEADER       "encrypt-header"

#define PREF_EXTRACT_SKIP_NEWER           "skip-newer"
#define PREF_EXTRACT_RECREATE_FOLDERS     "recreate-folders"

#define PREF_ADD_CURRENT_FOLDER           "current-folder"
#define PREF_ADD_SELECTED_FILES           "selected-files"
#define PREF_ADD_INCLUDE_FILES            "include-files"
#define PREF_ADD_EXCLUDE_FILES            "exclude-files"
#define PREF_ADD_EXCLUDE_FOLDERS          "exclude-folders"
#define PREF_ADD_UPDATE                   "update"
#define PREF_ADD_RECURSIVE                "recursive"
#define PREF_ADD_NO_FOLLOW_SYMLINKS       "no-symlinks"

#define PREF_NEW_DEFAULT_EXTENSION        "default-extension"
#define PREF_NEW_ENCRYPT_HEADER           "encrypt-header"
#define PREF_NEW_VOLUME_SIZE              "volume-size"
#define PREF_NEW_EXPAND_OPTIONS		  "expand-options"

#define NAUTILUS_SCHEMA                   "org.gnome.nautilus.preferences"
#define NAUTILUS_CLICK_POLICY             "click-policy"

void  pref_util_save_window_geometry    (GtkWindow  *window,
					 const char *dialog_id);
void  pref_util_restore_window_geometry (GtkWindow  *window,
					 const char *dialog_id);

#endif /* PREFERENCES_H */
