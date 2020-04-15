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

#ifndef FR_WINDOW_ACTION_ENTRIES_H
#define FR_WINDOW_ACTION_ENTRIES_H

#include <config.h>
#include <glib/gi18n.h>
#include "gtk-utils.h"
#include "fr-window-actions-callbacks.h"

static const GActionEntry fr_window_actions[] = {
	{ "add-files", fr_window_activate_add_files },
	{ "close", fr_window_activate_close },
	{ "delete", fr_window_activate_delete },
	{ "deselect-all", fr_window_activate_deselect_all },
	{ "edit-copy", fr_window_activate_edit_copy },
	{ "edit-cut", fr_window_activate_edit_cut },
	{ "edit-password", fr_window_activate_edit_password },
	{ "edit-paste", fr_window_activate_edit_paste },
	{ "extract-files", fr_window_activate_extract_files },
	{ "find", toggle_action_activated, NULL, "false", fr_window_activate_find },
	{ "go-back", fr_window_activate_go_back },
	{ "go-forward", fr_window_activate_go_forward },
	{ "go-home", fr_window_activate_go_home },
	{ "open-folder", fr_window_activate_open_folder },
	{ "open-with", fr_window_activate_open_with },
	{ "reload", fr_window_activate_reload },
	{ "rename", fr_window_activate_rename },
	{ "save-as", fr_window_activate_save_as },
	{ "select-all", fr_window_activate_select_all },
	{ "sidebar-delete", fr_window_activate_sidebar_delete },
	{ "sidebar-edit-copy", fr_window_activate_sidebar_edit_copy },
	{ "sidebar-edit-cut", fr_window_activate_sidebar_edit_cut },
	{ "sidebar-edit-paste", fr_window_activate_sidebar_edit_paste },
	{ "sidebar-extract-files", fr_window_activate_sidebar_extract_files },
	{ "sidebar-open-folder", fr_window_activate_sidebar_open_folder },
	{ "sidebar-rename", fr_window_activate_sidebar_rename },
	{ "stop", fr_window_activate_stop },
	{ "test-archive", fr_window_activate_test_archive },
	{ "view-properties", fr_window_activate_view_properties },
	{ "view-selection", fr_window_activate_view_selection },
	{ "view-sidebar", toggle_action_activated, NULL, "false", fr_window_activate_view_sidebar }
};


static const FrAccelerator fr_window_accelerators[] = {
	{ "close", "<Control>w" },
	{ "deselect-all", "<Shift><Control>a" },
	{ "edit-copy", "<Control>c" },
	{ "extract-files", "<Control>e" },
	{ "find", "<Control>f" },
	{ "reload", "<Control>r" },
	{ "rename", "F2" },
	{ "save-as", "<Shift><Control>s" },	
	{ "select-all", "<control>a" },
	{ "view-properties", "<alt>Return" },
	{ "view-sidebar", "F9" }
};


#endif /* FR_WINDOW_ACTION_ENTRIES_H */
