/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
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

#ifndef MENU_H
#define MENU_H


#include <config.h>

#include "dlg-add.h"
#include "dlg-delete.h"
#include "dlg-extract.h"
#include "dlg-open-with.h"
#include "dlg-password.h"
#include "dlg-preferences.h"
#include "dlg-prop.h"
#include "menu-callbacks.h"


/* Definition of the File menu */

enum {
	FILE_MENU_NEW_ARCHIVE = 0,
	FILE_MENU_OPEN_ARCHIVE,
	FILE_MENU_SEP3,
	FILE_MENU_ARCHIVE_PROP,
	FILE_MENU_SEP4,
	FILE_MENU_MOVE_ARCHIVE,
	FILE_MENU_COPY_ARCHIVE,
	FILE_MENU_RANAME_ARCHIVE,
	FILE_MENU_DELETE_ARCHIVE,
	FILE_MENU_SEP1,
	FILE_MENU_CLOSE_ARCHIVE,
	FILE_MENU_EXIT,
	FILE_MENU_LENGTH
};

GnomeUIInfo file_menu[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("New _Archive"), N_("Create a new archive"), 
	  new_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_NEW,
	  'n', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Open Archive..."), N_("Open an existing archive"), 
	  open_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_OPEN,
	  'o', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM,
	  N_("Archive _Information"), N_("Show information about the archive"),
	  dlg_prop, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Move Archive"), " ", 
	  move_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Copy Arc_hive"), " ", 
	  copy_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Rename Archive"), " ", 
	  rename_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Delete Archive"), " ", 
	  delete_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Close Archive"), N_("Close current archive"), 
	  close_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_CLOSE,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("E_xit"), N_("Exit"), 
	  quit_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_QUIT, 
	  'q', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_END
};

enum {
	ACTIONS_MENU_ADD = 0,
	ACTIONS_MENU_DELETE,
	ACTIONS_MENU_EXTRACT,
	ACTIONS_MENU_SEP1,
	ACTIONS_MENU_SELECT_ALL,
	ACTIONS_MENU_SEP2,
	ACTIONS_MENU_OPEN,
	ACTIONS_MENU_VIEW,
	ACTIONS_MENU_SEP3,
	ACTIONS_MENU_STOP,
	ACTIONS_MENU_SEP4,
	/*ACTIONS_MENU_UUENCODE,*/
	ACTIONS_MENU_TEST,
	/*ACTIONS_MENU_COMMENT,*/
	ACTIONS_MENU_VIEW_LAST_OUPUT,
	ACTIONS_MENU_LENGTH
};

GnomeUIInfo actions_menu[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("Add _Files..."), N_("Add files to the archive"), 
	  add_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_ADD,
	  'z', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Delete"), N_("Delete selected files and folders from the archive"), 
	  dlg_delete, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_REMOVE,
	  'd', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Extract to..."), N_("Extract files from the archive"), 
	  dlg_extract, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'x', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Select _All"), N_("Select all files"), 
	  select_all_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'a', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Open Files with..."), N_("Open selected files with an application"), 
	  open_with_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_View File"), N_("View file with internal viewer"), 
	  view_or_open_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'v', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Stop Operation"), N_("Stop current operation"), 
	  stop_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_STOP,
	  GDK_Escape, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	/* FIXME
	{ GNOME_APP_UI_ITEM, 
	  N_("_UUencode..."), " ",
	  NULL, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },
	*/

	{ GNOME_APP_UI_ITEM, 
	  N_("_Test Archive"), " ",
	  test_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	/* FIXME
	{ GNOME_APP_UI_ITEM, 
	  N_("C_omment..."), " ",
	  NULL, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },
	*/

	{ GNOME_APP_UI_ITEM, 
	  N_("Vi_ew Last Output"), " ", 
	  last_output_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_END
};


GnomeUIInfo sort_by_radio_list[] = {
        { GNOME_APP_UI_ITEM, 
          N_("Arrange by _Name"), N_("Sort file list by name"), 
          sort_list_by_name, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("Arrange by _Size"), N_("Sort file list by file size"), 
	  sort_list_by_size, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("Arrange by _Type"), N_("Sort file list by type"), 
	  sort_list_by_type, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },
        
        { GNOME_APP_UI_ITEM, 
          N_("Arrange by _Time"), N_("Sort file list by modification time"), 
	  sort_list_by_time, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("Arrange by _Path"), N_("Sort file list by path"), 
	  sort_list_by_path, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        GNOMEUIINFO_END
};


enum {
	VIEW_LIST_VIEW_ALL = 0,
	VIEW_LIST_AS_DIR
};


GnomeUIInfo view_list[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("View All _Files"), " ", 
	  set_list_mode_flat_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("View as a F_older"), " ", 
	  set_list_mode_as_dir_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_END
};


enum {
	OPTIONS_MENU_PREFERENCES = 0,
	OPTIONS_MENU_PASSWORD,
	OPTIONS_MENU_SEP1,
	OPTIONS_MENU_VIEW_LIST,
	OPTIONS_MENU_SEP2,
	OPTIONS_MENU_SORT_LIST,
	OPTIONS_MENU_SEP3,
	OPTIONS_MENU_REVERSED_ORDER
};


GnomeUIInfo options_menu[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("_Preferences"), " ", 
	  dlg_preferences, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_PREFERENCES, 
	  'p', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Pass_word..."), " ", 
	  dlg_password, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_RADIOLIST (view_list),

	GNOMEUIINFO_SEPARATOR,

        GNOMEUIINFO_RADIOLIST (sort_by_radio_list),

	GNOMEUIINFO_SEPARATOR,

        { GNOME_APP_UI_TOGGLEITEM, 
          N_("_Reversed Order"), N_("Reverse the list order"), 
	  sort_list_reversed, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

	GNOMEUIINFO_END
};


/* Definition of the Windows menu */

GnomeUIInfo windows_menu[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("New _Window"), N_("Open a new window"), 
	  new_window_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'w', GDK_SHIFT_MASK | GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Clo_se Window"), N_("Close this window"), 
	  close_window_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'w', GDK_CONTROL_MASK, NULL },
	
	GNOMEUIINFO_END
};


/* Definition of the Help menu */

GnomeUIInfo help_menu[] = {
	{ GNOME_APP_UI_ITEM, 
          N_("_Manual"), N_("Display the File Roller Manual"), 
          manual_cb, NULL, NULL,
          GNOME_APP_PIXMAP_STOCK, GTK_STOCK_HELP,
          0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_About"), N_("Information about the program"), 
	  about_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_ABOUT, 
	  0, 0, NULL },
	
	GNOMEUIINFO_END
};


/* Definition of the main menu */

GnomeUIInfo main_menu[] = {
	GNOMEUIINFO_MENU_FILE_TREE (file_menu),
	GNOMEUIINFO_SUBTREE (N_("_Actions"), actions_menu),
	GNOMEUIINFO_SUBTREE (N_("_Options"), options_menu),
	GNOMEUIINFO_SUBTREE (N_("_Windows"), windows_menu),
	GNOMEUIINFO_MENU_HELP_TREE (help_menu),
	GNOMEUIINFO_END
};



/* popup menus. */

enum {
	FILE_POPUP_MENU_ADD,
	FILE_POPUP_MENU_DELETE,
	FILE_POPUP_MENU_EXTRACT,
	FILE_POPUP_MENU_SEP1,
	FILE_POPUP_MENU_OPEN,
	FILE_POPUP_MENU_VIEW,
	FILE_POPUP_MENU_SEP2,
	FILE_POPUP_MENU_SELECT_ALL,
};


GnomeUIInfo file_popup_menu_data[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("Add _Files..."), N_("Add files to the archive"), 
	  add_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_ADD,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Delete Files"), N_("Delete files from the archive"), 
	  dlg_delete, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_REMOVE,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Extract to..."), N_("Extract files from the archive"), 
	  dlg_extract, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Open Files with..."), " ", 
	  open_with_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_View File"), N_("View file with internal viewer"), 
	  view_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Select All"), " ", 
	  select_all_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_END
};


#endif /* MENU_H */
