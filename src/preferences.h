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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <gtk/gtkenums.h>
#include "typedefs.h"


typedef struct {
	WindowSortMethod  sort_method;
	GtkSortType       sort_type;    /* ascending or discending. */
	WindowListMode    list_mode;

	guint             show_name : 1;
	guint             show_type : 1;
	guint             show_size : 1;
	guint             show_time : 1;
	guint             show_path : 1;

	gboolean          use_mime_icons;

	int               max_history_len;

	/* Other editors. */

	int               editors_n;
	GList *           editors;   /* char * elements. */

	/* Destop options. */

	gboolean          menus_have_tearoff;
	gboolean          toolbar_detachable;
	gchar *           nautilus_theme;

	gboolean          install_scripts;     /* first time automatic 
						* installation. */
	gboolean          scripts_installed;   /* whether the scripts are
						* instaleld. */

	/* Extraction options */

	gboolean          view_folder;

	/* Add options */
	FRCompression     compression;
} Preferences;


void  preferences_load    ();
void  preferences_save    ();
void  preferences_release ();


#endif /* PREFERENCES_H */
