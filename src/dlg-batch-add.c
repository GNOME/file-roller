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
#include <math.h>

#include <gnome.h>
#include <glade/glade.h>

#include "file-utils.h"
#include "fr-stock.h"
#include "window.h"
#include "typedefs.h"
#include "gtk-utils.h"


#define GLADE_FILE "file_roller.glade"
#define UPDATE_DROPPED_FILES (FALSE)
#define ARCHIVE_MIME_TYPE ("application/x-compressed-tar")
#define ARCHIVE_ICON_SIZE (48)


typedef struct {
	FRWindow  *window;
	GladeXML  *gui;

	GtkWidget *dialog;
	GtkWidget *a_add_to_entry;

	GList     *file_list;
	gboolean   add_clicked;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
            DialogData *data)
{
	if (! data->add_clicked) {
		window_pop_message (data->window);
		window_batch_mode_stop (data->window);
	}

	g_object_unref (data->gui);
        g_free (data);
}


/* called when the "add" button is pressed. */
static void
add_clicked_cb (GtkWidget  *widget, 
		DialogData *data)
{
	FRWindow *window = data->window; 
	char     *archive_name;
	char     *archive_dir;
	char     *archive_file;
	gboolean  do_not_add = FALSE;

	data->add_clicked = TRUE;

	/* Collect data */

	window->update_dropped_files = UPDATE_DROPPED_FILES;
	archive_name = g_filename_from_utf8 (gtk_entry_get_text (GTK_ENTRY (data->a_add_to_entry)), -1, NULL, NULL, NULL);

	/* check whether the user entered an archive name. */

	if (path_is_dir (archive_name)) {
		GtkWidget  *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not zip objects"),
					     _("You have to specify an archive name."),
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);

		return;
	}

	/* if the user do not specify an extension use tgz as default */
	if (strchr (archive_name, '.') == NULL) {
		char *new_archive_name;
		new_archive_name = g_strconcat (archive_name, ".tgz", NULL);
		g_free (archive_name);
		archive_name = new_archive_name;
	}

	if (! fr_archive_utils__file_is_archive (archive_name)) {
		GtkWidget  *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not zip objects"),
					     _("Archive type not supported."),
					     GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);

		return;
	}

	/* check directory existence. */
	archive_dir = remove_level_from_path ((char*) data->file_list->data);
	if (! path_is_dir (archive_dir)) {
		GtkWidget *d;
		int        r;
		
		d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("Destination folder does not exist.  Do you want to create it?"),
					     NULL,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Create _Folder"), GTK_RESPONSE_YES,
					     NULL);
			
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
	}

	if (! do_not_add && ! ensure_dir_exists (archive_dir, 0755)) {
		GtkWidget  *d;
		const char *error;
		char       *message;

		error = gnome_vfs_result_to_string (gnome_vfs_result_from_errno ());
		message = g_strdup_printf (_("Could not create the destination folder: %s."), error);
		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not zip objects"),
					     message,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		g_free (message);

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_dir);
		g_free (archive_name);
		return;
	}

	if (do_not_add) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Zip not performed"),
					     NULL,
					     GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_run (GTK_DIALOG (d));
		g_free (archive_dir);
		g_free (archive_name);
		gtk_widget_destroy (GTK_WIDGET (d));
		return;
	}

	archive_file = g_build_filename (archive_dir, archive_name, NULL);

	if (! path_is_file (archive_file)) {
		if (window->dropped_file_list != NULL)
			path_list_free (window->dropped_file_list);
		window->dropped_file_list = path_list_dup (data->file_list);
		window->add_after_creation = TRUE;
		window_archive_new (window, archive_file);

	} else {
		window->add_after_opening = TRUE;

		window_batch_mode_add_next_action (window,
						   FR_BATCH_ACTION_ADD,
						   path_list_dup (data->file_list),
						   (GFreeFunc) path_list_free);
		window_archive_open (window, archive_file, GTK_WINDOW (window->app));
	}

	g_free (archive_name);
	g_free (archive_dir);
	g_free (archive_file);

	gtk_widget_destroy (data->dialog);
}


/* taken from egg-recent-util.c */
static GdkPixbuf *
scale_icon (GdkPixbuf *pixbuf,
	    double    *scale)
{
	guint width, height;
	
	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	
	width = floor (width * *scale + 0.5);
	height = floor (height * *scale + 0.5);
	
        return gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
}


/* taken from egg-recent-util.c */
static GdkPixbuf *
load_icon_file (char          *filename,
		guint          base_size,
		guint          nominal_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
        guint      width, height, size;
        double     scale;
	
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	
	if (pixbuf == NULL) {
		return NULL;
	}

	if (base_size == 0) {
		width = gdk_pixbuf_get_width (pixbuf);
		height = gdk_pixbuf_get_height (pixbuf);
		size = MAX (width, height);
		if (size > nominal_size) {
			base_size = size;
		} else {
			/* Don't scale up small icons */
			base_size = nominal_size;
		}

	} 

	if (base_size != nominal_size) {
		scale = (double) nominal_size / base_size;
		scaled_pixbuf = scale_icon (pixbuf, &scale);
		g_object_unref (pixbuf);
		pixbuf = scaled_pixbuf;
	}

        return pixbuf;
}


static GdkPixbuf *
get_pixbuf_from_mime_type (const char *mime_type,
			   int         icon_size)
{
	GnomeIconTheme *icon_theme;
	char           *icon_name = NULL;
	GdkPixbuf      *icon = NULL;

	icon_theme = gnome_icon_theme_new ();

	if (icon_theme == NULL)
		return NULL;

	gnome_icon_theme_set_allow_svg (icon_theme, TRUE);

	icon_name = gnome_icon_lookup (icon_theme,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       mime_type,
				       GNOME_ICON_LOOKUP_FLAGS_NONE,
				       NULL);

	if (icon_name != NULL) {
		char                *icon_path = NULL;
		const GnomeIconData *icon_data;
		int                  base_size;

		icon_path = gnome_icon_theme_lookup_icon (icon_theme, 
							  icon_name,
							  icon_size,
							  &icon_data,
							  &base_size);

		if (icon_path != NULL) {
			icon = load_icon_file (icon_path, 
					       base_size, 
					       icon_size);
			g_free (icon_path);
		}
	}

	g_free (icon_name);
	g_object_unref (icon_theme);
	
	return icon;
}


void
dlg_batch_add_files (FRWindow *window,
		     GList    *file_list)
{
        DialogData *data;
	GtkWidget  *cancel_button;
	GtkWidget  *add_button;
	GtkWidget  *add_image;
	char       *path;

        data = g_new (DialogData, 1);

        data->window = window;
	data->file_list = file_list;
	data->add_clicked = FALSE;

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (!data->gui) {
                g_warning ("Could not find " GLADE_FILE "\n");
                return;
        }

        /* Get the widgets. */

        data->dialog = glade_xml_get_widget (data->gui, "batch_add_files_dialog");
	data->a_add_to_entry = glade_xml_get_widget (data->gui, "a_add_to_entry");

	add_button = glade_xml_get_widget (data->gui, "a_add_button");
	cancel_button = glade_xml_get_widget (data->gui, "a_cancel_button");

	add_image = glade_xml_get_widget (data->gui, "a_add_image");
	gtk_image_set_from_pixbuf (GTK_IMAGE (add_image), get_pixbuf_from_mime_type (ARCHIVE_MIME_TYPE, ARCHIVE_ICON_SIZE));

	/* Set widgets data. */

	path = g_strconcat (file_name_from_path ((char*) file_list->data), ".tgz", NULL);
	_gtk_entry_set_filename_text (GTK_ENTRY (data->a_add_to_entry), path);
	g_free (path);

	/* Set the signals handlers. */

        g_signal_connect (G_OBJECT (data->dialog), 
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (cancel_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (add_button), 
			  "clicked",
			  G_CALLBACK (add_clicked_cb),
			  data);

	/* Run dialog. */

	gtk_widget_grab_focus (data->a_add_to_entry);
	gtk_editable_set_position  (GTK_EDITABLE (data->a_add_to_entry), 
				    strlen (gtk_entry_get_text (GTK_ENTRY (data->a_add_to_entry))));

        gtk_window_set_transient_for (GTK_WINDOW (data->dialog), 
				      GTK_WINDOW (window->app));
	gtk_window_set_modal (GTK_WINDOW (data->dialog), FALSE); 

	gtk_widget_show (data->dialog);
}
