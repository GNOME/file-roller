/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef UI_H
#define UI_H


#include <config.h>
#include <gnome.h>
#include "actions.h"
#include "fr-stock.h"


static GtkActionEntry action_entries[] = {
	{ "ArchiveMenu", NULL, N_("_Archive") },
	{ "EditMenu", NULL, N_("_Edit") },
	{ "ViewMenu", NULL, N_("_View") },
	{ "HelpMenu", NULL, N_("_Help") },
	{ "ArrangeFilesMenu", NULL, N_("_Arrange Files") },
	{ "OpenRecentMenu", NULL, N_("Open R_ecent") },

	{ "About", GTK_STOCK_ABOUT,
	  NULL, NULL,
	  N_("Information about the program"),
	  G_CALLBACK (activate_action_about) },
	{ "AddFiles", FR_STOCK_ADD_FILES,
	  N_("_Add Files..."), NULL,
	  N_("Add files to the archive"),
	  G_CALLBACK (activate_action_add_files) },
	{ "AddFiles_Toolbar", FR_STOCK_ADD_FILES,
	  N_("Add Files"), NULL,
	  N_("Add files to the archive"),
	  G_CALLBACK (activate_action_add_files) },
	{ "AddFolder", FR_STOCK_ADD_FOLDER,
	  N_("Add a _Folder..."), NULL,
	  N_("Add a folder to the archive"),
	  G_CALLBACK (activate_action_add_folder) },
	{ "AddFolder_Toolbar", FR_STOCK_ADD_FOLDER,
	  N_("Add Folder"), NULL,
	  N_("Add a folder to the archive"),
	  G_CALLBACK (activate_action_add_folder) },	  
	{ "Close", GTK_STOCK_CLOSE,
	  NULL, NULL,
	  N_("Close the current archive"),
	  G_CALLBACK (activate_action_close) },
	{ "Contents", GTK_STOCK_HELP,
	  N_("Contents"), "F1",
	  N_("Display the File Roller Manual"),
	  G_CALLBACK (activate_action_manual) },
	{ "Copy", GTK_STOCK_COPY,
	  NULL, NULL,
	  N_("Copy the selection"),
	  G_CALLBACK (activate_action_copy) },
	{ "Cut", GTK_STOCK_CUT,
	  NULL, NULL,
	  N_("Cut the selection"),
	  G_CALLBACK (activate_action_cut) },
	{ "Delete", GTK_STOCK_DELETE,
	  NULL, NULL,
	  N_("Delete the selection from the archive"),
	  G_CALLBACK (activate_action_delete) },
	{ "DeselectAll", NULL,
	  N_("Dese_lect All"), NULL,
	  N_("Deselect all files"),
	  G_CALLBACK (activate_action_deselect_all) },
	{ "Extract", FR_STOCK_EXTRACT,
	  N_("_Extract..."), NULL,
	  N_("Extract files from the archive"),
	  G_CALLBACK (activate_action_extract) },
	{ "Extract_Toolbar", FR_STOCK_EXTRACT,
	  N_("Extract"), NULL,
	  N_("Extract files from the archive"),
	  G_CALLBACK (activate_action_extract) },
	{ "LastOutput", NULL,
	  N_("_Last Output"), NULL,
	  N_("View the output produced by the last executed command"),
	  G_CALLBACK (activate_action_last_output) },
	{ "New", GTK_STOCK_NEW,
	  NULL, NULL,
	  N_("Create a new archive"),
	  G_CALLBACK (activate_action_new) },
	{ "Open", GTK_STOCK_OPEN,
	  NULL, NULL,
	  N_("Open archive"),
	  G_CALLBACK (activate_action_open) },
	{ "Open_Toolbar", GTK_STOCK_OPEN,
	  NULL, NULL,
	  N_("Open archive"),
	  G_CALLBACK (activate_action_open) },
	{ "OpenSelection", NULL,
	  N_("Op_en With..."), NULL,
	  N_("Open selected files with an application"),
	  G_CALLBACK (activate_action_open_with) },
	{ "Password", NULL,
	  N_("Pass_word..."), NULL,
	  N_("Specify a password for this archive"),
	  G_CALLBACK (activate_action_password) },
	{ "Paste", GTK_STOCK_PASTE,
	  NULL, NULL,
	  N_("Paste the clipboard"),
	  G_CALLBACK (activate_action_paste) },
	{ "Properties", GTK_STOCK_PROPERTIES,
	  NULL, NULL,
	  N_("Show archive properties"),
	  G_CALLBACK (activate_action_properties) },
	{ "Reload", GTK_STOCK_REFRESH,
	  NULL, "<control>R",
	  N_("Reload current archive"),
	  G_CALLBACK (activate_action_reload) },
	{ "Rename", NULL,
	  N_("_Rename..."), "F2",
	  N_("Rename the selection"),
	  G_CALLBACK (activate_action_rename) },
	{ "SaveAs", GTK_STOCK_SAVE_AS,
	  NULL, NULL,
	  N_("Save the current archive with a different name"),
	  G_CALLBACK (activate_action_save_as) },
	{ "SelectAll", NULL,
	  N_("Select _All"), "<control>A",
	  N_("Select all files"),
	  G_CALLBACK (activate_action_select_all) },
	{ "Stop", GTK_STOCK_STOP,
	  NULL, "Escape",
	  N_("Stop current operation"),
	  G_CALLBACK (activate_action_stop) },
	{ "TestArchive", NULL,
	  N_("_Test Integrity"), NULL,
	  N_("Test whether the archive contains errors"),
	  G_CALLBACK (activate_action_test_archive) },
	{ "ViewSelection", FR_STOCK_VIEW,
	  NULL, NULL,
	  N_("View the selected file"),
	  G_CALLBACK (activate_action_view_or_open) },
	{ "ViewSelection_Toolbar", FR_STOCK_VIEW,
	  NULL, NULL,
	  N_("View the selected file"),
	  G_CALLBACK (activate_action_view_or_open) },
	{ "OpenFolder", GTK_STOCK_OPEN,
	  NULL, NULL,
	  N_("Open the selected folder"),
	  G_CALLBACK (activate_action_open_folder) },
};
static guint n_action_entries = G_N_ELEMENTS (action_entries);


static GtkToggleActionEntry action_toggle_entries[] = {
	{ "ViewToolbar", NULL,
	  N_("_Toolbar"), NULL,
	  N_("View the main toolbar"),
	  G_CALLBACK (activate_action_view_toolbar), 
	  TRUE },
	{ "ViewStatusbar", NULL,
	  N_("Stat_usbar"), NULL,
	  N_("View the statusbar"),
	  G_CALLBACK (activate_action_view_statusbar), 
	  TRUE },
	{ "SortReverseOrder", NULL,
	  N_("_Reversed Order"), NULL,
	  N_("Reverse the list order"),
	  G_CALLBACK (activate_action_sort_reverse_order), 
	  FALSE },
};
static guint n_action_toggle_entries = G_N_ELEMENTS (action_toggle_entries);


static GtkRadioActionEntry view_as_entries[] = {
	{ "ViewAllFiles", NULL,
	  N_("View All _Files"), NULL,
	  " ", WINDOW_LIST_MODE_FLAT },
	{ "ViewAsFolder", NULL,
	  N_("View as a F_older"), NULL,
	  " ", WINDOW_LIST_MODE_AS_DIR },
};
static guint n_view_as_entries = G_N_ELEMENTS (view_as_entries);


static GtkRadioActionEntry sort_by_entries[] = {
	{ "SortByName", NULL,
	  N_("by _Name"), NULL,
	  N_("Sort file list by name"), WINDOW_SORT_BY_NAME },
	{ "SortBySize", NULL,
	  N_("by _Size"), NULL,
	  N_("Sort file list by file size"), WINDOW_SORT_BY_SIZE },
	{ "SortByType", NULL,
	  N_("by T_ype"), NULL,
	  N_("Sort file list by type"), WINDOW_SORT_BY_TYPE },
	{ "SortByDate", NULL,
	  N_("by _Date modified"), NULL,
	  N_("Sort file list by modification time"), WINDOW_SORT_BY_TIME },
	{ "SortByLocation", NULL,
	  N_("by _Location"), NULL,
	  N_("Sort file list by location"), WINDOW_SORT_BY_PATH },
};
static guint n_sort_by_entries = G_N_ELEMENTS (sort_by_entries);


static const gchar *ui_info = 
"<ui>"
"  <menubar name='MenuBar'>"
"    <menu name='Archive' action='ArchiveMenu'>"
"      <menuitem action='New'/>"
"      <menuitem action='Open'/>"
"      <menu name='OpenRecentMenu' action='OpenRecentMenu'>"
"        <menuitem action='Open'/>"
"      </menu>"
"      <menuitem action='SaveAs'/>"
"      <separator/>"
"      <menuitem action='Extract'/>"
"      <menuitem action='TestArchive'/>"
"      <separator/>"
"      <menuitem action='Properties'/>"
"      <separator/>"
"      <menuitem action='Close'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='AddFiles'/>"
"      <menuitem action='AddFolder'/>"
"      <separator/>"
"      <menuitem action='Cut'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Paste'/>"
"      <menuitem action='Rename'/>"
"      <menuitem action='Delete'/>"
"      <separator/>"
"      <menuitem action='SelectAll'/>"
"      <menuitem action='DeselectAll'/>"
"      <separator/>"
"      <menuitem action='Password'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='ViewToolbar'/>"
"      <menuitem action='ViewStatusbar'/>"
"      <separator/>"
"      <menuitem action='Stop'/>"
"      <menuitem action='Reload'/>"
"      <separator/>"
"      <menuitem action='ViewAllFiles'/>"
"      <menuitem action='ViewAsFolder'/>"
"      <separator/>"
"      <menu action='ArrangeFilesMenu'>"
"        <menuitem action='SortByName'/>"
"        <menuitem action='SortBySize'/>"
"        <menuitem action='SortByType'/>"
"        <menuitem action='SortByDate'/>"
"        <menuitem action='SortByLocation'/>"
"        <separator/>"
"        <menuitem action='SortReverseOrder'/>"
"      </menu>"
"      <separator/>"
"      <menuitem action='LastOutput'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='Contents'/>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"  <toolbar  name='ToolBar'>"
"    <toolitem action='New'/>"
"    <toolitem action='Extract_Toolbar'/>"
"    <separator/>"
"    <toolitem action='AddFiles_Toolbar'/>"
"    <toolitem action='AddFolder_Toolbar'/>"
"    <toolitem action='ViewSelection_Toolbar'/>"
"    <separator/>"
"    <toolitem action='Stop'/>"
"  </toolbar>"
"  <popup name='FilePopupMenu'>"
"    <menuitem action='ViewSelection'/>"
"    <menuitem action='OpenSelection'/>"
"    <separator/>"
"    <menuitem action='Extract'/>"
"    <separator/>"
"    <menuitem action='Cut'/>"
"    <menuitem action='Copy'/>"
"    <menuitem action='Paste'/>"
"    <menuitem action='Rename'/>"
"    <menuitem action='Delete'/>"
"  </popup>"
"  <popup name='FolderPopupMenu'>"
"    <menuitem action='OpenFolder'/>"
"    <separator/>"
"    <menuitem action='Extract'/>"
"    <separator/>"
"    <menuitem action='Cut'/>"
"    <menuitem action='Copy'/>"
"    <menuitem action='Paste'/>"
"    <menuitem action='Rename'/>"
"    <menuitem action='Delete'/>"
"  </popup>"
"  <popup name='AddMenu'>"
"    <menuitem action='AddFiles'/>"
"    <menuitem action='AddFolder'/>"
"  </popup>"

"</ui>";


#endif /* UI_H */
