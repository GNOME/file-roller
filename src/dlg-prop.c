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
#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomeui/gnome-file-entry.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "file-utils.h"
#include "window.h"


#define PROP_GLADE_FILE "file_roller.glade2"


typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	g_object_unref (G_OBJECT (data->gui));
        g_free (data);
}


void
dlg_prop (GtkWidget *widget,
	  gpointer   callback_data)
{
        DialogData       *data;
	FRWindow         *window = callback_data;
	GtkWidget        *ok_button;
	GtkWidget        *label;
	char             *s;
	const char       *s1;
	GnomeVFSFileSize  size;
	time_t            t;
	gchar            *utf8_name;

        data = g_new (DialogData, 1);

	data->gui = glade_xml_new (GLADEDIR "/" PROP_GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " PROP_GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "prop_dialog");
	ok_button = glade_xml_get_widget (data->gui, "p_ok_button");

	/* Set widgets data. */
	
	label = glade_xml_get_widget (data->gui, "p_path_label");
	/* window->archive_filename is unescaped. */
	s = remove_level_from_path (window->archive_filename); 
	utf8_name = g_locale_to_utf8 (s, -1, NULL, NULL, NULL);
	gtk_label_set_text (GTK_LABEL (label), utf8_name);
	g_free (utf8_name);
	g_free (s);

	label = glade_xml_get_widget (data->gui, "p_name_label");
	s1 = file_name_from_path (window->archive_filename);
	utf8_name = g_locale_to_utf8 (s1, -1, NULL, NULL, NULL);
	gtk_label_set_text (GTK_LABEL (label), utf8_name);
	g_free (utf8_name);

	label = glade_xml_get_widget (data->gui, "p_size_label");
	size = get_file_size (window->archive_filename);
	s = gnome_vfs_format_file_size_for_display (size);
	gtk_label_set_text (GTK_LABEL (label), s);
	g_free (s);

	label = glade_xml_get_widget (data->gui, "p_files_label");
	s = g_strdup_printf ("%d", g_list_length (window->archive->command->file_list));
	gtk_label_set_text (GTK_LABEL (label), s);
	g_free (s);

	label = glade_xml_get_widget (data->gui, "p_date_label");
	t = get_file_mtime (window->archive->filename);
	s = g_strdup (ctime (&t));
	s[strlen (s) - 1] = 0;
	gtk_label_set_text (GTK_LABEL (label), s);
	g_free (s);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (ok_button), 
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
        gtk_window_set_modal         (GTK_WINDOW (data->dialog), TRUE);

	gtk_widget_show (data->dialog);
}
