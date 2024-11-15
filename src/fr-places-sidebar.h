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

#ifndef FR_PLACES_SIDEBAR_H
#define FR_PLACES_SIDEBAR_H

#include <gtk/gtk.h>
#include "typedefs.h"

#define FR_FILE_ATTRIBUTE_VOLUME "fr::volume"

#define FR_TYPE_PLACES_SIDEBAR fr_places_sidebar_get_type ()
G_DECLARE_DERIVABLE_TYPE (FrPlacesSidebar, fr_places_sidebar, FR, PLACES_SIDEBAR, GtkBox)

struct _FrPlacesSidebarClass {
	GtkBoxClass __parent_class;

	/*< signals >*/

	void (*open) (FrPlacesSidebar *self, GFile *location);
};

GtkWidget * fr_places_sidebar_new (void);
void        fr_places_sidebar_set_location (FrPlacesSidebar *self, GFile *location);

#endif /* FR_PLACES_SIDEBAR_H */
