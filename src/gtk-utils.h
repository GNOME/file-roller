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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GTK_UTILS_H
#define GTK_UTILS_H

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#define _GTK_LABEL_ADD _("_Add")
#define _GTK_LABEL_CANCEL _("_Cancel")
#define _GTK_LABEL_CLOSE _("_Close")
#define _GTK_LABEL_CREATE_ARCHIVE _("C_reate")
#define _GTK_LABEL_EXTRACT _("_Extract")
#define _GTK_LABEL_OPEN _("_Open")
#define _GTK_LABEL_SAVE _("_Save")


typedef struct {
	const char *action_name;
	const char *accelerator;
} FrAccelerator;

void          _gtk_dialog_run                      (GtkDialog *dialog);
GtkWidget *   _gtk_message_dialog_new              (GtkWindow        *parent,
						    GtkDialogFlags    flags,
						    const char       *message,
						    const char       *secondary_message,
						    const char       *first_button_text,
						    ...);
GtkWidget *   _gtk_request_dialog_new              (GtkWindow        *parent,
						    GtkDialogFlags    flags,
						    const char       *title,
						    const char       *message,
						    const char       *default_value,
						    int               max_length,
						    const gchar      *no_button_text,
						    const gchar      *yes_button_text);
char *        _gth_request_dialog_get_text         (GtkDialog        *dialog);
GtkWidget *   _gtk_error_dialog_new                (GtkWindow        *parent,
						    GtkDialogFlags    flags,
						    GList            *row_output,
						    const char       *primary_text,
						    const char       *secondary_text_format,
						    ...) G_GNUC_PRINTF (5, 6);
void          _gtk_error_dialog_run                (GtkWindow        *parent,
						    const gchar      *main_message,
						    const gchar      *format,
						    ...);
void          _gtk_dialog_add_to_window_group      (GtkDialog        *dialog);
void          _gtk_entry_set_locale_text           (GtkEntry         *entry,
						    const char       *text);
char *        _gtk_entry_get_locale_text           (GtkEntry         *entry);
void          _gtk_show_help_dialog                (GtkWindow        *parent,
						    const char       *section);
GtkWidget *   _gtk_builder_get_widget              (GtkBuilder       *builder,
						    const char       *name);
void          _gtk_entry_use_as_password_entry     (GtkEntry         *entry);
GtkWidget *   _gtk_menu_button_new_for_header_bar  (void);
GtkWidget *   _gtk_image_button_new_for_header_bar (const char       *icon_name);
GtkWidget *   _gtk_header_bar_create_image_button  (const char       *icon_name,
						    const char       *tooltip,
						    const char       *action_name);
GtkWidget *   _gtk_header_bar_create_image_toggle_button
						   (const char       *icon_name,
						    const char       *tooltip,
						    const char       *action_name);
void          _gtk_add_accelerator_for_action
						   (const char       *action_name,
						    const char       *accel,
						    GVariant         *target);
void          _gtk_add_accelerators_from_menu (GMenuModel       *menu);
gboolean      _gtk_settings_get_dialogs_use_header (void);
void          _gtk_application_add_accelerator_for_action
						   (GtkApplication   *app,
						    const char       *action_name,
						    const char       *accel);
void	      _gtk_application_add_accelerators    (GtkApplication   *app,
						    const FrAccelerator *accelerators,
						    int               n_accelerators);
void	      _gtk_popover_popup_at_selected (GtkPopover *popover, GtkTreeView *view);
void	      _gtk_popover_popup_at_position (GtkPopover *popover, gdouble x, gdouble y);
void          _gtk_box_pack_start            (GtkBox     *box,
					      GtkWidget  *child,
					      gboolean    hexpand,
					      gboolean    vexpand);
void          _gtk_box_pack_end              (GtkBox     *box,
					      GtkWidget  *child,
					      gboolean    hexpand,
					      gboolean    vexpand);
void          _gtk_widget_set_margin         (GtkWidget  *widget,
					      int         margin);
void          _gtk_show_folder               (GtkWindow  *parent_window,
					      GFile      *folder,
					      GFunc       callback,
					      gpointer    user_data);
void          _gtk_show_file_in_container    (GtkWindow  *window,
					      GFile      *file,
					      GFunc       callback,
					      gpointer    user_data);

#endif
