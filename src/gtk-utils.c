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
#include "glib-utils.h"
#include "gio-utils.h"


void
_gtk_dialog_run (GtkDialog *dialog)
{
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_window_destroy), NULL);
	gtk_window_present (GTK_WINDOW (dialog));
}


GtkWidget *
_gtk_message_dialog_new (GtkWindow      *parent,
			 GtkDialogFlags  flags,
			 const char     *message,
			 const char     *secondary_message,
			 const gchar    *first_button_text,
			 ...)
{
	GtkWidget   *dialog;
	va_list      args;
	const gchar *text;
	int          response_id;

	dialog = gtk_message_dialog_new (parent,
					 flags,
					 GTK_MESSAGE_OTHER,
					 GTK_BUTTONS_NONE,
					 "%s", message);

	if (secondary_message != NULL)
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", secondary_message);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

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


GtkWidget *
_gtk_request_dialog_new (GtkWindow      *parent,
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
	GtkWidget  *request_box;

	builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "request-dialog.ui");
	request_box = _gtk_builder_get_widget (builder, "request_box");

	dialog = g_object_new (GTK_TYPE_DIALOG,
			      "transient-for", parent,
			      "modal", flags & GTK_DIALOG_MODAL,
			      "use-header-bar", _gtk_settings_get_dialogs_use_header (),
			      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), request_box);
	g_object_set_data_full (G_OBJECT (dialog), "builder", builder, g_object_unref);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	label = _gtk_builder_get_widget (builder, "message_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (label), message);

	entry = _gtk_builder_get_widget (builder, "value_entry");
	gtk_entry_set_max_length (GTK_ENTRY (entry), max_length);
	gtk_editable_set_text (GTK_EDITABLE (entry), default_value);
	gtk_widget_grab_focus (entry);

	/* Add buttons */

	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      gtk_button_new_with_mnemonic (no_button_text),
				      GTK_RESPONSE_CANCEL);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      gtk_button_new_with_mnemonic (yes_button_text),
				      GTK_RESPONSE_YES);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	return dialog;
}


char *
_gth_request_dialog_get_text (GtkDialog *dialog)
{
	GtkBuilder *builder;
	GtkWidget  *entry;

	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	if (builder == NULL)
		return NULL;

	entry = _gtk_builder_get_widget (builder, "value_entry");
	if (entry == NULL)
		return NULL;

	return g_strdup (gtk_editable_get_text (GTK_EDITABLE (entry)));
}


GtkWidget *
_gtk_error_dialog_new (GtkWindow      *parent,
		       GtkDialogFlags  flags,
		       GList          *row_output,
		       const char     *primary_text,
		       const char     *secondary_text_format,
		       ...)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (parent,
					 flags,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", primary_text);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	/* label */

	if (secondary_text_format != NULL) {
		va_list  args;
		char    *secondary_message;

		va_start (args, secondary_text_format);
		secondary_message = g_strdup_vprintf (secondary_text_format, args);
		va_end (args);

		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", secondary_message);

		g_free (secondary_message);
	}

	/* output */

	if ((row_output != NULL) && (secondary_text_format == NULL)) {
		GtkWidget     *output_box;
		GtkWidget     *label;
		GtkWidget     *scrolled_window;
		GtkWidget     *text_view;
		GtkTextBuffer *text_buffer;
		GtkTextIter    iter;
		GList         *scan;

		output_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
		_gtk_box_pack_end (GTK_BOX (gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog))),
				   output_box,
				   TRUE,
				   FALSE);

		label = gtk_label_new_with_mnemonic (_("C_ommand Line Output:"));
		gtk_box_append (GTK_BOX (output_box), label);

		scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
						"has-frame", TRUE,
						"width-request", 450,
						"height-request", 200,
						NULL);
		_gtk_box_pack_start (GTK_BOX (output_box), scrolled_window, TRUE, TRUE);

		text_view = gtk_text_view_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), text_view);
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), text_view);

		text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
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

		gtk_widget_show (output_box);
	}

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
				      main_message,
				      message,
				      _GTK_LABEL_CLOSE, GTK_RESPONSE_CANCEL,
				      NULL);
	_gtk_dialog_run (GTK_DIALOG (d));

	g_free (message);
}


void
_gtk_dialog_add_to_window_group (GtkDialog *dialog)
{
	GtkRoot *toplevel;

	g_return_if_fail (dialog != NULL);

	toplevel = gtk_widget_get_root (GTK_WIDGET (dialog));
	if ((toplevel != NULL) && gtk_window_has_group (GTK_WINDOW (toplevel)))
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
	gtk_editable_set_text (GTK_EDITABLE (entry), (utf8_text != NULL) ? utf8_text : "");
	g_free (utf8_text);
}


char *
_gtk_entry_get_locale_text (GtkEntry *entry)
{
	const char *utf8_text;
	char       *text;

	utf8_text = gtk_editable_get_text (GTK_EDITABLE (entry));
	if (utf8_text == NULL)
		return NULL;

	text = g_locale_from_utf8 (utf8_text, -1, NULL, NULL, NULL);

	return text;
}


void
_gtk_show_help_dialog (GtkWindow  *parent,
		       const char *section)
{
	char *uri;

	uri = g_strconcat ("help:file-roller", section ? "?" : NULL, section, NULL);
	gtk_show_uri (parent, uri, GDK_CURRENT_TIME);

	g_free (uri);
}


GtkWidget *
_gtk_builder_get_widget (GtkBuilder *builder,
			 const char *name)
{
	return (GtkWidget *) gtk_builder_get_object (builder, name);
}


static void
password_entry_icon_press_cb (GtkEntry            *entry,
			      GtkEntryIconPosition icon_pos,
			      gpointer             user_data)
{
	gboolean visibility;

	visibility = gtk_entry_get_visibility (entry);
	gtk_entry_set_visibility (entry, ! visibility);
	if (visibility)
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
	else
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, "view-conceal-symbolic");
}


void
_gtk_entry_use_as_password_entry (GtkEntry *entry)
{
	gtk_entry_set_visibility (entry, FALSE);
	gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
	gtk_entry_set_icon_activatable (entry, GTK_ENTRY_ICON_SECONDARY, TRUE);
	gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY, _("Change password visibility"));

	g_signal_connect (GTK_ENTRY (entry),
			  "icon-press",
			  G_CALLBACK (password_entry_icon_press_cb),
			  NULL);
}


static void
_gtk_menu_button_set_style_for_header_bar (GtkWidget *button)
{
	GtkStyleContext *context;

	gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
	context = gtk_widget_get_style_context (button);
	gtk_style_context_add_class (context, "image-button");
	gtk_style_context_remove_class (context, "text-button");
}


GtkWidget *
_gtk_menu_button_new_for_header_bar (void)
{
	GtkWidget *button;

	button = gtk_menu_button_new ();
	_gtk_menu_button_set_style_for_header_bar (button);

	return button;
}


GtkWidget *
_gtk_image_button_new_for_header_bar (const char *icon_name)
{
	GtkWidget *button;

	button = gtk_button_new_from_icon_name (icon_name);
	_gtk_menu_button_set_style_for_header_bar (button);

	return button;
}


GtkWidget *
_gtk_header_bar_create_image_button (const char       *icon_name,
				     const char       *tooltip,
				     const char       *action_name)
{
	GtkWidget *button;

	g_return_val_if_fail (icon_name != NULL, NULL);
	g_return_val_if_fail (action_name != NULL, NULL);

	button = _gtk_image_button_new_for_header_bar (icon_name);
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
	if (tooltip != NULL)
		gtk_widget_set_tooltip_text (button, tooltip);
	gtk_widget_show (button);

	return button;
}


GtkWidget *
_gtk_header_bar_create_image_toggle_button (const char       *icon_name,
					    const char       *tooltip,
					    const char       *action_name)
{
	GtkWidget *button;

	g_return_val_if_fail (icon_name != NULL, NULL);
	g_return_val_if_fail (action_name != NULL, NULL);

	button = gtk_toggle_button_new ();
	gtk_button_set_icon_name (GTK_BUTTON (button), icon_name);
	_gtk_menu_button_set_style_for_header_bar (button);
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
	if (tooltip != NULL)
		gtk_widget_set_tooltip_text (button, tooltip);
	gtk_widget_show (button);

	return button;
}


/* -- _gtk_window_add_accelerator_for_action -- */


void
_gtk_add_accelerator_for_action (const char	*action_name,
					const char	*accel,
					GVariant	*target)
{
	g_autofree gchar *detailed_action = NULL;

	if ((action_name == NULL) || (accel == NULL))
		return;

	if (target == NULL) {
		detailed_action = g_strdup(action_name);
	} else {
		g_autofree gchar *target_string = g_variant_print (target, TRUE);
		detailed_action = g_strconcat (action_name, "(", target_string, ")", NULL);
	}

	_gtk_application_add_accelerator_for_action (GTK_APPLICATION (g_application_get_default ()), detailed_action, accel);

}


/* -- _gtk_add_accelerators_from_menu --  */


static void
add_accelerators_from_menu_item (GMenuModel     *model,
				 int             item)
{
	GMenuAttributeIter	*iter;
	const char		*key;
	GVariant		*value;
	const char		*accel = NULL;
	const char		*action = NULL;
	GVariant		*target = NULL;


	iter = g_menu_model_iterate_item_attributes (model, item);
	while (g_menu_attribute_iter_get_next (iter, &key, &value)) {
		if (g_str_equal (key, "action") && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
			action = g_variant_get_string (value, NULL);
		else if (g_str_equal (key, "accel") && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
			accel = g_variant_get_string (value, NULL);
		else if (g_str_equal (key, "target"))
			target = g_variant_ref (value);
		g_variant_unref (value);
	}
	g_object_unref (iter);

	_gtk_add_accelerator_for_action (action, accel, target);

	if (target != NULL)
		g_variant_unref (target);
}


void
_gtk_add_accelerators_from_menu (GMenuModel *model)
{
	int		 i;
	GMenuLinkIter	*iter;
	const char	*key;
	GMenuModel	*m;

	for (i = 0; i < g_menu_model_get_n_items (model); i++) {
		add_accelerators_from_menu_item (model, i);

		iter = g_menu_model_iterate_item_links (model, i);
		while (g_menu_link_iter_get_next (iter, &key, &m)) {
			_gtk_add_accelerators_from_menu (m);
			g_object_unref (m);
		}
		g_object_unref (iter);
	}
}


gboolean
_gtk_settings_get_dialogs_use_header (void)
{
	gboolean use_header;

	g_object_get (gtk_settings_get_default (),
		      "gtk-dialogs-use-header", &use_header,
		      NULL);

	return use_header;
}


void
_gtk_application_add_accelerator_for_action (GtkApplication   *app,
					     const char       *action_name,
					     const char       *accel)
{
	const char *accels[2];

	accels[0] = accel;
	accels[1] = NULL;
	gtk_application_set_accels_for_action (app, action_name, accels);
}


void
_gtk_application_add_accelerators (GtkApplication *app,
				   const FrAccelerator  *accelerators,
				   int             n_accelerators)
{
	int i;

	for (i = 0; i < n_accelerators; i++) {
		const FrAccelerator *acc = accelerators + i;
		_gtk_application_add_accelerator_for_action (GTK_APPLICATION (app),
							     acc->action_name,
							     acc->accelerator);
	}
}

void
_gtk_popover_popup_at_selected (GtkPopover *popover, GtkTreeView *tree_view)
{
	int width = gtk_widget_get_allocated_width (GTK_WIDGET (tree_view));
	int height = gtk_widget_get_allocated_height (GTK_WIDGET (tree_view));
	gdouble center_x = 0.5 * width;
	gdouble center_y = 0.5 * height;

	GdkRectangle rect;
	GtkTreeSelection *selection;
	GList *list;

	selection = gtk_tree_view_get_selection (tree_view);
	list = gtk_tree_selection_get_selected_rows (selection, NULL);
	if (list) {
		int min_y = height;
		int max_y = 0;
		int max_height = 0;

		for (GList *item = list; item; item = item->next) {
			GtkTreePath *path = list->data;

			gtk_tree_view_get_cell_area (tree_view, path, NULL, &rect);
			gtk_tree_view_convert_bin_window_to_widget_coords (tree_view, rect.x, rect.y, &rect.x, &rect.y);

			min_y = MIN (min_y, rect.y);
			if (rect.y > max_y) {
				max_y = rect.y;
				max_height = rect.height;
			}
		}

		rect.x = CLAMP (center_x - 20, 0, width - 40);
		rect.y = min_y;
		rect.width = 40;
		rect.height = max_y + max_height - min_y;

		g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
	} else {
		rect.x = center_x;
		rect.y = center_y;
		rect.width = 1;
		rect.height = 1;
	}

	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_popover_popup (GTK_POPOVER (popover));
}

void
_gtk_popover_popup_at_position (GtkPopover *popover, gdouble x, gdouble y)
{
	GdkRectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = 1;
	rect.height = 1;

	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_popover_popup (GTK_POPOVER (popover));
}


void
_gtk_box_pack_start (GtkBox    *box,
		     GtkWidget *child,
		     gboolean   hexpand,
		     gboolean   vexpand)
{
	gtk_widget_set_hexpand (child, hexpand);
	gtk_widget_set_vexpand (child, vexpand);
	gtk_box_prepend (box, child);
}


void
_gtk_box_pack_end (GtkBox    *box,
		   GtkWidget *child,
		   gboolean   hexpand,
		   gboolean   vexpand)
{
	gtk_widget_set_hexpand (child, hexpand);
	gtk_widget_set_vexpand (child, vexpand);
	gtk_box_append (box, child);
}


void
_gtk_widget_set_margin (GtkWidget *widget, int margin)
{
	gtk_widget_set_margin_start (widget, margin);
	gtk_widget_set_margin_end (widget, margin);
	gtk_widget_set_margin_top (widget, margin);
	gtk_widget_set_margin_bottom (widget, margin);
}


typedef struct {
	GtkWindow *window;
	GFile     *folder;
	GFunc      callback;
	gpointer   user_data;
} OpenFolderData;


static OpenFolderData *
open_folder_data_new (GtkWindow *window,
		      GFile     *folder,
		      GFunc      callback,
		      gpointer   user_data)
{
	OpenFolderData *data;

	data = g_new (OpenFolderData, 1);
	data->window = g_object_ref (window);
	data->folder = g_object_ref (folder);
	data->callback = callback;
	data->user_data = user_data;

	return data;
}


static void
open_folder_data_free (OpenFolderData *data)
{
	g_object_unref (data->window);
	g_object_unref (data->folder);
	g_free (data);
}


static void
_gtk_show_folder_cb (GObject      *source_object,
		     GAsyncResult *res,
		     gpointer      user_data)
{
	OpenFolderData *data = user_data;
	GError         *error = NULL;

	if (!gtk_show_uri_full_finish (data->window, res, &error)) {
		char *utf8_name = _g_file_get_display_name (data->folder);
		char *msg = g_strdup_printf (_("Could not open “%s”"), utf8_name);
		_gtk_error_dialog_run (data->window, msg, "%s", error->message);

		g_free (msg);
		g_free (utf8_name);
		g_error_free (error);
	}
	if (data->callback != NULL)
		data->callback (data->window, data->user_data);
	open_folder_data_free (data);
}


void
_gtk_show_folder (GtkWindow *parent_window,
		  GFile     *folder,
		  GFunc      callback,
		  gpointer   user_data)
{
	char *uri = g_file_get_uri (folder);
	gtk_show_uri_full (
		parent_window,
		uri,
		GDK_CURRENT_TIME,
		NULL,
		_gtk_show_folder_cb,
		open_folder_data_new (parent_window, folder, callback, user_data)
	);
	g_free (uri);
}


static void
file_manager_show_items_cb (GObject      *source_object,
			    GAsyncResult *res,
			    gpointer      user_data)
{
	OpenFolderData *data = user_data;
	GDBusProxy     *proxy;
	GVariant       *values;
	GError         *error = NULL;

	proxy = G_DBUS_PROXY (source_object);
	values = g_dbus_proxy_call_finish (proxy, res, &error);
	if (values == NULL) {
		_gtk_show_folder (data->window, data->folder, data->callback, data->user_data);
		g_clear_error (&error);
	}
	else {
		if (data->callback != NULL)
			data->callback (data->window, data->user_data);
	}

	if (values != NULL)
		g_variant_unref (values);
	g_object_unref (proxy);
	open_folder_data_free (data);
}


void
_gtk_show_file_in_container (GtkWindow *parent_window,
			     GFile     *file,
			     GFunc      callback,
			     gpointer   user_data)
{
	GFile           *parent;
	GDBusConnection *connection;
	GError          *error = NULL;

	parent = g_file_get_parent (file);
	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (connection != NULL) {
		GDBusProxy *proxy;

		proxy = g_dbus_proxy_new_sync (connection,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.freedesktop.FileManager1",
					       "/org/freedesktop/FileManager1",
					       "org.freedesktop.FileManager1",
					       NULL,
					       &error);

		if (proxy != NULL) {
			static int   sequence = 0;
			char       **uris;
			char        *startup_id;

			uris = g_new (char *, 2);
			uris[0] = g_file_get_uri (file);
			uris[1] = NULL;

			startup_id = g_strdup_printf ("%s-%lu-%s-%s-%d_TIME%lu",
						      g_get_prgname (),
						      (unsigned long) getpid (),
						      g_get_host_name (),
						      "org.freedesktop.FileManager1",
						      sequence++,
						      (unsigned long) g_get_real_time ());

			g_dbus_proxy_call (proxy,
					   "ShowItems",
					   g_variant_new ("(^ass)", uris, startup_id),
					   G_DBUS_CALL_FLAGS_NONE,
					   G_MAXINT,
					   NULL,
					   file_manager_show_items_cb,
					   open_folder_data_new (parent_window, parent, callback, user_data));

			g_free (startup_id);
			g_strfreev (uris);

			return;
		}
	}

	_gtk_show_folder (parent_window, parent, callback, user_data);

	g_object_unref (parent);
}
