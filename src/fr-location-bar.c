/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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
#include "fr-location-bar.h"


struct _FrLocationBar {
       GtkBox parent_instance;
};


G_DEFINE_TYPE (FrLocationBar, fr_location_bar, GTK_TYPE_BOX)


static const char *css =
".location-bar {\n"
"	border-width: 0 0 1px 0;\n" /* remove the top border, already provided by the headerbar */
"}";




GtkWidget *
fr_location_bar_new (void)
{
	return (GtkWidget *) g_object_new (fr_location_bar_get_type (), NULL);
}
