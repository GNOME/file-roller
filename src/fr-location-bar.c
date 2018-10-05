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


static gboolean
fr_location_bar_draw (GtkWidget    *widget,
		      cairo_t      *cr)
{
	GtkStyleContext *context;
	guint            border_width;

	context = gtk_widget_get_style_context (widget);
	border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));
	gtk_render_background (context,
			       cr,
			       border_width,
			       border_width,
	                       gtk_widget_get_allocated_width (widget) - 2 * border_width,
	                       gtk_widget_get_allocated_height (widget) - 2 * border_width);
	gtk_render_frame (context,
			  cr,
			  border_width,
			  border_width,
	                  gtk_widget_get_allocated_width (widget) - 2 * border_width,
	                  gtk_widget_get_allocated_height (widget) - 2 * border_width);

	GTK_WIDGET_CLASS (fr_location_bar_parent_class)->draw (widget, cr);

	return FALSE;
}


static void
fr_location_bar_class_init (FrLocationBarClass *klass)
{
	GtkWidgetClass *widget_class;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->draw = fr_location_bar_draw;
}


static const char *css =
".location-bar {\n"
"	border-width: 0 0 1px 0;\n" /* remove the top border, already provided by the headerbar */
"}";


static void
fr_location_bar_init (FrLocationBar *self)
{
	GtkStyleContext *style_context;
	GtkCssProvider  *css_provider;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
	gtk_box_set_spacing (GTK_BOX (self), 6);

	style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
	gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_TOOLBAR);
	gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
	gtk_style_context_add_class (style_context, "location-bar");

	css_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css_provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (css_provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


GtkWidget *
fr_location_bar_new (void)
{
	return (GtkWidget *) g_object_new (fr_location_bar_get_type (), NULL);
}
