/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2011 Free Software Foundation, Inc.
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
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "gth-toggle-menu-action.h"
#include "gth-toggle-menu-tool-button.h"


G_DEFINE_TYPE (GthToggleMenuAction, gth_toggle_menu_action, GTK_TYPE_TOGGLE_ACTION)


/* Properties */
enum {
        PROP_0,
        PROP_SHOW_ARROW,
        PROP_MENU,
        PROP_MENU_HALIGN
};


struct _GthToggleMenuActionPrivate {
	gboolean         show_arrow;
	GtkWidget       *menu;
	GtkAlign         menu_halign;
	GthShowMenuFunc  show_menu_func;
	gpointer         show_menu_data;
	GDestroyNotify   show_menu_data_destroy;
};


static void
gth_toggle_menu_action_init (GthToggleMenuAction *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GTH_TYPE_TOGGLE_MENU_ACTION, GthToggleMenuActionPrivate);
	self->priv->show_arrow = TRUE;
	self->priv->menu = gtk_menu_new ();
	g_object_ref_sink (self->priv->menu);
}



static void
gth_toggle_menu_action_set_property (GObject      *object,
				     guint         property_id,
				     const GValue *value,
				     GParamSpec   *pspec)
{
	GthToggleMenuAction *self = GTH_TOGGLE_MENU_ACTION (object);
	GtkWidget           *tmp;

	switch (property_id) {
	case PROP_SHOW_ARROW:
		self->priv->show_arrow = g_value_get_boolean (value);
		break;
	case PROP_MENU:
		tmp = self->priv->menu;
		self->priv->menu = g_value_dup_object (value);
		g_object_unref (tmp);
		g_object_notify (G_OBJECT (self), "menu");
		break;
	case PROP_MENU_HALIGN:
		self->priv->menu_halign = g_value_get_enum (value);
		break;
	default:
		break;
	}
}


static void
gth_toggle_menu_action_get_property (GObject    *object,
				     guint       property_id,
				     GValue     *value,
				     GParamSpec *pspec)
{
	GthToggleMenuAction *self = GTH_TOGGLE_MENU_ACTION (object);

	switch (property_id) {
	case PROP_SHOW_ARROW:
		g_value_set_boolean (value, self->priv->show_arrow);
		break;
	case PROP_MENU:
		g_value_set_object (value, self->priv->menu);
		break;
	case PROP_MENU_HALIGN:
		g_value_set_enum (value, self->priv->menu_halign);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}


static void
tool_item_show_menu_cb (GthToggleMenuToolButton *button,
			gpointer                 user_data)
{
	GthToggleMenuAction *self = user_data;

	self->priv->show_menu_func (GTK_ACTION (self), self->priv->show_menu_data);
}


static GtkWidget *
gth_toggle_menu_action_create_tool_item (GtkAction *action)
{
	GthToggleMenuAction *self = GTH_TOGGLE_MENU_ACTION (action);
	GtkWidget           *tool_item;

	tool_item = g_object_new (GTH_TYPE_TOGGLE_MENU_TOOL_BUTTON,
				  "show-arrow", self->priv->show_arrow,
				  NULL);
	if (self->priv->show_menu_func != NULL)
		g_signal_connect (tool_item,
				  "show_menu",
				  G_CALLBACK (tool_item_show_menu_cb),
				  self);
	else if (self->priv->menu != NULL) {
		g_object_set (tool_item,
			      "menu", self->priv->menu,
			      "menu-halign", self->priv->menu_halign,
			      NULL);
	}

	return tool_item;
}


static void
gth_toggle_menu_action_finalize (GObject *base)
{
	GthToggleMenuAction *self = GTH_TOGGLE_MENU_ACTION (base);

	if (self->priv->show_menu_data_destroy != NULL)
		self->priv->show_menu_data_destroy (self->priv->show_menu_data);
	if (self->priv->menu != NULL)
		g_object_unref (self->priv->menu);

	G_OBJECT_CLASS (gth_toggle_menu_action_parent_class)->finalize (base);
}


static void
gth_toggle_menu_action_class_init (GthToggleMenuActionClass *klass)
{
	GObjectClass   *object_class;
	GtkActionClass *action_class;

	g_type_class_add_private (klass, sizeof (GthToggleMenuActionPrivate));

	object_class = (GObjectClass *) klass;
	object_class->set_property = gth_toggle_menu_action_set_property;
	object_class->get_property = gth_toggle_menu_action_get_property;
	object_class->finalize = gth_toggle_menu_action_finalize;

	action_class = (GtkActionClass *) klass;
	action_class->toolbar_item_type = GTH_TYPE_TOGGLE_MENU_TOOL_BUTTON;
	action_class->create_tool_item = gth_toggle_menu_action_create_tool_item;

	/* properties */

	g_object_class_install_property (object_class,
					 PROP_SHOW_ARROW,
					 g_param_spec_boolean ("show-arrow",
                                                               "Show Arrow",
                                                               "Whether to show an arrow",
                                                               TRUE,
                                                               G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_MENU,
					 g_param_spec_object ("menu",
                                                              "Menu",
                                                              "The menu to show",
                                                              GTK_TYPE_MENU,
                                                              G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_MENU_HALIGN,
					 g_param_spec_enum ("menu-halign",
							    "Menu HAlign",
							    "Menu Horizontal Alignment",
							    GTK_TYPE_ALIGN,
							    GTK_ALIGN_START,
							    G_PARAM_READWRITE));
}


void
gth_toggle_menu_action_set_show_menu_func (GthToggleMenuAction *self,
					   GthShowMenuFunc      func,
					   gpointer             data,
					   GDestroyNotify       destroy)
{
	self->priv->show_menu_func = func;
	if (self->priv->show_menu_data_destroy != NULL)
		self->priv->show_menu_data_destroy (self->priv->show_menu_data);
	self->priv->show_menu_data = data;
	self->priv->show_menu_data_destroy = destroy;
}


GtkWidget *
gth_toggle_menu_action_get_menu (GthToggleMenuAction *self)
{
	return self->priv->menu;
}


GtkAlign
gth_toggle_menu_action_get_menu_halign (GthToggleMenuAction *self)
{
	return self->priv->menu_halign;
}


gboolean
gth_toggle_menu_action_get_show_arrow (GthToggleMenuAction *self)
{
	return self->priv->show_arrow;
}
