/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2008, 2012 Free Software Foundation, Inc.
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
#include <math.h>
#include <unistd.h>
#include <gio/gio.h>
#include "file-utils.h"
#include "fr-init.h"
#include "fr-new-archive-dialog.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_WIDGET(x)		(_gtk_builder_get_widget (self->builder, (x)))
#define MEGABYTE		(1024 * 1024)
#define ARCHIVE_ICON_SIZE	48


struct _FrNewArchiveDialog {
	GtkDialog   parent_instance;
	GSettings  *settings;
	GtkBuilder *builder;
	int        *supported_types;
	GHashTable *supported_ext;
	gboolean    can_encrypt;
	gboolean    can_encrypt_header;
	gboolean    can_create_volumes;
	GFile      *original_file;
	GList      *files_to_add;
};


G_DEFINE_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, GTK_TYPE_DIALOG)


static void
fr_new_archive_dialog_finalize (GObject *object)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (object);

	_g_object_list_unref (self->files_to_add);
	_g_object_unref (self->original_file);
	g_object_unref (self->settings);
	g_object_unref (self->builder);
	g_hash_table_unref (self->supported_ext);

	G_OBJECT_CLASS (fr_new_archive_dialog_parent_class)->finalize (object);
}


static int
get_selected_format (FrNewArchiveDialog *self)
{
	return self->supported_types[gtk_combo_box_get_active (GTK_COMBO_BOX (GET_WIDGET ("extension_comboboxtext")))];
}


static void
fr_new_archive_dialog_unmap (GtkWidget *widget)
{
	FrNewArchiveDialog *self;
	int                 n_format;

	self = FR_NEW_ARCHIVE_DIALOG (widget);

	g_settings_set_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton"))));
	g_settings_set_boolean (self->settings, PREF_NEW_EXPAND_OPTIONS, gtk_expander_get_expanded (GTK_EXPANDER (GET_WIDGET ("other_options_expander"))));
	g_settings_set_int (self->settings, PREF_NEW_VOLUME_SIZE, gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton"))) * MEGABYTE);

	n_format = get_selected_format (self);
	g_settings_set_string (self->settings, PREF_NEW_DEFAULT_EXTENSION, mime_type_desc[n_format].default_ext);

	GTK_WIDGET_CLASS (fr_new_archive_dialog_parent_class)->unmap (widget);
}


static void
fr_new_archive_dialog_class_init (FrNewArchiveDialogClass *klass)
{
	GObjectClass   *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fr_new_archive_dialog_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->unmap = fr_new_archive_dialog_unmap;
}


static void
fr_new_archive_dialog_init (FrNewArchiveDialog *self)
{
	self->settings = g_settings_new (FILE_ROLLER_SCHEMA_NEW);
	self->builder = NULL;
	self->supported_ext = g_hash_table_new (g_str_hash, g_str_equal);
	self->original_file = NULL;
	self->files_to_add = NULL;
}


static void
_fr_new_archive_dialog_update_sensitivity (FrNewArchiveDialog *self)
{
	gtk_widget_set_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (self), GTK_RESPONSE_OK), ! _g_utf8_all_spaces (gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("filename_entry")))));
	gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")), ! self->can_encrypt_header);
	gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), self->can_encrypt_header);
	gtk_widget_set_sensitive (GET_WIDGET ("volume_spinbutton"), ! self->can_create_volumes || gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("volume_checkbutton"))));
}


static void
entry_changed_cb (GtkEditable *editable,
		  gpointer     user_data)
{
	_fr_new_archive_dialog_update_sensitivity (FR_NEW_ARCHIVE_DIALOG (user_data));
}


static void
volume_toggled_cb (GtkToggleButton *toggle_button,
		   gpointer         user_data)
{
	_fr_new_archive_dialog_update_sensitivity (FR_NEW_ARCHIVE_DIALOG (user_data));
}


static void
extension_comboboxtext_changed_cb (GtkComboBox *combo_box,
				   gpointer     user_data)
{
	FrNewArchiveDialog *self = user_data;
	int                 n_format;

	n_format = get_selected_format (self);

	self->can_encrypt = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT;
	gtk_widget_set_sensitive (GET_WIDGET ("password_entry"), self->can_encrypt);
	gtk_widget_set_sensitive (GET_WIDGET ("password_label"), self->can_encrypt);

	self->can_encrypt_header = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), self->can_encrypt_header);

	self->can_create_volumes = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES;
	gtk_widget_set_sensitive (GET_WIDGET ("volume_box"), self->can_create_volumes);

	_fr_new_archive_dialog_update_sensitivity (self);
}


static char *
_g_path_remove_extension_if_archive (const char *filename)
{
	const char *ext;
	int         i;

	ext = _g_filename_get_extension (filename);
	if (ext == NULL)
		return g_strdup (filename);

	for (i = 0; file_ext_type[i].ext != NULL; i++) {
		if (strcmp (ext, file_ext_type[i].ext) == 0)
			return g_strndup (filename, strlen (filename) - strlen (ext))  ;
	}

	return g_strdup (filename);
}


static void
_fr_new_archive_dialog_construct (FrNewArchiveDialog *self,
				  GtkWindow          *parent,
				  FrNewArchiveAction  action,
				  GFile              *folder,
				  const char         *default_name,
				  GFile              *original_file)
{
	char *active_extension;
	int   active_extension_idx;
	int   i;

	gtk_window_set_transient_for (GTK_WINDOW (self), parent);
	gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (self), 5);

	self->builder = _gtk_builder_new_from_resource ("new-archive-dialog.ui");
	if (self->builder == NULL)
		return;

	_g_object_unref (self->original_file);
	self->original_file = _g_object_ref (original_file);

	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"));

	gtk_dialog_add_button (GTK_DIALOG (self), _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL);
	switch (action) {
	case FR_NEW_ARCHIVE_ACTION_NEW_MANY_FILES:
		self->supported_types = create_type;
		gtk_dialog_add_button (GTK_DIALOG (self), _GTK_LABEL_CREATE_ARCHIVE, GTK_RESPONSE_OK);
		break;
	case FR_NEW_ARCHIVE_ACTION_NEW_SINGLE_FILE:
		self->supported_types = single_file_save_type;
		gtk_dialog_add_button (GTK_DIALOG (self), _GTK_LABEL_CREATE_ARCHIVE, GTK_RESPONSE_OK);
		break;
	case FR_NEW_ARCHIVE_ACTION_SAVE_AS:
		self->supported_types = save_type;
		gtk_dialog_add_button (GTK_DIALOG (self), _GTK_LABEL_SAVE, GTK_RESPONSE_OK);
		break;
	}
	gtk_dialog_set_default_response (GTK_DIALOG (self), GTK_RESPONSE_OK);

	sort_mime_types_by_extension (self->supported_types);

	/* Set widgets data. */

	if (default_name != NULL) {
		char *default_name_no_ext;

		default_name_no_ext = _g_path_remove_extension_if_archive (default_name);
		gtk_entry_set_text (GTK_ENTRY (GET_WIDGET ("filename_entry")), default_name_no_ext);

		g_free (default_name_no_ext);
	}
	gtk_widget_grab_focus (GET_WIDGET ("filename_entry"));
	gtk_editable_select_region (GTK_EDITABLE (GET_WIDGET ("filename_entry")), 0, -1);

	if (folder == NULL)
		folder = _g_file_get_home ();
	gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (GET_WIDGET ("parent_filechooserbutton")), folder, NULL);

	gtk_expander_set_expanded (GTK_EXPANDER (GET_WIDGET ("other_options_expander")),
				      g_settings_get_boolean (self->settings, PREF_NEW_EXPAND_OPTIONS));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")),
				      g_settings_get_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER));

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")),
				   (double) g_settings_get_int (self->settings, PREF_NEW_VOLUME_SIZE) / MEGABYTE);

	active_extension = g_settings_get_string (self->settings, PREF_NEW_DEFAULT_EXTENSION);
	active_extension_idx = 0;
	for (i = 0; self->supported_types[i] != -1; i++) {
		if (strcmp (active_extension, mime_type_desc[self->supported_types[i]].default_ext) == 0)
			active_extension_idx = i;
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (GET_WIDGET ("extension_comboboxtext")),
					        mime_type_desc[self->supported_types[i]].default_ext);
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (GET_WIDGET ("extension_comboboxtext")), active_extension_idx);
	g_free (active_extension);

	gtk_widget_set_vexpand (GET_WIDGET ("other_options_expander"), FALSE);

	_fr_new_archive_dialog_update_sensitivity (self);
	extension_comboboxtext_changed_cb (GTK_COMBO_BOX (GET_WIDGET ("extension_comboboxtext")), self);

	_gtk_entry_use_as_password_entry (GTK_ENTRY (GET_WIDGET ("password_entry")));

	/* Set the signals handlers. */

	g_signal_connect (GET_WIDGET ("filename_entry"),
			  "changed",
			  G_CALLBACK (entry_changed_cb),
			  self);
	g_signal_connect (GET_WIDGET ("password_entry"),
			  "changed",
			  G_CALLBACK (entry_changed_cb),
			  self);
	g_signal_connect (GET_WIDGET ("volume_checkbutton"),
			  "toggled",
			  G_CALLBACK (volume_toggled_cb),
			  self);
	g_signal_connect (GET_WIDGET ("extension_comboboxtext"),
			  "changed",
			  G_CALLBACK (extension_comboboxtext_changed_cb),
			  self);
}


GtkWidget *
fr_new_archive_dialog_new (const char         *title,
			   GtkWindow          *parent,
			   FrNewArchiveAction  action,
			   GFile              *folder,
			   const char         *default_name,
			   GFile              *original_file)
{
	FrNewArchiveDialog *self;

	self = g_object_new (fr_new_archive_dialog_get_type (),
			     "title", title,
			     "use-header-bar", _gtk_settings_get_dialogs_use_header (),
			     NULL);
	_fr_new_archive_dialog_construct (self, parent, action, folder, default_name, original_file);

	return (GtkWidget *) self;
}


void
fr_new_archive_dialog_set_files_to_add (FrNewArchiveDialog  *self,
					GList               *file_list /* GFile list */)
{
	_g_object_list_unref (self->files_to_add);
	self->files_to_add = _g_object_list_ref (file_list);
}


GFile *
fr_new_archive_dialog_get_file (FrNewArchiveDialog  *self,
			        const char         **mime_type)
{
	const char *basename;
	int         n_format;
	char       *basename_ext;
	GFile      *parent;
	GFile      *file;
	GError     *error = NULL;
	const char *file_mime_type;
	GFileInfo  *parent_info;
	GtkWidget  *dialog;

	/* Check whether the user entered a valid archive name. */

	basename = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("filename_entry")));
	if ((basename == NULL) || (*basename == '\0')) {
		GtkWidget *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (self),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not create the archive"),
					   "%s",
					   _("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		return NULL;
	}

	/* file */

	n_format = get_selected_format (self);
	parent = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (GET_WIDGET ("parent_filechooserbutton")));
	if (parent == NULL) {
		GtkWidget *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (self),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not create the archive"),
					   "%s",
					   _("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		return NULL;
	}

	basename_ext = g_strconcat (basename, mime_type_desc[n_format].default_ext, NULL);
	file = g_file_get_child_for_display_name (parent, basename_ext, &error);

	if (file == NULL) {
		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL,
						NULL,
						_("Could not create the archive"),
						"%s",
						error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));
		g_error_free (error);
		g_free (basename_ext);
		g_object_unref (parent);

		return NULL;
	}

	g_free (basename_ext);

	/* mime type */

	file_mime_type = mime_type_desc[n_format].mime_type;
	if (mime_type != NULL)
		*mime_type = file_mime_type;

	/* check permissions */

	parent_info = g_file_query_info (parent,
				         (G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				          G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				          G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE","
				          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME),
				         0,
				         NULL,
				         &error);

	g_object_unref (parent);

	if (error != NULL) {
		g_warning ("Failed to get permission for extraction dir: %s", error->message);

		g_clear_error (&error);
		g_object_unref (parent_info);
		g_object_unref (file);

		return NULL;
	}

	if (g_file_info_has_attribute (parent_info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE) &&
				       ! g_file_info_get_attribute_boolean (parent_info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) {
		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("You don’t have permission to create an archive in this folder"));
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));
		g_object_unref (parent_info);
		g_object_unref (file);

		return NULL;
	}

	/* check whehter the file is equal to the original file */

	if ((self->original_file != NULL) && (g_file_equal (file, self->original_file))) {
		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("New name is the same as old one, please type other name."));
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));
		g_object_unref (parent_info);
		g_object_unref (file);

		return NULL;
	}

	/* check whether the file is included in the files to add */

	{
		GList *scan;

		for (scan = self->files_to_add; scan; scan = scan->next) {
			if (_g_file_cmp_uris (G_FILE (scan->data), file) == 0) {
				dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
							        GTK_DIALOG_MODAL,
								NULL,
								_("Could not create the archive"),
								"%s",
								_("You can’t add an archive to itself."));
				gtk_dialog_run (GTK_DIALOG (dialog));

				gtk_widget_destroy (GTK_WIDGET (dialog));
				g_object_unref (parent_info);
				g_object_unref (file);

				return NULL;
			}
		}
	}

	/* overwrite confirmation */

	if (g_file_query_exists (file, NULL)) {
		char     *filename;
		char     *message;
		char     *secondary_message;
		gboolean  overwrite;

		filename = _g_file_get_display_basename (file);
		message = g_strdup_printf (_("A file named “%s” already exists.  Do you want to replace it?"), filename);
		secondary_message = g_strdup_printf (_("The file already exists in “%s”.  Replacing it will overwrite its contents."), g_file_info_get_display_name (parent_info));
		dialog = _gtk_message_dialog_new (GTK_WINDOW (self),
						  GTK_DIALOG_MODAL,
						  message,
						  secondary_message,
						  _GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						  _("_Replace"), GTK_RESPONSE_OK,
						  NULL);
		overwrite = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

		gtk_widget_destroy (dialog);
		g_free (secondary_message);
		g_free (message);
		g_free (filename);

		if (overwrite) {
			g_file_delete (file, NULL, &error);
			if (error != NULL) {
				dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
								GTK_DIALOG_MODAL,
								NULL,
								_("Could not delete the old archive."),
								"%s",
								error->message);
				gtk_dialog_run (GTK_DIALOG (dialog));

				gtk_widget_destroy (GTK_WIDGET (dialog));
				g_error_free (error);
				g_object_unref (parent_info);
				g_object_unref (file);

				return NULL;
			}
		}
		else
			g_clear_object (&file);
	}

	g_object_unref (parent_info);

	return file;
}


const char *
fr_new_archive_dialog_get_password (FrNewArchiveDialog *self)
{
	const char *password = NULL;
	int         n_format;

	n_format = get_selected_format (self);

	if (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT)
		password = (char*) gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("password_entry")));

	return password;
}


gboolean
fr_new_archive_dialog_get_encrypt_header (FrNewArchiveDialog *self)
{
	gboolean encrypt_header = FALSE;
	int      n_format;

	n_format = get_selected_format (self);

	if (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT) {
		const char *password = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("password_entry")));
		if (password != NULL) {
			if (strcmp (password, "") != 0) {
				if (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER)
					encrypt_header = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")));
			}
		}
	}

	return encrypt_header;
}


int
fr_new_archive_dialog_get_volume_size (FrNewArchiveDialog *self)
{
	guint volume_size = 0;
	int   n_format;

	n_format = get_selected_format (self);

	if ((mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES)
	    && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("volume_checkbutton"))))
	{
		double value;

		value = gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")));
		volume_size = floor (value * MEGABYTE);

	}

	return volume_size;
}
