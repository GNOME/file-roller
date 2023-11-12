/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#ifndef FR_WINDOW_ACTION_CALLBACKS_H
#define FR_WINDOW_ACTION_CALLBACKS_H

#include <gtk/gtk.h>
#include "glib-utils.h"

GtkWidget * _gtk_application_get_current_window (GApplication *application);

DEF_ACTION_CALLBACK (fr_toggle_action_activated)
DEF_ACTION_CALLBACK (fr_window_activate_add_files)
DEF_ACTION_CALLBACK (fr_window_activate_close)
DEF_ACTION_CALLBACK (fr_window_activate_delete)
DEF_ACTION_CALLBACK (fr_window_activate_deselect_all)
DEF_ACTION_CALLBACK (fr_window_activate_edit_copy)
DEF_ACTION_CALLBACK (fr_window_activate_edit_cut)
DEF_ACTION_CALLBACK (fr_window_activate_edit_password)
DEF_ACTION_CALLBACK (fr_window_activate_edit_paste)
DEF_ACTION_CALLBACK (fr_window_activate_extract_files)
DEF_ACTION_CALLBACK (fr_window_activate_extract_all_by_default)
DEF_ACTION_CALLBACK (fr_window_activate_find)
DEF_ACTION_CALLBACK (fr_window_activate_go_back)
DEF_ACTION_CALLBACK (fr_window_activate_go_forward)
DEF_ACTION_CALLBACK (fr_window_activate_go_home)
DEF_ACTION_CALLBACK (fr_window_activate_go_up_one_level)
DEF_ACTION_CALLBACK (fr_window_activate_navigate_to)
DEF_ACTION_CALLBACK (fr_window_activate_new)
DEF_ACTION_CALLBACK (fr_window_activate_open)
DEF_ACTION_CALLBACK (fr_window_activate_open_folder)
DEF_ACTION_CALLBACK (fr_window_activate_open_with)
DEF_ACTION_CALLBACK (fr_window_activate_reload)
DEF_ACTION_CALLBACK (fr_window_activate_rename)
DEF_ACTION_CALLBACK (fr_window_activate_save_as)
DEF_ACTION_CALLBACK (fr_window_activate_select_all)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_delete)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_edit_copy)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_edit_cut)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_edit_paste)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_extract_files)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_open_folder)
DEF_ACTION_CALLBACK (fr_window_activate_sidebar_rename)
DEF_ACTION_CALLBACK (fr_window_activate_stop)
DEF_ACTION_CALLBACK (fr_window_activate_test_archive)
DEF_ACTION_CALLBACK (fr_window_activate_view_properties)
DEF_ACTION_CALLBACK (fr_window_activate_view_selection)
DEF_ACTION_CALLBACK (fr_window_activate_view_sidebar)
DEF_ACTION_CALLBACK (fr_window_activate_focus_location)

#endif /* FR_WINDOW_ACTION_CALLBACKS_H */
