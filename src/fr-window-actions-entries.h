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
	{
		.name = "add-files",
		.activate = fr_window_activate_add_files,
	},
	{
		.name = "close",
		.activate = fr_window_activate_close,
	},
	{
		.name = "delete",
		.activate = fr_window_activate_delete,
	},
	{
		.name = "deselect-all",
		.activate = fr_window_activate_deselect_all,
	},
	{
		.name = "edit-copy",
		.activate = fr_window_activate_edit_copy,
	},
	{
		.name = "edit-cut",
		.activate = fr_window_activate_edit_cut,
	},
	{
		.name = "edit-password",
		.activate = fr_window_activate_edit_password,
	},
	{
		.name = "edit-paste",
		.activate = fr_window_activate_edit_paste,
	},
	{
		.name = "extract-files",
		.activate = fr_window_activate_extract_files,
	},
	{
		.name = "extract-all-by-default",
		.activate = fr_window_activate_extract_all_by_default,
	},
	{
		.name = "find",
		.activate = fr_toggle_action_activated,
		.state = "false",
		.change_state = fr_window_activate_find,
	},
	{
		.name = "go-back",
		.activate = fr_window_activate_go_back,
	},
	{
		.name = "go-forward",
		.activate = fr_window_activate_go_forward,
	},
	{
		.name = "go-home",
		.activate = fr_window_activate_go_home,
	},
	{
		.name = "go-up-one-level",
		.activate = fr_window_activate_go_up_one_level,
	},
	{
		.name = "navigate-to",
		.activate = fr_window_activate_navigate_to,
	},
	{
		.name = "open-folder",
		.activate = fr_window_activate_open_folder,
	},
	{
		.name = "open-with",
		.activate = fr_window_activate_open_with,
	},
	{
		.name = "reload",
		.activate = fr_window_activate_reload,
	},
	{
		.name = "rename",
		.activate = fr_window_activate_rename,
	},
	{
		.name = "save-as",
		.activate = fr_window_activate_save_as,
	},
	{
		.name = "select-all",
		.activate = fr_window_activate_select_all,
	},
	{
		.name = "sidebar-delete",
		.activate = fr_window_activate_sidebar_delete,
	},
	{
		.name = "sidebar-edit-copy",
		.activate = fr_window_activate_sidebar_edit_copy,
	},
	{
		.name = "sidebar-edit-cut",
		.activate = fr_window_activate_sidebar_edit_cut,
	},
	{
		.name = "sidebar-edit-paste",
		.activate = fr_window_activate_sidebar_edit_paste,
	},
	{
		.name = "sidebar-extract-files",
		.activate = fr_window_activate_sidebar_extract_files,
	},
	{
		.name = "sidebar-open-folder",
		.activate = fr_window_activate_sidebar_open_folder,
	},
	{
		.name = "sidebar-rename",
		.activate = fr_window_activate_sidebar_rename,
	},
	{
		.name = "stop",
		.activate = fr_window_activate_stop,
	},
	{
		.name = "test-archive",
		.activate = fr_window_activate_test_archive,
	},
	{
		.name = "view-properties",
		.activate = fr_window_activate_view_properties,
	},
	{
		.name = "view-selection",
		.activate = fr_window_activate_view_selection,
	},
	{
		.name = "view-sidebar",
		.activate = fr_toggle_action_activated,
		.parameter_type = NULL,
		.state = "false",
		.change_state = fr_window_activate_view_sidebar,
	},
	{
		.name = "focus-location",
		.activate = fr_window_activate_focus_location,
	},
};


static const FrAccelerator fr_window_accelerators[] = {
	{ "win.close", "<Control>w" },
	{ "win.deselect-all", "<Shift><Control>a" },
	{ "win.edit-copy", "<Control>c" },
	{ "win.extract-files", "<Control>e" },
	{ "win.find", "<Control>f" },
	{ "win.reload", "<Control>r" },
	{ "win.rename", "F2" },
	{ "win.save-as", "<Shift><Control>s" },
	{ "win.select-all", "<control>a" },
	{ "win.view-properties", "<alt>Return" },
	{ "win.view-sidebar", "F9" },
	{ "win.focus-location", "<Control>l" },
	{ "win.add-files", "<Control>plus" },
};


#endif /* FR_WINDOW_ACTION_ENTRIES_H */
