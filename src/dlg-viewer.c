/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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
#include <gconf/gconf-client.h>
#include <libgnomeui/gnome-file-entry.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "file-utils.h"
#include "gtk-utils.h"
#include "window.h"
#include "gnome-vfs-helpers.h"


#define GLADE_FILE "file_roller.glade"


typedef struct {
	GladeXML  *gui;
	FRWindow  *window;
	GtkWidget *dialog;
	GtkWidget *pw_password_entry;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	g_object_unref (data->gui);
        g_free (data);
}


static void
close_clicked_cb (GtkWidget  *widget,
		  DialogData *data)
{
	gtk_widget_destroy (data->dialog);
}


static void
load_document (GtkTextBuffer *text_buf, 
	       const char    *filename)
{  
	char           *file_contents;
        GnomeVFSResult  res;
        gsize           file_size;
        GtkTextIter     iter;
	char           *uri;

	uri = g_strconcat ("file://", filename, NULL);
	res = gnome_vfs_x_read_entire_file (uri, &file_size, &file_contents);
	g_free (uri);

	if (res != GNOME_VFS_OK) {
		/* FIXME : popup a dialog. */
		return;
	}
	
	if (file_size > 0) {
		if (!g_utf8_validate (file_contents, file_size, NULL)) {
			/* The file contains invalid UTF8 data.
			 * Try to convert it to UTF-8 from currence locale */
			GError *conv_error = NULL;
			char   *converted_file_contents = NULL;
			gsize   bytes_written;
                        
			converted_file_contents = g_locale_to_utf8 (file_contents, file_size, NULL, &bytes_written, &conv_error); 
                        
                        g_free (file_contents);

                        if ((conv_error != NULL) || ! g_utf8_validate (converted_file_contents, bytes_written, NULL)) {
                                /* Coversion failed */
                                if (conv_error != NULL)
                                        g_error_free (conv_error);

				/* FIXME  : popup a dialog.
				 * g_print ("Invalid UTF-8 data\n"); 
				 */

                                if (converted_file_contents != NULL)
                                        g_free (converted_file_contents);

				return;
                        } 

			file_contents = converted_file_contents;
			file_size = bytes_written;
                }
		
                /* Insert text in the buffer */
                gtk_text_buffer_get_iter_at_offset (text_buf, &iter, 0);
                gtk_text_buffer_insert (text_buf, &iter, file_contents, file_size);

                /* Place the cursor at the start of the document */
                gtk_text_buffer_get_iter_at_offset (text_buf, &iter, 0);
                gtk_text_buffer_place_cursor (text_buf, &iter);
        }

        g_free (file_contents);
}


/* From nautilus-text-view.c 
 *   Copyright (C) 2000 Eazel, Inc.
 *   Copyright (C) 2002 Sun Microsystems Inc.  */
static void
set_monospace_font (GtkWidget *widget)
{
        GConfClient *conf_client;
        char        *monospace_font;

	/* Pick up the monospace font from desktop preferences */

        conf_client = gconf_client_get_default ();
        monospace_font = gconf_client_get_string (conf_client, "/desktop/gnome/interface/monospace_font_name", NULL);

        if (monospace_font != NULL) {
		PangoFontDescription *monospace_font_desc;

                monospace_font_desc = pango_font_description_from_string (monospace_font);
                gtk_widget_modify_font (widget, monospace_font_desc);
                pango_font_description_free (monospace_font_desc);

		g_free (monospace_font);
        }

        g_object_unref (conf_client);
}


void
dlg_viewer (FRWindow   *window,
	    const char *filename)
{
	DialogData    *data;
	GtkWidget     *text_view;
	GtkWidget     *close_button;
	GtkTextBuffer *text_buf;
	char          *utf8_text;

        data = g_new (DialogData, 1);
	data->window = window;
	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "viewer_dialog");
	text_view = glade_xml_get_widget (data->gui, "v_viewer_textview");
	close_button = glade_xml_get_widget (data->gui, "v_close_button");

	/* Set widgets data. */

	set_monospace_font (text_view);

	text_buf = gtk_text_buffer_new (NULL);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (text_view), text_buf);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
	g_object_unref (text_buf);

	utf8_text = g_filename_to_utf8 (file_name_from_path (filename), -1, 0, 0, 0);
	gtk_window_set_title (GTK_WINDOW (data->dialog), utf8_text);
	g_free (utf8_text);

	load_document (text_buf, filename);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);

	g_signal_connect (G_OBJECT (close_button),
			  "clicked",
			  G_CALLBACK (close_clicked_cb),
			  data);

	/* Run dialog. */

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
	gtk_widget_show (data->dialog);
}
