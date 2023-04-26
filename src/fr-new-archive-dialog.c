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
#include <adwaita.h>
#include "file-utils.h"
#include "fr-init.h"
#include "fr-location-button.h"
#include "fr-new-archive-dialog.h"
#include "gio-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_WIDGET(x)		(_gtk_builder_get_widget (self->builder, (x)))
#define MEGABYTE		(1024 * 1024)
#define ARCHIVE_ICON_SIZE	48


typedef enum {
	STATE_FILENAME,
	STATE_OPTIONS,
} State;


struct _FrNewArchiveDialog {
	GtkDialog   parent_instance;
	GSettings  *settings;
	GtkBuilder *builder;
	int        *supported_types;
	gboolean    can_encrypt;
	gboolean    can_encrypt_header;
	gboolean    can_create_volumes;
	GFile      *original_file;
	GList      *files_to_add;
	char       *filename;
	GFile      *folder;
	State       state;
};


G_DEFINE_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, GTK_TYPE_DIALOG)


static void
fr_new_archive_dialog_finalize (GObject *object)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (object);

	_g_object_list_unref (self->files_to_add);
	_g_object_unref (self->original_file);
	g_free (self->filename);
	_g_object_unref (self->folder);
	g_object_unref (self->settings);
	g_object_unref (self->builder);

	G_OBJECT_CLASS (fr_new_archive_dialog_parent_class)->finalize (object);
}


static int
get_selected_format (FrNewArchiveDialog *self)
{
	guint idx = adw_combo_row_get_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")));
	return (idx != GTK_INVALID_LIST_POSITION) ? self->supported_types[idx] : -1;
}


static void
fr_new_archive_dialog_unmap (GtkWidget *widget)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (widget);

	g_settings_set_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER, gtk_switch_get_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch"))));
	g_settings_set_int (self->settings, PREF_NEW_VOLUME_SIZE, gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton"))) * MEGABYTE);

	int n_format = get_selected_format (self);
	if (n_format >= 0)
		g_settings_set_string (self->settings, PREF_NEW_DEFAULT_EXTENSION, mime_type_desc[n_format].default_ext);

	GTK_WIDGET_CLASS (fr_new_archive_dialog_parent_class)->unmap (widget);
}


static void choose_file (FrNewArchiveDialog *self);


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
	self->original_file = NULL;
	self->files_to_add = NULL;
	self->filename = NULL;
	self->folder = NULL;
	self->state = STATE_FILENAME;
}


static void
_fr_new_archive_dialog_update_sensitivity (FrNewArchiveDialog *self)
{
	if (!self->can_encrypt)
		adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("encrypt_archive_expander_row")), FALSE);
	if (!self->can_create_volumes)
		adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("split_in_volumes_expander_row")), FALSE);
	if (!self->can_encrypt_header)
		gtk_switch_set_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch")), FALSE);
	gtk_widget_set_sensitive (GET_WIDGET ("encrypt_archive_expander_row"), self->can_encrypt);
	gtk_widget_set_sensitive (GET_WIDGET ("password_entry_row"), self->can_encrypt);
	gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_row"), self->can_encrypt_header);
	gtk_widget_set_sensitive (GET_WIDGET ("volume_group"), self->can_create_volumes);
	gtk_widget_set_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (self), GTK_RESPONSE_OK), self->filename != NULL);
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
update_filename_label (FrNewArchiveDialog *self)
{
	if (self->filename != NULL)
		adw_action_row_set_subtitle (ADW_ACTION_ROW (GET_WIDGET ("filename_row")), self->filename);
	else
		adw_action_row_set_subtitle (ADW_ACTION_ROW (GET_WIDGET ("filename_row")), "");
}


static void
update_folder_label (FrNewArchiveDialog *self)
{
	char *location_name = _g_file_get_display_name (self->folder);
	if (location_name != NULL) {
		gtk_label_set_text (GTK_LABEL (GET_WIDGET ("location_name")), location_name);
		g_free (location_name);
	}
	else {
		gtk_label_set_text (GTK_LABEL (GET_WIDGET ("location_name")), "");
	}
}


static void update_from_selected_format (FrNewArchiveDialog *self, int n_format);


static gboolean
set_file (FrNewArchiveDialog *self, GFile *file, GError **error)
{
	gboolean result = TRUE;

	int active_extension_idx = -1;
	char *name = _g_file_get_display_basename (file);
	if (name != NULL) {
		const char *ext = _g_filename_get_extension (name);
		if (ext == NULL) {
			/* If the extension is not specified use the last selected extension. */
			int n_format = get_selected_format (self);
			if (n_format >= 0) {
				ext = mime_type_desc[n_format].default_ext;

				char *tmp = g_strconcat (name, ext, NULL);
				g_free (name);
				name = tmp;
			}
		}
		if (ext != NULL) {
			for (int i = 0; self->supported_types[i] != -1; i++) {
				if (strcmp (ext, mime_type_desc[self->supported_types[i]].default_ext) == 0) {
					active_extension_idx = i;
					break;
				}
			}
		}
	}

	if (active_extension_idx == -1) {
		result = FALSE;
		*error = g_error_new_literal (FR_ERROR,
					      FR_ERROR_UNSUPPORTED_FORMAT,
					      _("Archive type not supported."));
	}
	else {
		result = TRUE;

		_g_object_unref (self->folder);
		self->folder = g_file_get_parent (file);

		g_free (self->filename);
		self->filename = g_strdup (name);

		adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), active_extension_idx);
		update_filename_label (self);
		update_folder_label (self);
		update_from_selected_format (self, self->supported_types[active_extension_idx]);
	}

	g_free (name);

	return result;
}


static gboolean
format_has_other_options (FrNewArchiveDialog *self)
{
	int n_format = get_selected_format (self);
	return (n_format >= 0) && ((mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT) ||
		(mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES));
}


static void
file_chooser_response_cb (GtkDialog *dialog,
			  int        response,
			  gpointer   user_data)
{
	FrNewArchiveDialog *self = user_data;
	gboolean choosing_file = self->state == STATE_FILENAME;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		if (choosing_file)
			gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_CANCEL);
		return;
	}

	GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
	if (file == NULL)
		return;

	GError *error = NULL;
	if (!set_file (self, file, &error)) {
		_gtk_error_dialog_run (GTK_WINDOW (dialog),
				       _("Could not perform the operation"),
				       "%s",
				       error->message);
		g_error_free (error);
	}
	else {
		self->state = STATE_OPTIONS;
		gtk_window_destroy (GTK_WINDOW (dialog));
		if (choosing_file && !format_has_other_options (self))
			gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
		else
			gtk_window_present (GTK_WINDOW (self));
	}

	g_object_unref (file);
}


static void
choose_file (FrNewArchiveDialog *self)
{
	GtkWidget *file_sel = gtk_file_chooser_dialog_new (
		C_("Window title", "New Archive"),
		gtk_window_get_transient_for (GTK_WINDOW (self)),
		GTK_FILE_CHOOSER_ACTION_SAVE,
		_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
		_GTK_LABEL_CREATE_ARCHIVE, GTK_RESPONSE_OK,
		NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel), self->folder, NULL);
	if (self->filename != NULL)
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_sel), self->filename);
	_gtk_dialog_add_to_window_group (GTK_DIALOG (file_sel));
	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	g_signal_connect (GTK_FILE_CHOOSER_DIALOG (file_sel),
			  "response",
			  G_CALLBACK (file_chooser_response_cb),
			  self);
	gtk_window_present (GTK_WINDOW (file_sel));
}


static void
update_from_selected_format (FrNewArchiveDialog *self, int n_format)
{
	if (n_format < 0)
		n_format = get_selected_format (self);
	if (n_format < 0)
		return;
	self->can_encrypt = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT;
	self->can_encrypt_header = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	self->can_create_volumes = mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES;
	_fr_new_archive_dialog_update_sensitivity (self);
}


static void
combo_box_selected_notify_cb (GObject    *gobject,
			      GParamSpec *pspec,
			      gpointer    user_data)
{
	FrNewArchiveDialog *self = FR_NEW_ARCHIVE_DIALOG (user_data);

	if (self->filename != NULL) {
		int n_format = get_selected_format (self);
		if (n_format >= 0) {
			bool changed = FALSE;
			const char *ext = _g_filename_get_extension (self->filename);
			if (g_strcmp0 (ext, mime_type_desc[n_format].default_ext) != 0) {
				char *filename_no_ext = _g_path_remove_extension_if_archive (self->filename);
				char *tmp = g_strconcat (filename_no_ext, mime_type_desc[n_format].default_ext, NULL);
				g_free (filename_no_ext);
				g_free (self->filename);
				self->filename = tmp;
				changed = TRUE;
			}
			if (changed) {
				update_filename_label (self);
				update_from_selected_format (self, n_format);
			}
		}
	}
}


static void
_fr_new_archive_dialog_construct (FrNewArchiveDialog *self,
				  GtkWindow          *parent,
				  FrNewArchiveAction  action,
				  GFile              *folder,
				  const char         *default_name,
				  GFile              *original_file)
{
	gtk_window_set_transient_for (GTK_WINDOW (self), parent);
	gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
	gtk_window_set_default_size (GTK_WINDOW (self), 500, -1);

	self->builder = gtk_builder_new_from_resource (FILE_ROLLER_RESOURCE_UI_PATH "new-archive-dialog.ui");
	gtk_box_append (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self))), GET_WIDGET ("content"));

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

	fr_sort_mime_types_by_extension (self->supported_types);

	_g_object_unref (self->original_file);
	self->original_file = _g_object_ref (original_file);

	/* Set widgets data. */

	/* Filename */

	if (default_name != NULL)
		self->filename = g_strdup (default_name);
	update_filename_label (self);

	/* Folder */

	self->folder = (folder != NULL) ? g_object_ref (folder) : _g_file_get_home ();
	update_folder_label (self);

	/* Extension */

	GtkStringList *extension_list = (GtkStringList *) gtk_builder_get_object (self->builder, "extension_list");
	char *active_extension = g_settings_get_string (self->settings, PREF_NEW_DEFAULT_EXTENSION);
	int active_extension_idx = 0;
	for (int i = 0; self->supported_types[i] != -1; i++) {
		if (strcmp (active_extension, mime_type_desc[self->supported_types[i]].default_ext) == 0)
			active_extension_idx = i;
		gtk_string_list_append (extension_list, mime_type_desc[self->supported_types[i]].default_ext);
	}
	adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), active_extension_idx);
	g_free (active_extension);

	/* Encrypt */

	adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("encrypt_archive_expander_row")), FALSE);
	gtk_switch_set_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch")), g_settings_get_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER));

	/* Volumes */

	adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("split_in_volumes_expander_row")), FALSE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")),
				   (double) g_settings_get_int (self->settings, PREF_NEW_VOLUME_SIZE) / MEGABYTE);

	/* Signals */

	g_signal_connect (GET_WIDGET ("extension_combo_row"),
			  "notify::selected",
			  G_CALLBACK (combo_box_selected_notify_cb),
			  self);
	g_signal_connect_swapped (GET_WIDGET ("filename_row"),
				  "activated",
				  G_CALLBACK (choose_file),
				  self);

	update_from_selected_format (self, -1);
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


/* -- fr_new_archive_dialog_get_file -- */


typedef struct {
	FrNewArchiveDialog *dialog;
	FrNewArchiveDialogCallback callback;
	GFile *file;
	int n_format;
	gpointer user_data;
} OverwriteDialogData;


static void
overwrite_dialog_data_free (OverwriteDialogData *data)
{
	_g_object_unref (data->file);
	g_free (data);
}


static void
overwrite_dialog_response_cb (GtkDialog *dialog,
			      int        response_id,
			      gpointer   user_data)
{
	OverwriteDialogData *data = user_data;
	gboolean overwrite = (response_id == GTK_RESPONSE_OK);

	gtk_window_destroy (GTK_WINDOW (dialog));

	if (overwrite) {
		GError *error = NULL;

		g_file_delete (data->file, NULL, &error);
		if (error != NULL) {
			GtkWidget *dialog = _gtk_error_dialog_new (
				GTK_WINDOW (data->dialog),
				GTK_DIALOG_MODAL,
				NULL,
				_("Could not delete the old archive."),
				"%s",
				error->message);
			_gtk_dialog_run (GTK_DIALOG (dialog));

			g_error_free (error);

			data->callback (data->dialog, NULL, NULL, data->user_data);
			overwrite_dialog_data_free (data);
			return;
		}
	}

	data->callback (data->dialog,
			data->file,
			mime_type_desc[data->n_format].mime_type,
			data->user_data);
	overwrite_dialog_data_free (data);
}


void
fr_new_archive_dialog_show (FrNewArchiveDialog *self)
{
	if (self->state == STATE_FILENAME)
		choose_file (self);
	else
		gtk_window_present (GTK_WINDOW (self));
}


void
fr_new_archive_dialog_get_file (FrNewArchiveDialog  *self,
				FrNewArchiveDialogCallback callback,
				gpointer user_data)
{
	if (self->filename == NULL) {
		GtkWidget *msg_dialog = _gtk_error_dialog_new (
			GTK_WINDOW (self),
			GTK_DIALOG_MODAL,
			NULL,
			_("Could not create the archive"),
			"%s",
			_("You have to specify an archive name."));
		_gtk_dialog_run (GTK_DIALOG (msg_dialog));

		callback (self, NULL, NULL, user_data);
		return;
	}

	GError *error = NULL;
	GFile *file = g_file_get_child_for_display_name (self->folder, self->filename, &error);
	if (file == NULL) {
		GtkWidget *msg_dialog = _gtk_error_dialog_new (
			GTK_WINDOW (self),
			GTK_DIALOG_MODAL,
			NULL,
			_("Could not create the archive"),
			"%s",
			error->message);
		_gtk_dialog_run (GTK_DIALOG (msg_dialog));

		g_error_free (error);

		callback (self, NULL, NULL, user_data);
		return;
	}

	/* Check permissions */

	GFileInfo *parent_info = g_file_query_info (self->folder,
		(G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
		 G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
		 G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE","
		 G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME),
		0,
		NULL,
		&error);

	if (error != NULL) {
		g_warning ("Failed to get permission for extraction dir: %s", error->message);

		g_clear_error (&error);
		g_object_unref (parent_info);
		g_object_unref (file);

		callback (self, NULL, NULL, user_data);
		return;
	}

	if (g_file_info_has_attribute (parent_info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE) &&
	    ! g_file_info_get_attribute_boolean (parent_info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE))
	{
		GtkWidget *msg_dialog = _gtk_error_dialog_new (
			GTK_WINDOW (self),
			GTK_DIALOG_MODAL,
			NULL,
			_("Could not create the archive"),
			"%s",
			_("You don’t have permission to create an archive in this folder"));
		_gtk_dialog_run (GTK_DIALOG (msg_dialog));

		g_object_unref (parent_info);
		g_object_unref (file);

		callback (self, NULL, NULL, user_data);
		return;
	}

	/* Check whehter the file is equal to the original file */

	if ((self->original_file != NULL) && (g_file_equal (file, self->original_file))) {
		GtkWidget *msg_dialog = _gtk_error_dialog_new (
			GTK_WINDOW (self),
			GTK_DIALOG_MODAL,
			NULL,
			_("Could not create the archive"),
			"%s",
			_("New name is the same as old one, please type other name."));
		_gtk_dialog_run (GTK_DIALOG (msg_dialog));

		g_object_unref (parent_info);
		g_object_unref (file);

		callback (self, NULL, NULL, user_data);
		return;
	}

	/* Check whether the file is included in the files to add. */

	{
		GList *scan;

		for (scan = self->files_to_add; scan; scan = scan->next) {
			if (_g_file_cmp_uris (G_FILE (scan->data), file) == 0) {
				GtkWidget *msg_dialog = _gtk_error_dialog_new (
					GTK_WINDOW (self),
					GTK_DIALOG_MODAL,
					NULL,
					_("Could not create the archive"),
					"%s",
					_("You can’t add an archive to itself."));
				_gtk_dialog_run (GTK_DIALOG (msg_dialog));

				g_object_unref (parent_info);
				g_object_unref (file);

				callback (self, NULL, NULL, user_data);
				return;
			}
		}
	}

	/* Overwrite confirmation. */

	int n_format = get_selected_format (self);
	if (g_file_query_exists (file, NULL)) {
		char     *filename;
		char     *message;
		char     *secondary_message;

		filename = _g_file_get_display_basename (file);
		message = g_strdup_printf (_("A file named “%s” already exists.  Do you want to replace it?"), filename);
		secondary_message = g_strdup_printf (_("The file already exists in “%s”.  Replacing it will overwrite its contents."), g_file_info_get_display_name (parent_info));
		GtkWidget *msg_dialog = _gtk_message_dialog_new (
			GTK_WINDOW (self),
			GTK_DIALOG_MODAL,
			message,
			secondary_message,
			_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
			_("_Replace"), GTK_RESPONSE_OK,
			NULL);

		OverwriteDialogData *overwrite_data = g_new0 (OverwriteDialogData, 1);
		overwrite_data->dialog = self;
		overwrite_data->file = g_object_ref (file);
		overwrite_data->n_format = n_format;
		overwrite_data->callback = callback;
		overwrite_data->user_data = user_data;
		g_signal_connect (msg_dialog,
				  "response",
				  G_CALLBACK (overwrite_dialog_response_cb),
				  overwrite_data);
		gtk_window_present (GTK_WINDOW (msg_dialog));

		g_free (secondary_message);
		g_free (message);
		g_free (filename);
	}
	else
		callback (self,
			  file,
			  mime_type_desc[n_format].mime_type,
			  user_data);

	g_object_unref (parent_info);
}


const char *
fr_new_archive_dialog_get_password (FrNewArchiveDialog *self)
{
	const char *password = NULL;
	int         n_format;

	n_format = get_selected_format (self);
	if ((n_format >= 0) && (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT))
		password = (char*) gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("password_entry_row")));

	return password;
}


gboolean
fr_new_archive_dialog_get_encrypt_header (FrNewArchiveDialog *self)
{
	gboolean encrypt_header = FALSE;

	int n_format = get_selected_format (self);
	if ((n_format >= 0) && (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT)) {
		const char *password = gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("password_entry_row")));
		if ((password != NULL) && (strcmp (password, "") != 0)) {
			if (mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER)
				encrypt_header = gtk_switch_get_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch")));
		}
	}

	return encrypt_header;
}


int
fr_new_archive_dialog_get_volume_size (FrNewArchiveDialog *self)
{
	guint volume_size = 0;

	int n_format = get_selected_format (self);
	if ((n_format >= 0) &&
		(mime_type_desc[n_format].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES) &&
		adw_expander_row_get_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("split_in_volumes_expander_row"))))
	{
		double value = gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")));
		volume_size = floor (value * MEGABYTE);
	}

	return volume_size;
}
