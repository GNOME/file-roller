/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* 
 * Copyright (C) 2002 James Willcox
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author:  James Willcox  <jwillcox@gnome.org>
 */

#ifndef FILE_ROLLER_COMPONENT_H
#define FILE_ROLLER_COMPONENT_H

#include <bonobo/bonobo-object.h>

#define TYPE_FILE_ROLLER_COMPONENT	     (file_roller_component_get_type ())
#define FILE_ROLLER_COMPONENT(obj)	     (GTK_CHECK_CAST ((obj), TYPE_FILE_ROLLER_COMPONENT, FileRollerComponent))
#define FILE_ROLLER_COMPONENT_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), TYPE_FILE_ROLLER_COMPONENT, FileRollerComponentClass))
#define IS_FILE_ROLLER_COMPONENT(obj)	     (GTK_CHECK_TYPE ((obj), TYPE_FILE_ROLLER_COMPONENT))
#define IS_FILE_ROLLER_COMPONENT_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), TYPE_FILE_ROLLER_COMPONENT))

typedef struct {
	BonoboObject parent;
} FileRollerComponent;

typedef struct {
	BonoboObjectClass parent;

	POA_Bonobo_Listener__epv epv;
} FileRollerComponentClass;

GType file_roller_component_get_type (void);

#endif /* FILE_ROLLER_COMPONENT_H */
