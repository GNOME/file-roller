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


static GtkWidget *
create_button (const char *text)
{
	GtkWidget *button;

	button = gtk_button_new_with_mnemonic (text);
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
	GtkWidget  *content_area;
	GtkWidget  *request_box;
	char       *result;

	builder = _gtk_builder_new_from_resource ("request-dialog.ui");
	request_box = _gtk_builder_get_widget (builder, "request_box");

	dialog = g_object_new (GTK_TYPE_DIALOG,
			      "transient-for", parent,
			      "modal", flags & GTK_DIALOG_MODAL,
			      "use-header-bar", _gtk_settings_get_dialogs_use_header (),
			      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), (flags & GTK_DIALOG_DESTROY_WITH_PARENT));
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_add (GTK_CONTAINER (content_area), request_box);
	g_object_weak_ref (G_OBJECT (request_box), (GWeakNotify) g_object_unref, builder);

	if (flags & GTK_DIALOG_MODAL)
		_gtk_dialog_add_to_window_group (GTK_DIALOG (dialog));

	label = _gtk_builder_get_widget (builder, "message_label");
	gtk_label_set_text_with_mnemonic (GTK_LABEL (label), message);

	entry = _gtk_builder_get_widget (builder, "value_entry");
	gtk_entry_set_max_length (GTK_ENTRY (entry), max_length);
	gtk_entry_set_text (GTK_ENTRY (entry), default_value);

	/* Add buttons */

	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      create_button (no_button_text),
				      GTK_RESPONSE_CANCEL);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      create_button (yes_button_text),
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
		gtk_box_pack_end (GTK_BOX (gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog))),
				  output_box, TRUE, TRUE, 0);

		label = gtk_label_new_with_mnemonic (_("C_ommand Line Output:"));
		gtk_box_pack_start (GTK_BOX (output_box), label, FALSE, FALSE, 0);

		scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
						"shadow-type", GTK_SHADOW_IN,
						"width-request", 450,
						"height-request", 200,
						NULL);
		gtk_box_pack_start (GTK_BOX (output_box), scrolled_window, TRUE, TRUE, 0);

		text_view = gtk_text_view_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), text_view);
		gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);

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

		gtk_widget_show_all (output_box);
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

		g_object_unref (icon_info);
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
	if (! gtk_show_uri_on_window (parent, uri, GDK_CURRENT_TIME, &error)) {
  		GtkWidget *dialog;

		dialog = _gtk_message_dialog_new (parent,
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  _("Could not display help"),
						  error->message,
						  _GTK_LABEL_CLOSE, GTK_RESPONSE_OK,
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

	filename = g_build_filename (UIDIR, ui_file, NULL);
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

	if (! gtk_icon_size_lookup (icon_size, &w, &h))
		w = h = 16;

	return MAX (w, h);
}


static void
password_entry_icon_press_cb (GtkEntry            *entry,
			      GtkEntryIconPosition icon_pos,
			      GdkEvent            *event,
			      gpointer             user_data)
{
	gtk_entry_set_visibility (entry, ! gtk_entry_get_visibility (entry));
}


void
_gtk_entry_use_as_password_entry (GtkEntry *entry)
{
	gtk_entry_set_visibility (entry, FALSE);
	gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, "security-medium-symbolic");
	gtk_entry_set_icon_activatable (entry, GTK_ENTRY_ICON_SECONDARY, TRUE);
	gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY, _("Change password visibility"));

	g_signal_connect (entry,
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

	button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	_gtk_menu_button_set_style_for_header_bar (button);

	return button;
}


GtkWidget *
_gtk_header_bar_create_text_button (const char       *label,
				    const char       *tooltip,
				    const char       *action_name)
{
	GtkWidget *button;

	g_return_val_if_fail (label != NULL, NULL);
	g_return_val_if_fail (action_name != NULL, NULL);

	button = gtk_button_new_with_label (label);
	gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "text-button");
	gtk_style_context_remove_class (gtk_widget_get_style_context (button), "image-button");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
	if (tooltip != NULL)
		gtk_widget_set_tooltip_text (button, tooltip);
	gtk_widget_show (button);

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
	gtk_container_add (GTK_CONTAINER (button), gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU));
	_gtk_menu_button_set_style_for_header_bar (button);
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
	if (tooltip != NULL)
		gtk_widget_set_tooltip_text (button, tooltip);
	gtk_widget_show_all (button);

	return button;
}


/* -- _gtk_window_add_accelerator_for_action -- */


typedef struct {
	GtkWindow *window;
	char      *action_name;
	GVariant  *target;
} AccelData;


static void
accel_data_free (gpointer  user_data,
                 GClosure *closure)
{
	AccelData *accel_data = user_data;

	g_return_if_fail (accel_data != NULL);

	if (accel_data->target != NULL)
		g_variant_unref (accel_data->target);
	g_free (accel_data->action_name);
	g_free (accel_data);
}


static gboolean
window_accelerator_activated_cb (GtkAccelGroup	*accel_group,
				 GObject		*object,
				 guint		 key,
				 GdkModifierType	 mod,
				 gpointer		 user_data)
{
	AccelData *accel_data = user_data;
	GAction   *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (accel_data->window), accel_data->action_name);
	if (action != NULL) {
		g_action_activate (action, accel_data->target);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}


void
_gtk_window_add_accelerator_for_action (GtkWindow	*window,
					GtkAccelGroup	*accel_group,
					const char	*action_name,
					const char	*accel,
					GVariant	*target)
{
	AccelData	*accel_data;
	guint		 key;
	GdkModifierType  mods;
	GClosure	*closure;

	if ((action_name == NULL) || (accel == NULL))
		return;

	if (g_str_has_prefix (action_name, "app."))
		return;

	accel_data = g_new0 (AccelData, 1);
	accel_data->window = window;
	/* remove the win. prefix from the action name */
	if (g_str_has_prefix (action_name, "win."))
		accel_data->action_name = g_strdup (action_name + strlen ("win."));
	else
		accel_data->action_name = g_strdup (action_name);
	if (target != NULL)
		accel_data->target = g_variant_ref (target);

	gtk_accelerator_parse (accel, &key, &mods);
	closure = g_cclosure_new (G_CALLBACK (window_accelerator_activated_cb),
				  accel_data,
				  accel_data_free);
	gtk_accel_group_connect (accel_group,
				 key,
				 mods,
				 0,
				 closure);
}


/* -- _gtk_window_add_accelerators_from_menu --  */


static void
add_accelerators_from_menu_item (GtkWindow      *window,
				 GtkAccelGroup  *accel_group,
				 GMenuModel     *model,
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

	_gtk_window_add_accelerator_for_action (window,
						accel_group,
						action,
						accel,
						target);

	if (target != NULL)
		g_variant_unref (target);
}


static void
add_accelerators_from_menu (GtkWindow      *window,
			    GtkAccelGroup  *accel_group,
			    GMenuModel     *model)
{
	int		 i;
	GMenuLinkIter	*iter;
	const char	*key;
	GMenuModel	*m;

	for (i = 0; i < g_menu_model_get_n_items (model); i++) {
		add_accelerators_from_menu_item (window, accel_group, model, i);

		iter = g_menu_model_iterate_item_links (model, i);
		while (g_menu_link_iter_get_next (iter, &key, &m)) {
			add_accelerators_from_menu (window, accel_group, m);
			g_object_unref (m);
		}
		g_object_unref (iter);
	}
}


void
_gtk_window_add_accelerators_from_menu (GtkWindow  *window,
					GMenuModel *menu)
{
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new ();
	add_accelerators_from_menu (window, accel_group, menu);
	gtk_window_add_accel_group (window, accel_group);
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
