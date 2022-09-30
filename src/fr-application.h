/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 Free Software Foundation, Inc.
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


#ifndef FR_APPLICATION_H
#define FR_APPLICATION_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_DECLARE_FINAL_TYPE (FrApplication, fr_application, FR, APPLICATION, AdwApplication)

GtkApplication * fr_application_new           (void);

/**
 * fr_application_get_settings:
 * Returns: (transfer none)
 */
GSettings *      fr_application_get_settings  (FrApplication *app,
		     	     	     	       const char    *schema);

#endif /* FR_APPLICATION_H */
