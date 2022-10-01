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

#ifndef FR_LOCATION_BAR_H
#define FR_LOCATION_BAR_H

#include <gtk/gtk.h>

#define FR_TYPE_LOCATION_BAR (fr_location_bar_get_type ())
G_DECLARE_FINAL_TYPE (FrLocationBar, fr_location_bar, FR, LOCATION_BAR, GtkBox)

struct _FrLocationBar {
	GtkBox parent_class;
};

struct _FrLocationBarClass {
	GtkBoxClass parent_class;
	void (* changed) (FrLocationBar *location_bar);
};

GtkWidget * fr_location_bar_new (void);

/**
 * fr_location_bar_get_location:
 * Returns: (transfer none)
 */
GFile * fr_location_bar_get_location (FrLocationBar *dialog);

void    fr_location_bar_set_location (FrLocationBar *dialog,
				      GFile         *location);

#endif /* FR_LOCATION_BAR_H */
