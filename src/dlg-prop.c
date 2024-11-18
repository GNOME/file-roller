/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2004 Free Software Foundation, Inc.
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
#include <string.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include "gio-utils.h"
#include "glib-utils.h"
#include "file-utils.h"
#include "gtk-utils.h"
#include "fr-window.h"
#include "dlg-prop.h"

#define GET_WIDGET(x) (_gtk_builder_get_widget (data->builder, (x)))

typedef struct {
	FrWindow *window;
	GtkBuilder *builder;
	GtkWidget *dialog;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
	    DialogData *data)
{
	g_object_unref (G_OBJECT (data->builder));
	g_free (data);
}


static void
open_location_clicked_cb (GtkButton *button,
			  DialogData *data)
{
	_gtk_show_file_in_container (GTK_WINDOW (data->dialog), fr_window_get_archive_file (data->window), NULL, NULL);
}


void
dlg_prop (FrWindow *window)
{
	DialogData *data;

	data = g_new (DialogData, 1);
	data->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "properties.ui");
	data->window = window;

	/* Make the dialog */

	data->dialog = g_object_new (GTK_TYPE_DIALOG,
				     "title", _("Properties"),
				     "transient-for", GTK_WINDOW (window),
				     "modal", TRUE,
				     "use-header-bar", _gtk_settings_get_dialogs_use_header (),
				     NULL);
	gtk_window_set_default_size (GTK_WINDOW (data->dialog), 500, -1);
	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data->dialog))),
			_gtk_builder_get_widget (data->builder, "content"));

	GtkWidget *header_bar = gtk_dialog_get_header_bar (GTK_DIALOG (data->dialog));
	if (header_bar != NULL)
		gtk_widget_add_css_class (header_bar, "flat");

	/* Set widgets data. */

	// Icon

	GIcon *icon = g_content_type_get_icon (window->archive->mime_type);
	if (icon != NULL) {
		gtk_image_set_from_gicon (GTK_IMAGE (GET_WIDGET ("icon")), icon);
	}

	/* Name */

	char *utf8_text = _g_file_get_display_name (fr_window_get_archive_file (window));
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("filename_label")), utf8_text);

	g_free (utf8_text);

	/* Location. */

	GFile *parent = g_file_get_parent (fr_window_get_archive_file (window));
	char *uri = g_file_get_uri (parent);
	utf8_text = g_file_get_parse_name (parent);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("location_label")), utf8_text);

	g_free (utf8_text);
	g_free (uri);
	g_object_unref (parent);

	/* Mime type. */

	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("mime_type_label")), window->archive->mime_type);

	/* Date. */

	{
		g_autoptr (GDateTime) date_time;
		date_time = g_date_time_new_from_unix_local (_g_file_get_file_mtime (fr_window_get_archive_file (window)));
		utf8_text = g_date_time_format (date_time, _("%d %B %Y, %H:%M"));
		gtk_label_set_text (GTK_LABEL (GET_WIDGET ("date_label")), utf8_text);
		g_free (utf8_text);
	}

	/* Size */

	goffset size = _g_file_get_file_size (fr_window_get_archive_file (window));
	utf8_text = g_format_size_full (size, G_FORMAT_SIZE_LONG_FORMAT);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("size_label")), utf8_text);
	g_free (utf8_text);

	/* Uncompressed size. */

	goffset uncompressed_size = 0;
	if (fr_window_archive_is_present (window)) {
		for (guint i = 0; i < window->archive->files->len; i++) {
			FrFileData *fd = g_ptr_array_index (window->archive->files, i);
			uncompressed_size += fd->size;
		}
	}
	utf8_text = g_format_size_full (uncompressed_size, G_FORMAT_SIZE_LONG_FORMAT);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("uncompressed_size_label")), utf8_text);
	g_free (utf8_text);

	/* Compression ratio. */

	double ratio = (uncompressed_size != 0) ? (double) uncompressed_size / size : 0.0;
	utf8_text = g_strdup_printf ("%0.2f", ratio);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("compression_ratio_label")), utf8_text);
	g_free (utf8_text);

	/* Number of files. */

	utf8_text = g_strdup_printf ("%d", window->archive->n_regular_files);
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("n_files_label")), utf8_text);
	g_free (utf8_text);

	/* Set the signals handlers. */

	g_signal_connect (data->dialog,
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect (GET_WIDGET ("open_location_button"),
			  "clicked",
			  G_CALLBACK (open_location_clicked_cb),
			  data);

	/* Run dialog. */
	gtk_window_present (GTK_WINDOW (data->dialog));
}
