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
	FILE_MENU_RECENTS_MENU,
	FILE_MENU_SEP1,
	FILE_MENU_OPEN,
	FILE_MENU_VIEW,
	FILE_MENU_SEP2,
	FILE_MENU_RANAME_ARCHIVE,
	FILE_MENU_COPY_ARCHIVE,
	FILE_MENU_MOVE_ARCHIVE,
	FILE_MENU_DELETE_ARCHIVE,
	FILE_MENU_SEP3,
	FILE_MENU_ARCHIVE_PROP,
	FILE_MENU_SEP4,
	FILE_MENU_CLOSE_ARCHIVE,
	FILE_MENU_EXIT,
	FILE_MENU_LENGTH
};

GnomeUIInfo recents_menu[] = {
	GNOMEUIINFO_END
};

GnomeUIInfo file_menu[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("_New"), N_("Create a new archive"), 
	  new_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_NEW,
	  'n', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Open..."), N_("Open an existing archive"), 
	  open_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_OPEN,
	  'o', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SUBTREE (N_("Open R_ecent"), recents_menu),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Open Fi_les..."), N_("Open selected files with an application"), 
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
	  N_("_Rename"), N_("Rename current archive"), 
	  rename_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Cop_y"), N_("Copy current archive to another folder"), 
	  copy_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Move"), N_("Move current archive to another folder"), 
	  move_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Delete"), N_("Delete current archive from disk"), 
	  delete_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_DELETE,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM,
	  N_("_Properties"), N_("Show archive properties"),
	  dlg_prop, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_PROPERTIES,
	  'i', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Close"), N_("Close current archive"), 
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
	EDIT_MENU_ADD = 0,
	EDIT_MENU_DELETE,
	EDIT_MENU_EXTRACT,
	/*EDIT_MENU_UUENCODE,*/
	EDIT_MENU_TEST,
	/*EDIT_MENU_COMMENT,*/
	EDIT_MENU_SEP1,
	EDIT_MENU_SELECT_ALL,
	EDIT_MENU_DESELECT_ALL,
	EDIT_MENU_SEP2,
	EDIT_MENU_PASSWORD,
	EDIT_MENU_SEP3,
	EDIT_MENU_PREFERENCES,
	EDIT_MENU_LENGTH
};

GnomeUIInfo edit_menu[] = {
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

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Select _All"), N_("Select all files"), 
	  select_all_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'a', GDK_CONTROL_MASK, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Dese_lect All"), N_("Deselect all files"), 
	  deselect_all_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  'a', GDK_SHIFT_MASK | GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Pass_word..."), " ", 
	  dlg_password, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Preferences"), " ", 
	  dlg_preferences, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_PREFERENCES, 
	  'p', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_END
};


GnomeUIInfo sort_by_radio_list[] = {
        { GNOME_APP_UI_ITEM, 
          N_("by _Name"), N_("Sort file list by name"), 
          sort_list_by_name, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("by _Size"), N_("Sort file list by file size"), 
	  sort_list_by_size, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("by T_ype"), N_("Sort file list by type"), 
	  sort_list_by_type, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },
        
        { GNOME_APP_UI_ITEM, 
          N_("by _Time"), N_("Sort file list by modification time"), 
	  sort_list_by_time, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },

        { GNOME_APP_UI_ITEM, 
          N_("by _Path"), N_("Sort file list by path"), 
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
	ARRANGE_MENU_SORT_LIST = 0,
	ARRANGE_MENU_SEP1,
	ARRANGE_MENU_REVERSED_ORDER
};


GnomeUIInfo arrange_menu[] = {
	GNOMEUIINFO_RADIOLIST (sort_by_radio_list),

	GNOMEUIINFO_SEPARATOR,

        { GNOME_APP_UI_TOGGLEITEM, 
          N_("_Reversed Order"), N_("Reverse the list order"), 
	  sort_list_reversed, NULL, NULL,
          GNOME_APP_PIXMAP_NONE, NULL,
          0, 0, NULL },
	
	GNOMEUIINFO_END
};

enum {
	VIEW_MENU_TOOLBAR,
	VIEW_MENU_STATUSBAR,
	VIEW_MENU_SEP1,
	VIEW_MENU_STOP,
	VIEW_MENU_RELOAD,
	VIEW_MENU_SEP2,
	VIEW_MENU_VIEW_LIST,
	VIEW_MENU_SEP3,
	VIEW_MENU_SORT_LIST,
	VIEW_MENU_SEP4,
	VIEW_MENU_LAST_OUTPUT
};

GnomeUIInfo view_menu[] = {
	{ GNOME_APP_UI_TOGGLEITEM, 
	  N_("_Toolbar"), N_("View the main toolbar"), 
	  view_toolbar_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_TOGGLEITEM, 
	  N_("Stat_usbar"), N_("View the statusbar"), 
	  view_statusbar_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Stop"), N_("Stop current operation"), 
	  stop_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_STOP,
	  GDK_Escape, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("_Reload"), N_("Reload current archive"), 
	  reload_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_REFRESH,
	  'r', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_RADIOLIST (view_list),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE (N_("_Arrange Files"), arrange_menu),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("_Last Output"), " ", 
	  last_output_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
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
	GNOMEUIINFO_SUBTREE (N_("_File"), file_menu),
	GNOMEUIINFO_SUBTREE (N_("_Edit"), edit_menu),
	GNOMEUIINFO_SUBTREE (N_("_View"), view_menu),
	GNOMEUIINFO_SUBTREE (N_("_Windows"), windows_menu),
	GNOMEUIINFO_MENU_HELP_TREE (help_menu),
	GNOMEUIINFO_END
};



/* popup menus. */

enum {
	FILE_POPUP_MENU_OPEN,
	FILE_POPUP_MENU_VIEW,
	FILE_POPUP_MENU_SEP1,
	FILE_POPUP_MENU_ADD,
	FILE_POPUP_MENU_DELETE,
	FILE_POPUP_MENU_EXTRACT,
	FILE_POPUP_MENU_SEP2,
	FILE_POPUP_MENU_SELECT_ALL
};


GnomeUIInfo file_popup_menu_data[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("Open Files..."), " ", 
	  open_with_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("View File"), N_("View file with internal viewer"), 
	  view_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, 0,
	  0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Add Files..."), N_("Add files to the archive"), 
	  add_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_ADD,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Delete Files"), N_("Delete files from the archive"), 
	  dlg_delete, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_REMOVE,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Extract to..."), N_("Extract files from the archive"), 
	  dlg_extract, NULL, NULL,
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
