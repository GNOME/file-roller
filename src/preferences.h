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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <gtk/gtkenums.h>
#include "typedefs.h"
#include "window.h"

#define PREF_LIST_SORT_METHOD      "/apps/file-roller/listing/sort_method"
#define PREF_LIST_SORT_TYPE        "/apps/file-roller/listing/sort_type"
#define PREF_LIST_MODE             "/apps/file-roller/listing/list_mode"
#define PREF_LIST_SHOW_TYPE        "/apps/file-roller/listing/show_type"
#define PREF_LIST_SHOW_SIZE        "/apps/file-roller/listing/show_size"
#define PREF_LIST_SHOW_TIME        "/apps/file-roller/listing/show_time"
#define PREF_LIST_SHOW_PATH        "/apps/file-roller/listing/show_path"
#define PREF_LIST_USE_MIME_ICONS   "/apps/file-roller/listing/use_mime_icons"

#define PREF_UI_HISTORY_LEN        "/apps/file-roller/ui/history_len"
#define PREF_UI_TOOLBAR            "/apps/file-roller/ui/view_toolbar"
#define PREF_UI_STATUSBAR          "/apps/file-roller/ui/view_statusbar"

#define PREF_EDIT_EDITORS          "/apps/file-roller/general/editors"
#define PREF_ADD_COMPRESSION_LEVEL "/apps/file-roller/general/compression_level"
#define PREF_MIGRATE_DIRECTORIES   "/apps/file-roller/general/migrate_directories"

#define PREF_EXTRACT_VIEW_FOLDER      "/apps/file-roller/dialogs/extract/view_destination_folder"
#define PREF_EXTRACT_OVERWRITE        "/apps/file-roller/dialogs/extract/overwrite"
#define PREF_EXTRACT_SKIP_NEWER       "/apps/file-roller/dialogs/extract/skip_newer"
#define PREF_EXTRACT_RECREATE_FOLDERS "/apps/file-roller/dialogs/extract/recreate_folders"

#define PREF_DESKTOP_ICON_THEME         "/desktop/gnome/file_views/icon_theme"
#define PREF_DESKTOP_MENUS_HAVE_TEAROFF "/desktop/gnome/interface/menus_have_tearoff"
#define PREF_DESKTOP_TOOLBAR_DETACHABLE "/desktop/gnome/interface/toolbar_detachable"


WindowSortMethod    preferences_get_sort_method ();

void                preferences_set_sort_method (WindowSortMethod i_value);

GtkSortType         preferences_get_sort_type ();

void                preferences_set_sort_type (GtkSortType i_value);

WindowListMode      preferences_get_list_mode ();

void                preferences_set_list_mode (WindowListMode i_value);

FRCompression       preferences_get_compression_level ();

void                preferences_set_compression_level (FRCompression i_value);


#endif /* PREFERENCES_H */
