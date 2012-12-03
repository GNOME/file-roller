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

#define FR_TYPE_APPLICATION            (fr_application_get_type ())
#define FR_APPLICATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_APPLICATION, FrApplication))
#define FR_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_APPLICATION, FrApplicationClass))
#define FR_IS_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_APPLICATION))
#define FR_IS_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_APPLICATION))
#define FR_APPLICATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_APPLICATION, FrApplicationClass))

typedef struct _FrApplication         FrApplication;
typedef struct _FrApplicationClass    FrApplicationClass;
typedef struct _FrApplicationPrivate  FrApplicationPrivate;

struct _FrApplication {
	GtkApplication __parent;
	FrApplicationPrivate *priv;
};

struct _FrApplicationClass {
	GtkApplicationClass __parent_class;
};

GType            fr_application_get_type      (void);
GtkApplication * fr_application_new           (void);
GSettings *      fr_application_get_settings  (FrApplication *app,
		     	     	     	       const char    *schema);

#endif /* FR_APPLICATION_H */
