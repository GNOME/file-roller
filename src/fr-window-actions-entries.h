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
	{ "edit-find", toggle_action_activated, NULL, "false", fr_window_activate_edit_find },
	{ "edit-password", fr_window_activate_edit_password },
	{ "extract-files", fr_window_activate_extract_files },
	{ "go-back", fr_window_activate_go_back },
	{ "go-forward", fr_window_activate_go_forward },
	{ "go-home", fr_window_activate_go_home },
	{ "save-as", fr_window_activate_save_as },
	{ "test-archive", fr_window_activate_test_archive },
	{ "view-properties", fr_window_activate_view_properties },
};


static const FrAccelerator fr_window_accelerators[] = {
	{ "close", "<Control>w" }
};


#endif /* FR_WINDOW_ACTION_ENTRIES_H */
