/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2022 The Free Software Foundation, Inc.
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

#ifndef FR_LOCATION_BUTTON_H
#define FR_LOCATION_BUTTON_H

#include <gtk/gtk.h>

#define FR_TYPE_LOCATION_BUTTON (fr_location_button_get_type ())
G_DECLARE_FINAL_TYPE (FrLocationButton, fr_location_button, FR, LOCATION_BUTTON, GtkButton)

struct _FrLocationButton {
	GtkButton parent_class;
};

struct _FrLocationButtonClass {
	GtkButtonClass parent_class;
	void (* changed) (FrLocationButton *location_button);
};

GtkWidget * fr_location_button_new (const char *title);

/**
 * fr_location_button_get_location:
 * Returns: (transfer full)
 */
GFile * fr_location_button_get_location (FrLocationButton *dialog);

void    fr_location_button_set_location (FrLocationButton *dialog,
					 GFile            *location);

#endif /* FR_LOCATION_BUTTON_H */
