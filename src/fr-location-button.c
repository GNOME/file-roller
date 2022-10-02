/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2022 Free Software Foundation, Inc.
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
#include "gtk-utils.h"
#include "glib-utils.h"
#include "fr-location-button.h"


enum {
	PROP_0,
	PROP_TITLE,
};


enum {
	CHANGED,
	LAST_SIGNAL
};


static guint fr_location_button_signals[LAST_SIGNAL] = { 0 };


typedef struct {
	GFile *location;
	GtkWidget *label;
	GtkWidget *icon;
	GtkFileChooserNative *chooser;
	char *title;
} FrLocationButtonPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrLocationButton, fr_location_button, GTK_TYPE_BUTTON)


static void
fr_location_button_finalize (GObject *object)
{
	FrLocationButton *self = FR_LOCATION_BUTTON (object);
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	_g_object_unref (private->chooser);
	g_free (private->title);

	G_OBJECT_CLASS (fr_location_button_parent_class)->finalize (object);
}


static void
gth_location_button_set_property (GObject      *object,
				  guint         property_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
	FrLocationButton *self = FR_LOCATION_BUTTON (object);
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	switch (property_id) {
	case PROP_TITLE:
		g_free (private->title);
		private->title = g_value_dup_string (value);
		break;

	default:
		break;
	}
}


static void
gth_location_button_get_property (GObject    *object,
				  guint       property_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
	FrLocationButton *self = FR_LOCATION_BUTTON (object);
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	switch (property_id) {
	case PROP_TITLE:
		g_value_set_string (value, private->title);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}


static void
fr_location_button_class_init (FrLocationButtonClass *klass)
{
	GObjectClass *object_class;

	fr_location_button_parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass*) klass;
	object_class->set_property = gth_location_button_set_property;
	object_class->get_property = gth_location_button_get_property;
	object_class->finalize = fr_location_button_finalize;

	fr_location_button_signals[CHANGED] =
		g_signal_newv ("changed",
			       G_TYPE_FROM_CLASS (klass),
			       G_SIGNAL_RUN_LAST,
			       /* class_closure = */ NULL,
			       NULL, NULL,
			       g_cclosure_marshal_VOID__VOID,
			       G_TYPE_NONE,
			       0, NULL);

	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "Title",
							      "The file chooser title",
							      NULL,
							      G_PARAM_READWRITE));

}


static void
location_changed (FrLocationButton *self) {
	g_signal_emit (self, fr_location_button_signals[CHANGED], 0, NULL);
}


static void
file_chooser_response_cb (GtkNativeDialog *native,
			  int              response,
			  gpointer         user_data)
{
	FrLocationButton *self = user_data;
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	if (response == GTK_RESPONSE_ACCEPT) {
		GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (private->chooser));
		if (file != NULL) {
			fr_location_button_set_location (self, file);
			location_changed (self);
			g_object_unref (file);
		}
	}

	g_object_unref (private->chooser);
	private->chooser = NULL;
}


static void
clicked_cb (GtkButton *button,
	    gpointer   user_data)
{
	FrLocationButton *self = user_data;
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	if (private->chooser != NULL)
		return;

	private->chooser = gtk_file_chooser_native_new (private->title,
							GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))),
							GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
							_GTK_LABEL_OPEN,
							_GTK_LABEL_CANCEL);
	gtk_native_dialog_set_modal (GTK_NATIVE_DIALOG (private->chooser), TRUE);
	if (private->location != NULL)
		gtk_file_chooser_set_file (GTK_FILE_CHOOSER (private->chooser), private->location, NULL);

	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_add_mime_type (filter, "inode/directory");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (private->chooser), filter);

	g_signal_connect (private->chooser,
			  "response",
			  G_CALLBACK (file_chooser_response_cb),
			  self);
	gtk_native_dialog_show (GTK_NATIVE_DIALOG (private->chooser));
}


static void
fr_location_button_init (FrLocationButton *self)
{
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	private->chooser = NULL;
	private->title = NULL;

	gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);

	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_button_set_child (GTK_BUTTON (self), box);

	private->icon = gtk_image_new ();
	gtk_box_append (GTK_BOX (box), private->icon);

	private->label = gtk_label_new (private->title);
	gtk_box_append (GTK_BOX (box), private->label);

	g_signal_connect (self,
			  "clicked",
			  G_CALLBACK (clicked_cb),
			  self);
}


GtkWidget *
fr_location_button_new (const char *title)
{
	return g_object_new (FR_TYPE_LOCATION_BUTTON,
			     "title", title,
			     NULL);
}


GFile *
fr_location_button_get_location (FrLocationButton *self)
{
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);
	return _g_object_ref (private->location);
}


void
fr_location_button_set_location (FrLocationButton *self,
				 GFile            *location)
{
	FrLocationButtonPrivate *private = fr_location_button_get_instance_private (self);

	_g_object_unref (private->location);
	private->location = g_object_ref (location);

	GFileInfo *info = g_file_query_info (private->location,
					     "standard::display-name,standard::icon,standard::symbolic-icon",
					     G_FILE_QUERY_INFO_NONE,
					     NULL,
					     NULL);
	if (info != NULL) {
		gtk_label_set_text (GTK_LABEL (private->label), g_file_info_get_display_name (info));
		gtk_image_set_from_gicon (GTK_IMAGE (private->icon), g_file_info_get_symbolic_icon (info));
		g_object_unref (info);
	}
	else {
		char *name = g_file_get_uri (private->location);
		gtk_label_set_text (GTK_LABEL (private->label), name);
		gtk_image_set_from_icon_name (GTK_IMAGE (private->icon), "folder-symbolic");
		g_free (name);
	}
}
