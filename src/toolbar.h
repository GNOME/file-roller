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

#ifndef TOOLBAR_H
#define TOOLBAR_H


/* Definition of the toolbar. */

enum {
	TOOLBAR_NEW = 0,
	TOOLBAR_OPEN,
	TOOLBAR_SEP1,
	TOOLBAR_SEP2,
	TOOLBAR_STOP,
};

GnomeUIInfo toolbar_data[] = {
	{ GNOME_APP_UI_ITEM, 
	  N_("New"), N_("Create a new archive"), 
	  new_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_NEW,
	  0, 0, NULL },

	{ GNOME_APP_UI_ITEM, 
	  N_("Open"), N_("Open archive"), 
	  open_archive_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_OPEN,
	  0, 0, NULL },
	
	GNOMEUIINFO_SEPARATOR,
	
	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, 
	  N_("Stop"), N_("Stop current operation"), 
	  stop_cb, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_STOP, 
	  0, 0, NULL },

	GNOMEUIINFO_END
};

#endif /* TOOLBAR_H */
