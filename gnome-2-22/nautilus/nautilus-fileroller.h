/*
 *  File-Roller
 * 
 *  Copyright (C) 2004 Free Software Foundation, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Paolo Bacchilega <paobac@cvs.gnome.org>
 * 
 */

#ifndef NAUTILUS_FILEROLLER_H
#define NAUTILUS_FILEROLLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_FR  (nautilus_fr_get_type ())
#define NAUTILUS_FR(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), NAUTILUS_TYPE_FR, NautilusFr))
#define NAUTILUS_IS_FR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), NAUTILUS_TYPE_FR))

typedef struct _NautilusFr      NautilusFr;
typedef struct _NautilusFrClass NautilusFrClass;

struct _NautilusFr {
	GObject __parent;
};

struct _NautilusFrClass {
	GObjectClass __parent;
};

GType nautilus_fr_get_type      (void);
void  nautilus_fr_register_type (GTypeModule *module);

G_END_DECLS

#endif /* NAUTILUS_FILEROLLER_H */
