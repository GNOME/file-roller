/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2013 The Free Software Foundation, Inc.
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

#define FR_TYPE_LOCATION_BAR            (fr_location_bar_get_type ())
#define FR_LOCATION_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_LOCATION_BAR, FrLocationBar))
#define FR_LOCATION_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_LOCATION_BAR, FrLocationBarClass))
#define FR_IS_LOCATION_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_LOCATION_BAR))
#define FR_IS_LOCATION_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_LOCATION_BAR))
#define FR_LOCATION_BAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), FR_TYPE_LOCATION_BAR, FrLocationBarClass))

typedef struct _FrLocationBar FrLocationBar;
typedef struct _FrLocationBarClass FrLocationBarClass;
typedef struct _FrLocationBarPrivate FrLocationBarPrivate;

struct _FrLocationBar {
	GtkBox parent_instance;
	FrLocationBarPrivate *priv;
};

struct _FrLocationBarClass {
	GtkBoxClass parent_class;
};

GType		fr_location_bar_get_type	(void);
GtkWidget *	fr_location_bar_new		(void);

#endif /* FR_LOCATION_BAR_H */
