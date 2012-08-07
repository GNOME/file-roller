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

#include <config.h>
#include "fr-file-selector-dialog.h"
#include "gtk-utils.h"
#include "glib-utils.h"


#define GET_WIDGET(x) (_gtk_builder_get_widget (self->priv->builder, (x)))


struct _FrFileSelectorDialogPrivate {
	GtkBuilder *builder;
	GtkWidget  *extra_widget;
	GFile      *current_folder;
};


G_DEFINE_TYPE (FrFileSelectorDialog, fr_file_selector_dialog, GTK_TYPE_DIALOG)


static void
fr_file_selector_dialog_finalize (GObject *object)
{
	FrFileSelectorDialog *self;

	self = FR_FILE_SELECTOR_DIALOG (object);
	g_object_unref (self->priv->builder);
	_g_object_unref (self->priv->current_folder);

	G_OBJECT_CLASS (fr_file_selector_dialog_parent_class)->finalize (object);
}


static void
fr_file_selector_dialog_class_init (FrFileSelectorDialogClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (FrFileSelectorDialogPrivate));

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_file_selector_dialog_finalize;
}


static void
fr_file_selector_dialog_init (FrFileSelectorDialog *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_FILE_SELECTOR_DIALOG, FrFileSelectorDialogPrivate);
	self->priv->current_folder = NULL;
	self->priv->builder = _gtk_builder_new_from_resource ("file-selector.ui");

	gtk_container_set_border_width (GTK_CONTAINER (self), 5);
	gtk_window_set_default_size (GTK_WINDOW (self), 830, 510); /* FIXME: find a good size */
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"));
}


GtkWidget *
fr_file_selector_dialog_new (const char *title,
			     GtkWindow  *parent)
{
	return (GtkWidget *) g_object_new (FR_TYPE_FILE_SELECTOR_DIALOG,
					   "title", title,
					   "transient-for", parent,
					   NULL);
}


void
fr_file_selector_dialog_set_extra_widget (FrFileSelectorDialog *self,
					  GtkWidget            *extra_widget)
{
	if (self->priv->extra_widget != NULL)
		gtk_container_remove (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->priv->extra_widget);
	self->priv->extra_widget = extra_widget;
	gtk_container_add (GTK_CONTAINER (GET_WIDGET ("extra_widget_container")), self->priv->extra_widget);
}


GtkWidget *
fr_file_selector_dialog_get_extra_widget (FrFileSelectorDialog *self)
{
	return self->priv->extra_widget;
}


void
fr_file_selector_dialog_set_current_folder (FrFileSelectorDialog *dialog,
					    GFile                *folder)
{

}


GFile *
fr_file_selector_dialog_get_current_folder (FrFileSelectorDialog *dialog)
{
	return NULL;
}


void
fr_file_selector_dialog_set_selected_files (FrFileSelectorDialog  *dialog,
					    GList                 *files)
{

}


GList *
fr_file_selector_dialog_get_selected_files (FrFileSelectorDialog *dialog)
{
	return NULL;
}
