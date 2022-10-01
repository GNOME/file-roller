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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include "typedefs.h"
#include "preferences.h"
#include "fr-init.h"
#include "file-utils.h"
#include "fr-window.h"


void
pref_util_save_window_geometry (GtkWindow  *window,
				const char *dialog_id)
{
	char      *schema;
	GSettings *settings;
	int        width;
	int        height;

	schema = g_strconcat (FILE_ROLLER_SCHEMA_DIALOGS, ".", dialog_id, NULL);
	settings = g_settings_new (schema);

	gtk_window_get_default_size (window, &width, &height);
	g_settings_set_int (settings, "width", width);
	g_settings_set_int (settings, "height", height);

	g_object_unref (settings);
	g_free (schema);
}


void
pref_util_restore_window_geometry (GtkWindow  *window,
				   const char *dialog_id)
{
	char      *schema;
	GSettings *settings;
	int        width;
	int        height;

	schema = g_strconcat (FILE_ROLLER_SCHEMA_DIALOGS, ".", dialog_id, NULL);
	settings = g_settings_new (schema);

	width = g_settings_get_int (settings, "width");
	height = g_settings_get_int (settings, "height");
	gtk_window_present (window);
	if ((width > 0) && (height > 0))
		gtk_window_set_default_size (window, width, height);

	g_object_unref (settings);
	g_free (schema);
}
