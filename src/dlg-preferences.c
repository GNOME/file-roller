/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <libgnomeui/gnome-pixmap.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libgnome/gnome-config.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <glade/glade.h>

#include "main.h"
#include "window.h"
#include "gconf-utils.h"


#define GLADE_FILE "file_roller_prop.glade"


typedef struct {
	FRWindow  *window;

	GladeXML  *gui;
	GtkWidget *dialog;

	GtkWidget *show_hide_column[NUMBER_OF_COLUMNS]; /* checkbuttons */
	GtkWidget *history_len_spinbutton;
	GtkWidget *compression_optionmenu;
} DialogData;


static guint
opt_menu_get_active_idx (GtkWidget *opt_menu)
{
        GtkWidget *item;
        guint      idx;
        GList     *scan;
        GtkWidget *menu;

        menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (opt_menu));
        item = gtk_menu_get_active (GTK_MENU (menu));

        idx = 0;
        scan = GTK_MENU_SHELL (menu)->children;
        while (scan && (item != scan->data)) {
                idx++;
                scan = scan->next;
        }

        return idx;
}


/* called when the "apply" button is clicked. */
static void
apply_cb (GtkWidget  *widget, 
	  DialogData *data)
{
	/* Show/Hide Columns options. */

	eel_gconf_set_boolean (PREF_LIST_SHOW_TYPE, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_TYPE])));
	eel_gconf_set_boolean (PREF_LIST_SHOW_SIZE, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_SIZE])));
	eel_gconf_set_boolean (PREF_LIST_SHOW_TIME, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_TIME])));
	eel_gconf_set_boolean (PREF_LIST_SHOW_PATH, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_PATH])));

	/* Misc options. */

	eel_gconf_set_integer (PREF_UI_HISTORY_LEN, gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (data->history_len_spinbutton)));
	preferences_set_compression_level (opt_menu_get_active_idx (data->compression_optionmenu));
}


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget, 
	    DialogData *data)
{
	apply_cb (widget, data);
	g_object_unref (G_OBJECT (data->gui));
	g_free (data);
}


/* called when the "help" button is clicked. */
static void
help_clicked_cb (GtkWidget  *widget, 
		 DialogData *data)
{
	GError *err;

	err = NULL;  
	gnome_help_display ("file-roller", "fr-settings", &err);
	
	if (err != NULL) {
		GtkWidget *dialog;
		
		dialog = gtk_message_dialog_new (GTK_WINDOW (data->dialog),
						 0,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Could not display help: %s"),
						 err->message);
		
		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);
		
		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
		
		gtk_widget_show (dialog);
		
		g_error_free (err);
	}
}


/* create the main dialog. */
void
dlg_preferences (GtkWidget *caller, 
		 FRWindow  *window)
{
	DialogData *data;
	GtkWidget  *btn_close;
	GtkWidget  *help_button;
	GtkWidget  *label;
	char       *label_text;
	int         i;

	data = g_new0 (DialogData, 1);

	data->window = window;

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
        if (!data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
		g_free (data);
                return;
        }

	/* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "options_dialog");

	data->show_hide_column[COLUMN_TYPE] = glade_xml_get_widget (data->gui, "type_checkbutton");
	data->show_hide_column[COLUMN_SIZE] = glade_xml_get_widget (data->gui, "size_checkbutton");
	data->show_hide_column[COLUMN_TIME] = glade_xml_get_widget (data->gui, "time_checkbutton");
	data->show_hide_column[COLUMN_PATH] = glade_xml_get_widget (data->gui, "path_checkbutton");

        data->history_len_spinbutton = glade_xml_get_widget (data->gui, "history_len_spinbutton");
	data->compression_optionmenu = glade_xml_get_widget (data->gui, "compression_optionmenu");

        btn_close = glade_xml_get_widget (data->gui, "p_close_button");
	help_button = glade_xml_get_widget (data->gui, "p_help_button");

	/* Set widgets data. */

	label = glade_xml_get_widget (data->gui, "general_label");
	label_text = g_strdup_printf ("<b>%s</b>", _("General"));
	gtk_label_set_markup (GTK_LABEL (label), label_text);
	g_free (label_text);

	label = glade_xml_get_widget (data->gui, "columns_to_show_label");
	label_text = g_strdup_printf ("<b>%s</b>", _("Visible columns"));
	gtk_label_set_markup (GTK_LABEL (label), label_text);
	g_free (label_text);

	/**/

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_TYPE]), eel_gconf_get_boolean (PREF_LIST_SHOW_TYPE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_SIZE]), eel_gconf_get_boolean (PREF_LIST_SHOW_SIZE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_TIME]), eel_gconf_get_boolean (PREF_LIST_SHOW_TIME));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_hide_column[COLUMN_PATH]), eel_gconf_get_boolean (PREF_LIST_SHOW_PATH));

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->history_len_spinbutton), (gfloat) eel_gconf_get_integer (PREF_UI_HISTORY_LEN));

	gtk_option_menu_set_history (GTK_OPTION_MENU (data->compression_optionmenu), preferences_get_compression_level ());

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	
	g_signal_connect_swapped (G_OBJECT (btn_close), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (help_button), 
			  "clicked",
			  G_CALLBACK (help_clicked_cb),
			  data);
	
	for (i = 0; i < NUMBER_OF_COLUMNS; i++)
		if (data->show_hide_column[i] != NULL)
			g_signal_connect (G_OBJECT (data->show_hide_column[i]),
					  "toggled",
					  G_CALLBACK (apply_cb),
					  data);

	/* run dialog. */

	gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
	gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);
	gtk_widget_show_all (data->dialog);
}
