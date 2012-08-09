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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include "gtk-utils.h"

#define LOAD_BUFFER_SIZE 65536
#define FILE_ROLLER_RESOURCE_UI_PATH "/org/gnome/FileRoller/ui/"


static void
count_selected (GtkTreeModel *model,
		GtkTreePath  *path,
		GtkTreeIter  *iter,
		gpointer      data)
{
	int *n = data;
	*n = *n + 1;
}


int
_gtk_tree_selection_count_selected (GtkTreeSelection *selection)
{
	int n = 0;

	if (selection == NULL)
		return 0;

	gtk_tree_selection_selected_foreach (selection, count_selected, &n);
	return n;
}


GtkWidget *
_gtk_message_dialog_new (GtkWindow      *parent,
			 GtkDialogFlags  flags,
			 const char     *stock_id,
			 const char     *message,
			 const char     *secondary_message,
			 const gchar    *first_button_text,
			 ...)
{
	GtkBuilder  *builder;
	GtkWidget   *dialog;
	GtkWidget   *label;
	va_list      args;
	const gchar *text;
	int          response_id;
	char        *markup_text;

	builder = _gtk_builder_new_from_resource ("message-dialog.ui");
	dialog = _gtk_builder_get_widget (builder, "message_dialog");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), (flags & GTK_DIALOG_MODAL));
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	g_object_weak_ref (G_OBJECT (dialog), (GWeakNotify) g_object_unref, builder);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	/* set the icon */

	gtk_image_set_from_stock (GTK_IMAGE (_gtk_builder_get_widget (builder, "icon_image")),
				  stock_id,
				  GTK_ICON_SIZE_DIALOG);

	/* set the message */

	label = _gtk_builder_get_widget (builder, "message_label");

	if (message != NULL) {
		char *escaped_message;

		escaped_message = g_markup_escape_text (message, -1);
		if (secondary_message != NULL) {
			char *escaped_secondary_message;

			escaped_secondary_message = g_markup_escape_text (secondary_message, -1);
			markup_text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
						       escaped_message,
						       escaped_secondary_message);

			g_free (escaped_secondary_message);
		}
		else
			markup_text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>", escaped_message);

		g_free (escaped_message);
	}
	else
		markup_text = g_markup_escape_text (secondary_message, -1);

	gtk_label_set_markup (GTK_LABEL (label), markup_text);
	g_free (markup_text);

	/* add the buttons */

	if (first_button_text == NULL)
		return dialog;

	va_start (args, first_button_text);

	text = first_button_text;
	response_id = va_arg (args, gint);

	while (text != NULL) {
		gtk_dialog_add_button (GTK_DIALOG (dialog), text, response_id);

		text = va_arg (args, char*);
		if (text == NULL)
			break;
		response_id = va_arg (args, int);
	}

	va_end (args);

	return dialog;
}


static GtkWidget *
create_button (const char *stock_id,
	       const char *text)
{
	GtkWidget    *button;
	GtkWidget    *image;
	const char   *label_text;
	gboolean      text_is_stock;
	GtkStockItem  stock_item;

	if (gtk_stock_lookup (text, &stock_item)) {
		label_text = stock_item.label;
		text_is_stock = TRUE;
	}
	else {
		label_text = text;
		text_is_stock = FALSE;
	}

	if (text_is_stock)
		image = gtk_image_new_from_stock (text, GTK_ICON_SIZE_BUTTON);
	else
		image = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new_with_mnemonic (label_text);
	gtk_button_set_image (GTK_BUTTON (button), image);

	gtk_widget_set_can_default (button, TRUE);

	gtk_widget_show (button);

	return button;
}


char *
_gtk_request_dialog_run (GtkWindow      *parent,
			 GtkDialogFlags  flags,
			 const char     *title,
			 const char     *message,
			 const char     *default_value,
			 int             max_length,
			 const gchar    *no_button_text,
			 const gchar    *yes_button_text)
{
	GtkBuilder *builder;
	GtkWidget  *dialog;
	GtkWidget  *label;
	GtkWidget  *entry;
	char       *result;

	builder = _gtk_builder_new_from_resource ("request-dialog.ui");
	dialog = _gtk_builder_get_widget (builder, "request_dialog");
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), (flags & GTK_DIALOG_MODAL));
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_object_weak_ref (G_OBJECT (dialog), (GWeakNotify) g_object_unref, builder);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	label = _gtk_builder_get_widget (builder, "message_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (label), message);

	entry = _gtk_builder_get_widget (builder, "value_entry");
	gtk_entry_set_max_length (GTK_ENTRY (entry), max_length);
	gtk_entry_set_text (GTK_ENTRY (entry), default_value);

	/* Add buttons */

	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      create_button (GTK_STOCK_CANCEL, no_button_text),
				      GTK_RESPONSE_CANCEL);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      create_button (GTK_STOCK_OK, yes_button_text),
				      GTK_RESPONSE_YES);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	/* Run dialog */

	gtk_widget_grab_focus (entry);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES)
		result = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	else
		result = NULL;

	gtk_widget_destroy (dialog);

	return result;
}


GtkWidget *
_gtk_error_dialog_new (GtkWindow      *parent,
		       GtkDialogFlags  flags,
		       GList          *row_output,
		       const char     *primary_text,
		       const char     *secondary_text_format,
		       ...)
{
	GtkBuilder *builder;
	GtkWidget  *dialog;
	char       *escaped_message;
	char       *markup_text;
	gboolean    view_output;

	builder = _gtk_builder_new_from_resource ("error-dialog.ui");
	dialog = _gtk_builder_get_widget (builder, "error_dialog");
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_window_set_modal (GTK_WINDOW (dialog), (flags & GTK_DIALOG_MODAL));
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	g_object_weak_ref (G_OBJECT (dialog), (GWeakNotify) g_object_unref, builder);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	/* label */

	escaped_message = g_markup_escape_text (primary_text, -1);
	if (secondary_text_format != NULL) {
		va_list  args;
		char    *secondary_message;
		char    *escaped_secondary_message;

		va_start (args, secondary_text_format);
		secondary_message = g_strdup_vprintf (secondary_text_format, args);
		va_end (args);
		escaped_secondary_message = g_markup_escape_text (secondary_message, -1);

		markup_text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
					       escaped_message,
					       escaped_secondary_message);

		g_free (escaped_secondary_message);
		g_free (secondary_message);
	}
	else
		markup_text = g_strdup (escaped_message);
	gtk_label_set_markup (GTK_LABEL (_gtk_builder_get_widget (builder, "message_label")), markup_text);
	g_free (markup_text);
	g_free (escaped_message);

	/* output */

	view_output = (row_output != NULL) && (secondary_text_format == NULL);
	if (view_output) {
		GtkTextBuffer *text_buffer;
		GtkTextIter    iter;
		GList         *scan;

		text_buffer = (GtkTextBuffer *) gtk_builder_get_object (builder, "output_textbuffer");
		gtk_text_buffer_create_tag (text_buffer, "monospace",
					    "family", "monospace",
					    NULL);
		gtk_text_buffer_get_iter_at_offset (text_buffer, &iter, 0);
		for (scan = row_output; scan; scan = scan->next) {
			char  *line = scan->data;
			char  *utf8_line;
			gsize  bytes_written;

			utf8_line = g_locale_to_utf8 (line, -1, NULL, &bytes_written, NULL);
			gtk_text_buffer_insert_with_tags_by_name (text_buffer,
								  &iter,
								  utf8_line,
								  bytes_written,
								  "monospace", NULL);
			g_free (utf8_line);

			gtk_text_buffer_insert (text_buffer, &iter, "\n", 1);
		}
	}

	gtk_widget_set_visible (_gtk_builder_get_widget (builder, "output_box"), view_output);

	return dialog;
}


void
_gtk_error_dialog_run (GtkWindow  *parent,
		       const char *main_message,
		       const char *format,
		       ...)
{
	GtkWidget *d;
	char      *message;
	va_list    args;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	d =  _gtk_message_dialog_new (parent,
				      GTK_DIALOG_MODAL,
				      GTK_STOCK_DIALOG_ERROR,
				      main_message,
				      message,
				      GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL,
				      NULL);
	g_free (message);

	g_signal_connect (G_OBJECT (d), "response",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	gtk_widget_show (d);
}


void
_gtk_dialog_add_to_window_group (GtkDialog *dialog)
{
	GtkWidget *toplevel;

	g_return_if_fail (dialog != NULL);

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (dialog));
	if (gtk_widget_is_toplevel (toplevel) && gtk_window_has_group (GTK_WINDOW (toplevel)))
		gtk_window_group_add_window (gtk_window_get_group (GTK_WINDOW (toplevel)), GTK_WINDOW (dialog));
}


void
_gtk_entry_set_locale_text (GtkEntry   *entry,
			    const char *text)
{
	char *utf8_text;

	if (text == NULL)
		return;

	utf8_text = g_locale_to_utf8 (text, -1, NULL, NULL, NULL);
	if (utf8_text != NULL)
		gtk_entry_set_text (entry, utf8_text);
	else
		gtk_entry_set_text (entry, "");
	g_free (utf8_text);
}


char *
_gtk_entry_get_locale_text (GtkEntry *entry)
{
	const char *utf8_text;
	char       *text;

	utf8_text = gtk_entry_get_text (entry);
	if (utf8_text == NULL)
		return NULL;

	text = g_locale_from_utf8 (utf8_text, -1, NULL, NULL, NULL);

	return text;
}


void
_gtk_label_set_locale_text (GtkLabel   *label,
			    const char *text)
{
	char *utf8_text;

	utf8_text = g_locale_to_utf8 (text, -1, NULL, NULL, NULL);
	if (utf8_text != NULL) {
		gtk_label_set_text (label, utf8_text);
		g_free (utf8_text);
	}
	else
		gtk_label_set_text (label, "");
}


char *
_gtk_label_get_locale_text (GtkLabel *label)
{
	const char *utf8_text;
	char       *text;

	utf8_text = gtk_label_get_text (label);
	if (utf8_text == NULL)
		return NULL;

	text = g_locale_from_utf8 (utf8_text, -1, NULL, NULL, NULL);

	return text;
}


void
_gtk_entry_set_filename_text (GtkEntry   *entry,
			      const char *text)
{
	char *utf8_text;

	utf8_text = g_filename_to_utf8 (text, -1, NULL, NULL, NULL);
	if (utf8_text != NULL) {
		gtk_entry_set_text (entry, utf8_text);
		g_free (utf8_text);
	}
	else
		gtk_entry_set_text (entry, "");
}


char *
_gtk_entry_get_filename_text (GtkEntry   *entry)
{
	const char *utf8_text;
	char       *text;

	utf8_text = gtk_entry_get_text (entry);
	if (utf8_text == NULL)
		return NULL;

	text = g_filename_from_utf8 (utf8_text, -1, NULL, NULL, NULL);

	return text;
}


void
_gtk_label_set_filename_text (GtkLabel   *label,
			      const char *text)
{
	char *utf8_text;

	utf8_text = g_filename_display_name (text);
	gtk_label_set_text (label, utf8_text);
	g_free (utf8_text);
}


char *
_gtk_label_get_filename_text (GtkLabel   *label)
{
	const char *utf8_text;
	char       *text;

	utf8_text = gtk_label_get_text (label);
	if (utf8_text == NULL)
		return NULL;

	text = g_filename_from_utf8 (utf8_text, -1, NULL, NULL, NULL);

	return text;
}


GdkPixbuf *
_g_icon_get_pixbuf (GIcon        *icon,
		    int           icon_size,
		    GtkIconTheme *icon_theme)
{
	GdkPixbuf   *pixbuf = NULL;
	GtkIconInfo *icon_info;

	icon_info = gtk_icon_theme_lookup_by_gicon (icon_theme,
						    icon,
						    icon_size,
						    GTK_ICON_LOOKUP_USE_BUILTIN);

	if (icon_info != NULL) {
		GError *error = NULL;

		pixbuf = gtk_icon_info_load_icon (icon_info, &error);
		if (error != NULL) {
			g_print ("%s\n", error->message);
			g_error_free (error);
		}

		gtk_icon_info_free (icon_info);
	}

	return pixbuf;
}


GdkPixbuf *
_g_mime_type_get_icon (const char   *mime_type,
		       int           icon_size,
		       GtkIconTheme *icon_theme)
{
	GdkPixbuf *pixbuf = NULL;
	GIcon     *icon;

	if (icon_theme == NULL)
		icon_theme = gtk_icon_theme_get_default ();

	icon = g_content_type_get_icon (mime_type);
	if (icon == NULL)
		icon = g_themed_icon_new ("text-x-generic");
	pixbuf = _g_icon_get_pixbuf (icon, icon_size, icon_theme);

	g_object_unref (icon);

	return pixbuf;
}


void
_gtk_show_help_dialog (GtkWindow  *parent,
		       const char *section)
{
	char   *uri;
	GError *error = NULL;

	uri = g_strconcat ("help:file-roller", section ? "?" : NULL, section, NULL);
	if (! gtk_show_uri (gtk_window_get_screen (parent), uri, GDK_CURRENT_TIME, &error)) {
  		GtkWidget *dialog;

		dialog = _gtk_message_dialog_new (parent,
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not display help"),
						  error->message,
						  GTK_STOCK_OK, GTK_RESPONSE_OK,
						  NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);

		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

		gtk_widget_show (dialog);

		g_clear_error (&error);
	}
	g_free (uri);
}


GtkBuilder *
_gtk_builder_new_from_file (const char *ui_file)
{
	char       *filename;
	GtkBuilder *builder;
	GError     *error = NULL;

	filename = g_build_filename (UI_DIR, ui_file, NULL);
	builder = gtk_builder_new ();
        if (! gtk_builder_add_from_file (builder, filename, &error)) {
                g_warning ("%s\n", error->message);
                g_clear_error (&error);
        }
	g_free (filename);

        return builder;
}


GtkBuilder *
_gtk_builder_new_from_resource (const char *resource_path)
{
	GtkBuilder *builder;
	char       *full_path;
	GError     *error = NULL;

	builder = gtk_builder_new ();
	full_path = g_strconcat (FILE_ROLLER_RESOURCE_UI_PATH, resource_path, NULL);
        if (! gtk_builder_add_from_resource (builder, full_path, &error)) {
                g_warning ("%s\n", error->message);
                g_clear_error (&error);
        }
	g_free (full_path);

        return builder;
}


GtkWidget *
_gtk_builder_get_widget (GtkBuilder *builder,
			 const char *name)
{
	return (GtkWidget *) gtk_builder_get_object (builder, name);
}


int
_gtk_widget_lookup_for_size (GtkWidget   *widget,
			     GtkIconSize  icon_size)
{
	int w, h;
	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (widget),
					   icon_size,
					   &w, &h);
	return MAX (w, h);
}
