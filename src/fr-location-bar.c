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
#include "fr-location-bar.h"


enum {
	CHANGED,
	LAST_SIGNAL
};


static guint fr_location_bar_signals[LAST_SIGNAL] = { 0 };


typedef struct {
	GFile *location;
	GtkWidget *location_entry;
	GtkWidget *previous_location_button;
	GtkWidget *next_location_button;
	GtkWidget *parent_location_button;
	GList *history;
	GList *history_current;
} FrLocationBarPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrLocationBar, fr_location_bar, GTK_TYPE_BOX)


static void
fr_location_bar_finalize (GObject *object)
{
	FrLocationBar *self = FR_LOCATION_BAR (object);
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	_g_object_list_unref (private->history);

	G_OBJECT_CLASS (fr_location_bar_parent_class)->finalize (object);
}


static void
fr_location_bar_class_init (FrLocationBarClass *klass)
{
	GObjectClass *object_class;

	fr_location_bar_parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_location_bar_finalize;

	fr_location_bar_signals[CHANGED] =
		g_signal_newv ("changed",
			       G_TYPE_FROM_CLASS (klass),
			       G_SIGNAL_RUN_LAST,
			       /* class_closure = */ NULL,
			       NULL, NULL,
			       g_cclosure_marshal_VOID__VOID,
			       G_TYPE_NONE,
			       0, NULL);
}


static void
location_changed (FrLocationBar *self) {
	g_signal_emit (self, fr_location_bar_signals[CHANGED], 0, NULL);
}


static void
update_navigation_sensitivity (FrLocationBar *self)
{
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);
	gtk_widget_set_sensitive (private->previous_location_button,
				  (private->history != NULL)
				  && (private->history_current != NULL)
				  && (private->history_current->next != NULL));
	gtk_widget_set_sensitive (private->next_location_button,
				  (private->history != NULL)
				  && (private->history_current != NULL)
				  && (private->history_current->prev != NULL));

	GFile *parent = (private->location != NULL) ? g_file_get_parent (private->location) : NULL;
	gtk_widget_set_sensitive (private->parent_location_button, parent != NULL);
	_g_object_unref (parent);
}


static gboolean
previous_location_button_clicked_cb (GtkButton *button,
				     gpointer   user_data)
{
	FrLocationBar *self = user_data;
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	if (private->history == NULL)
		return TRUE;
	if (private->history_current == NULL)
		return TRUE;
	if (private->history_current->next == NULL)
		return TRUE;

	private->history_current = private->history_current->next;

	_g_object_unref (private->location);
	private->location = g_object_ref (G_FILE (private->history_current->data));
	location_changed (self);

	return TRUE;
}


static gboolean
next_location_button_clicked_cb (GtkButton *button,
				 gpointer   user_data)
{
	FrLocationBar *self = user_data;
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	if (private->history == NULL)
		return TRUE;
	if (private->history_current == NULL)
		return TRUE;
	if (private->history_current->prev == NULL)
		return TRUE;

	private->history_current = private->history_current->prev;

	_g_object_unref (private->location);
	private->location = g_object_ref (G_FILE (private->history_current->data));
	location_changed (self);

	return TRUE;
}


static gboolean
parent_location_button_clicked_cb (GtkButton *button,
				   gpointer   user_data)
{
	FrLocationBar *self = user_data;
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);
	GFile *parent;

	parent = g_file_get_parent (private->location);
	if (parent == NULL)
		return TRUE;

	_g_object_unref (private->location);
	private->location = parent;
	location_changed (self);

	return TRUE;
}


static gboolean
location_entry_activate_cb (GtkEntry *entry,
			    gpointer  user_data)
{
	FrLocationBar *self = user_data;
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	_g_object_unref (private->location);
	private->location = g_file_parse_name (gtk_editable_get_text (GTK_EDITABLE (entry)));
	location_changed (self);

	return FALSE;
}


static void
fr_location_bar_init (FrLocationBar *self)
{
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	private->location = NULL;
	private->history = NULL;
	private->history_current = NULL;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
	gtk_box_set_spacing (GTK_BOX (self), 6);

	GtkWidget *navigation_commands = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *button;

	private->previous_location_button = button = gtk_button_new_from_icon_name ("go-previous-symbolic");
	gtk_widget_set_tooltip_text (button, _("Previous visited location"));
	gtk_box_append (GTK_BOX (navigation_commands), button);
	g_signal_connect (button,
			  "clicked",
			  G_CALLBACK (previous_location_button_clicked_cb),
			  self);

	private->next_location_button = button = gtk_button_new_from_icon_name ("go-next-symbolic");
	gtk_widget_set_tooltip_text (button, _("Next visited location"));
	gtk_box_append (GTK_BOX (navigation_commands), button);
	g_signal_connect (button,
			  "clicked",
			  G_CALLBACK (next_location_button_clicked_cb),
			  self);

	private->parent_location_button = button = gtk_button_new_from_icon_name ("go-up-symbolic");
	gtk_widget_set_tooltip_text (button, _("Parent location"));
	gtk_box_append (GTK_BOX (navigation_commands), button);
	g_signal_connect (button,
			  "clicked",
			  G_CALLBACK (parent_location_button_clicked_cb),
			  self);

	GtkWidget *location_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	/* Translators: after the colon there is a folder name. */
	GtkWidget *location_label = gtk_label_new_with_mnemonic (_("_Location:"));
	gtk_widget_set_margin_start (location_label, 5);
	gtk_widget_set_margin_end (location_label, 5);
	gtk_box_append (GTK_BOX (location_box), location_label);

	private->location_entry = gtk_entry_new ();
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (private->location_entry),
					   GTK_ENTRY_ICON_PRIMARY,
					   "folder-symbolic");
	g_signal_connect (private->location_entry,
			  "activate",
			  G_CALLBACK (location_entry_activate_cb),
			  self);
	gtk_widget_set_margin_start (private->location_entry, 5);
	gtk_widget_set_margin_end (private->location_entry, 5);
	_gtk_box_pack_end (GTK_BOX (location_box), private->location_entry, TRUE, FALSE);
	gtk_label_set_mnemonic_widget (GTK_LABEL (location_label), private->location_entry);

	/* Pack widgets */

	_gtk_box_pack_end (GTK_BOX (self), location_box, TRUE, FALSE);
	gtk_box_append (GTK_BOX (self), navigation_commands);
}


GtkWidget *
fr_location_bar_new (void)
{
	return g_object_new (FR_TYPE_LOCATION_BAR, NULL);
}


GFile *
fr_location_bar_get_location (FrLocationBar *self)
{
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);
	return private->location;
}


static void
fr_location_bar_history_add (FrLocationBar *self,
			     GFile         *location)
{
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	if ((private->history_current == NULL) || !g_file_equal (location, (GFile *) private->history_current->data)) {
		GList *scan;
		GList *new_current = NULL;

		/* Search the location in the history. */
		for (scan = private->history_current; scan; scan = scan->next) {
			GFile *location_in_history = scan->data;

			if (g_file_equal (location, location_in_history)) {
				new_current = scan;
				break;
			}
		}

		if (new_current != NULL) {
			private->history_current = new_current;
		}
		else {
			/* Remove all the location after the current position. */
			for (scan = private->history; scan && (scan != private->history_current); /* void */) {
				GList *next = scan->next;

				private->history = g_list_remove_link (private->history, scan);
				_g_object_list_unref (scan);

				scan = next;
			}

			private->history = g_list_prepend (private->history, g_object_ref (location));
			private->history_current = private->history;
		}
	}
	update_navigation_sensitivity (self);
}


void
fr_location_bar_set_location (FrLocationBar *self,
			      GFile         *location)
{
	FrLocationBarPrivate *private = fr_location_bar_get_instance_private (self);

	_g_object_unref (private->location);
	private->location = g_object_ref (location);

	char *name = g_file_get_parse_name (private->location);
	gtk_editable_set_text (GTK_EDITABLE (private->location_entry), name);
	g_free (name);

	fr_location_bar_history_add (self, private->location);
}
