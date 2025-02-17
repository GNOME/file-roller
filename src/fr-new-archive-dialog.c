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

typedef struct {
	int ext; // file_ext_type index
	int mime_type; // mime_type_desc index
} Extension;

struct _FrNewArchiveDialog {
	GtkDialog   parent_instance;
	GSettings  *settings;
	GtkBuilder *builder;
	int        *supported_types;
	GList      *valid_extensions; // GList<Extension>
	gboolean    can_encrypt;
	gboolean    can_encrypt_header;
	gboolean    can_create_volumes;
	GFile      *original_file;
	GList      *files_to_add;
	char       *filename;
	GFile      *folder;
	State       state;
	gboolean    filename_is_valid;
};


G_DEFINE_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, GTK_TYPE_DIALOG)


static void
fr_new_archive_dialog_finalize (GObject *object)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (object);

	g_list_foreach (self->valid_extensions, (GFunc) g_free, NULL);
	g_list_free (self->valid_extensions);
	_g_object_list_unref (self->files_to_add);
	_g_object_unref (self->original_file);
	g_free (self->filename);
	_g_object_unref (self->folder);
	g_object_unref (self->settings);
	g_object_unref (self->builder);

	G_OBJECT_CLASS (fr_new_archive_dialog_parent_class)->finalize (object);
}


static guint
get_selected_extension_idx (FrNewArchiveDialog *self)
{
	return adw_combo_row_get_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")));
}


static Extension*
get_selected_extension (FrNewArchiveDialog *self)
{
	guint idx = get_selected_extension_idx (self);
	return (idx != GTK_INVALID_LIST_POSITION) ? g_list_nth_data (self->valid_extensions, idx) : NULL;
}


static void
fr_new_archive_dialog_unmap (GtkWidget *widget)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (widget);

	g_settings_set_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER, gtk_switch_get_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch"))));
	g_settings_set_int (self->settings, PREF_NEW_VOLUME_SIZE, gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton"))) * MEGABYTE);

	Extension *selected_ext = get_selected_extension (self);
	if (selected_ext != NULL)
		g_settings_set_string (self->settings, PREF_NEW_DEFAULT_EXTENSION, file_ext_type[selected_ext->ext].ext);

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
	self->filename_is_valid = FALSE;
	self->valid_extensions = NULL;
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
	gtk_widget_set_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (self), GTK_RESPONSE_OK), self->filename_is_valid);
}


static void filename_changed_cb (GtkEditable *editable, gpointer user_data);


static void
update_filename_entry (FrNewArchiveDialog *self)
{
	g_signal_handlers_block_by_func (GET_WIDGET ("filename_row"), filename_changed_cb, self);
	if (self->filename != NULL) {
		gtk_editable_set_text (GTK_EDITABLE (GET_WIDGET ("filename_row")), self->filename);
	}
	else {
		gtk_editable_set_text (GTK_EDITABLE (GET_WIDGET ("filename_row")), "");
	}
	g_signal_handlers_unblock_by_func (GET_WIDGET ("filename_row"), filename_changed_cb, self);
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


static void update_from_selected_extension (FrNewArchiveDialog *self, guint ext_idx);


static gboolean
extension_is_valid_archive_extension (FrNewArchiveDialog *self, const char *ext, int *ext_idx)
{
	if (ext == NULL)
		return FALSE;
	int idx = 0;
	for (GList *scan = self->valid_extensions; scan; scan = scan->next) {
		Extension *valid_ext = scan->data;
		if (g_strcmp0 (ext, file_ext_type[valid_ext->ext].ext) == 0) {
			if (ext_idx != NULL)
				*ext_idx = idx;
			return TRUE;
		}
		idx++;
	}
	return FALSE;
}


static gboolean
set_file (FrNewArchiveDialog *self, GFile *file, GError **error)
{
	char *name = _g_file_get_display_name (file);
	if (name != NULL) {
		const char *ext = _g_filename_get_extension (name);
		int ext_idx;
		if (!extension_is_valid_archive_extension (self, ext, &ext_idx)) {
			*error = g_error_new_literal (
				FR_ERROR,
				FR_ERROR_UNSUPPORTED_FORMAT,
				_("Archive type not supported.")
			);
			return FALSE;
		}
		else {
			adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), ext_idx);
		}
	}

	_g_object_unref (self->folder);
	self->folder = g_file_get_parent (file);

	g_free (self->filename);
	self->filename = g_strdup (name);
	self->filename_is_valid = TRUE;

	update_filename_entry (self);
	update_folder_label (self);
	update_from_selected_extension (self, get_selected_extension_idx (self));

	return TRUE;
}


static gboolean
format_has_other_options (FrNewArchiveDialog *self)
{
	Extension *selected_ext = get_selected_extension (self);
	if (selected_ext == NULL)
		return FALSE;
	FrArchiveCaps capabilities = mime_type_desc[selected_ext->mime_type].capabilities;
	return (capabilities & FR_ARCHIVE_CAN_ENCRYPT) || (capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES);
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
		if (choosing_file && !format_has_other_options (self)) {
			// Save the last used format.
			Extension *selected_ext = get_selected_extension (self);
			if (selected_ext != NULL)
				g_settings_set_string (self->settings, PREF_NEW_DEFAULT_EXTENSION, file_ext_type[selected_ext->ext].ext);
			gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
		}
		else {
			gtk_window_present (GTK_WINDOW (self));
		}
	}

	g_object_unref (file);
}


static char *
get_basename (FrNewArchiveDialog *self)
{
	char *basename = NULL;
	const char *file_ext = _g_filename_get_extension (self->filename);
	if (extension_is_valid_archive_extension (self, file_ext, NULL)) {
		basename = g_strdup (self->filename);
	}
	else {
		Extension *selected_ext = get_selected_extension (self);
		if (selected_ext != NULL) {
			basename = g_strconcat (self->filename, file_ext_type[selected_ext->ext].ext, NULL);
		}
	}
	return basename;
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
	char *basename = get_basename (self);
	if (basename != NULL) {
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_sel), basename);
		g_free (basename);
	}
	_gtk_dialog_add_to_window_group (GTK_DIALOG (file_sel));
	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	g_signal_connect (GTK_FILE_CHOOSER_DIALOG (file_sel),
			  "response",
			  G_CALLBACK (file_chooser_response_cb),
			  self);
	gtk_window_present (GTK_WINDOW (file_sel));
}


static void
location_chooser_response_cb (GtkDialog *dialog,
			      int        response,
			      gpointer   user_data)
{
	FrNewArchiveDialog *self = user_data;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		return;
	}

	GFile *location = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
	if (location == NULL)
		return;

	_g_object_unref (self->folder);
	self->folder = g_object_ref (location);
	update_folder_label (self);

	g_object_unref (location);
	gtk_window_destroy (GTK_WINDOW (dialog));
}


static void
choose_location (FrNewArchiveDialog *self)
{
	GtkWidget *file_sel = gtk_file_chooser_dialog_new (
		C_("Window title", "New Archive"),
		gtk_window_get_transient_for (GTK_WINDOW (self)),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
		_GTK_LABEL_OPEN, GTK_RESPONSE_OK,
		NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel), self->folder, NULL);
	_gtk_dialog_add_to_window_group (GTK_DIALOG (file_sel));
	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	g_signal_connect (GTK_FILE_CHOOSER_DIALOG (file_sel),
			  "response",
			  G_CALLBACK (location_chooser_response_cb),
			  self);
	gtk_window_present (GTK_WINDOW (file_sel));
}


static void
update_from_selected_extension (FrNewArchiveDialog *self, guint ext_idx)
{
	Extension *ext = g_list_nth_data (self->valid_extensions, ext_idx);
	if (ext == NULL)
		return;
	FrArchiveCaps capabilities = mime_type_desc[ext->mime_type].capabilities;
	self->can_encrypt = capabilities & FR_ARCHIVE_CAN_ENCRYPT;
	self->can_encrypt_header = capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER;
	self->can_create_volumes = capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES;
	_fr_new_archive_dialog_update_sensitivity (self);
}


static void
filename_changed_cb (GtkEditable *editable,
		     gpointer     user_data)
{
	FrNewArchiveDialog *self = FR_NEW_ARCHIVE_DIALOG (user_data);

	const char *filename = gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("filename_row")));
	if ((g_strcmp0 (filename, self->filename) == 0) || (filename == self->filename)) {
		return;
	}

	g_free (self->filename);
	self->filename = g_strdup (filename);
	self->filename_is_valid = TRUE;

	if ((filename == NULL) || (*filename == 0)) {
		self->filename_is_valid = FALSE;
	}
	else {
		const char *ext = _g_filename_get_extension (filename);
		int active_extension_idx = 0;
		if (extension_is_valid_archive_extension (self, ext, &active_extension_idx)) {
			self->filename_is_valid = g_strcmp0 (self->filename, ext) != 0;
			adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), active_extension_idx);
			update_from_selected_extension (self, active_extension_idx);
		}
	}

	gtk_widget_set_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (self), GTK_RESPONSE_OK), self->filename_is_valid);
}


static void update_filename_from_extension_selector (FrNewArchiveDialog *self)
{
	Extension *selected_ext = get_selected_extension (self);
	if (selected_ext == NULL)
		return;

	bool changed = FALSE;
	const char *file_ext = _g_filename_get_extension (self->filename);
	const char *new_ext = file_ext_type[selected_ext->ext].ext;
	if (extension_is_valid_archive_extension (self, new_ext, NULL)
		&& (g_strcmp0 (file_ext, new_ext) != 0))
	{
		char *filename_no_ext = (file_ext != NULL) ? g_strndup (self->filename, strlen (self->filename) - strlen (file_ext)) : g_strdup (self->filename);
		if (filename_no_ext == NULL) {
			filename_no_ext = g_strdup ("");
		}
		char *tmp = g_strconcat (filename_no_ext, new_ext, NULL);
		g_free (filename_no_ext);
		g_free (self->filename);
		self->filename = tmp;
		self->filename_is_valid = g_strcmp0 (self->filename, new_ext) != 0;
		changed = TRUE;
	}
	if (changed) {
		update_filename_entry (self);
		update_from_selected_extension (self, get_selected_extension_idx (self));
	}
}


static void
combo_box_selected_notify_cb (GObject    *gobject,
			      GParamSpec *pspec,
			      gpointer    user_data)
{
	update_filename_from_extension_selector (FR_NEW_ARCHIVE_DIALOG (user_data));
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

	/* Folder */

	self->folder = (folder != NULL) ? g_object_ref (folder) : _g_file_get_home ();
	update_folder_label (self);

	/* Extension */

	GtkStringList *extension_list = (GtkStringList *) gtk_builder_get_object (self->builder, "extension_list");
	char *active_extension = g_settings_get_string (self->settings, PREF_NEW_DEFAULT_EXTENSION);
	int active_extension_idx = 0;
	int ext_idx = 0;
	for (int i = 0; file_ext_type[i].ext != NULL; i++) {
		FrExtensionType *ext = file_ext_type + i;
		for (int j = 0; self->supported_types[j] != -1; j++) {
			const char *mime_type = mime_type_desc[self->supported_types[j]].mime_type;
			if (strcmp (ext->mime_type, mime_type) == 0) {
				Extension *valid_ext = g_new0 (Extension, 1);
				valid_ext->ext = i;
				valid_ext->mime_type = self->supported_types[j];
				self->valid_extensions = g_list_prepend (self->valid_extensions, valid_ext);
				if (strcmp (active_extension, ext->ext) == 0) {
					active_extension_idx = ext_idx;
				}
				gtk_string_list_append (extension_list, ext->ext);
				ext_idx++;
				break;
			}
		}
	}
	self->valid_extensions = g_list_reverse (self->valid_extensions);
	adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), active_extension_idx);
	g_free (active_extension);

	/* Filename */

	if (default_name != NULL) {
		if (action == FR_NEW_ARCHIVE_ACTION_SAVE_AS) {
			/* Give priority to the selected extension, changing the default name if needed. */
			self->filename = g_strdup (default_name);
			update_filename_from_extension_selector (self);
		}
		else {
			/* Give priority to the file name, changing the selected extension if it is not the same. */
			const char *ext = _g_filename_get_extension (default_name);
			int ext_idx;
			if (!extension_is_valid_archive_extension (self, ext, &ext_idx)) {
				/* If the extension is not specified use the last selected extension. */
				Extension *valid_ext = g_list_nth_data (self->valid_extensions, active_extension_idx);
				if (valid_ext != NULL) {
					ext = file_ext_type[valid_ext->ext].ext;
					self->filename = g_strconcat (default_name, ext, NULL);
				}
			}
			else {
				/* Update the selected extension. */
				adw_combo_row_set_selected (ADW_COMBO_ROW (GET_WIDGET ("extension_combo_row")), ext_idx);
				self->filename = g_strdup (default_name);
			}
		}
		self->filename_is_valid = self->filename != NULL;
		update_filename_entry (self);
	}
	else {
		// No filename
		self->filename_is_valid = FALSE;
		gtk_editable_set_text (GTK_EDITABLE (GET_WIDGET ("filename_row")), "");
	}
	gtk_widget_set_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (self), GTK_RESPONSE_OK), self->filename_is_valid);

	/* Encrypt */

	adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("encrypt_archive_expander_row")), FALSE);
	gtk_switch_set_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch")), g_settings_get_boolean (self->settings, PREF_NEW_ENCRYPT_HEADER));

	/* Volumes */

	adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("split_in_volumes_expander_row")), FALSE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")),
				   (double) g_settings_get_int (self->settings, PREF_NEW_VOLUME_SIZE) / MEGABYTE);

	/* Signals */

	g_signal_connect (GET_WIDGET ("filename_row"),
			  "changed",
			  G_CALLBACK (filename_changed_cb),
			  self);
	g_signal_connect (GET_WIDGET ("extension_combo_row"),
			  "notify::selected",
			  G_CALLBACK (combo_box_selected_notify_cb),
			  self);
	g_signal_connect_swapped (GET_WIDGET ("choose_filename_button"),
				  "clicked",
				  G_CALLBACK (choose_file),
				  self);
	g_signal_connect_swapped (GET_WIDGET ("choose_location_button"),
				  "clicked",
				  G_CALLBACK (choose_location),
				  self);

	update_from_selected_extension (self, get_selected_extension_idx (self));
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
	int mime_type;
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

	gtk_window_destroy (GTK_WINDOW (dialog));

	if ((response_id == GTK_RESPONSE_CANCEL) || (response_id == GTK_RESPONSE_DELETE_EVENT)) {
		data->callback (data->dialog, NULL, NULL, data->user_data);
		overwrite_dialog_data_free (data);
		return;
	}

	if (response_id == GTK_RESPONSE_OK) {
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
			mime_type_desc[data->mime_type].mime_type,
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
fr_new_archive_dialog_show_options (FrNewArchiveDialog *self)
{
	self->state = STATE_OPTIONS;
	gtk_window_present (GTK_WINDOW (self));
}


static GFile*
get_selected_file (FrNewArchiveDialog  *self,
		   GError             **error)
{
	char *basename = get_basename (self);
	if (basename == NULL) {
		*error = g_error_new_literal (
			FR_ERROR,
			FR_ERROR_UNSUPPORTED_FORMAT,
			_("Archive type not supported.")
		);
		return NULL;
	}
	GFile *file = g_file_get_child_for_display_name (self->folder, basename, error);

	g_free (basename);
	return file;
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
	GFile *file = get_selected_file (self, &error);
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

	Extension *selected_ext = get_selected_extension (self);
	if (selected_ext == NULL) {
		callback (self, NULL, NULL, user_data);
		return;
	}

	if (g_file_query_exists (file, NULL)) {
		char *filename;
		char *message;
		char *secondary_message;

		filename = _g_file_get_display_name (file);
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
		overwrite_data->mime_type = selected_ext->mime_type;
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
	else {
		callback (self,
			  file,
			  mime_type_desc[selected_ext->mime_type].mime_type,
			  user_data);
	}

	g_object_unref (parent_info);
}


const char *
fr_new_archive_dialog_get_password (FrNewArchiveDialog *self)
{
	const char *password = NULL;

	Extension *selected_ext = get_selected_extension (self);
	if ((selected_ext != NULL) && (mime_type_desc[selected_ext->mime_type].capabilities & FR_ARCHIVE_CAN_ENCRYPT))
		password = (char*) gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("password_entry_row")));

	return password;
}


gboolean
fr_new_archive_dialog_get_encrypt_header (FrNewArchiveDialog *self)
{
	gboolean encrypt_header = FALSE;

	Extension *selected_ext = get_selected_extension (self);
	if ((selected_ext != NULL) && (mime_type_desc[selected_ext->mime_type].capabilities & FR_ARCHIVE_CAN_ENCRYPT)) {
		const char *password = gtk_editable_get_text (GTK_EDITABLE (GET_WIDGET ("password_entry_row")));
		if ((password != NULL) && (strcmp (password, "") != 0)) {
			if (mime_type_desc[selected_ext->mime_type].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER)
				encrypt_header = gtk_switch_get_state (GTK_SWITCH (GET_WIDGET ("encrypt_header_switch")));
		}
	}

	return encrypt_header;
}


int
fr_new_archive_dialog_get_volume_size (FrNewArchiveDialog *self)
{
	guint volume_size = 0;

	Extension *selected_ext = get_selected_extension (self);
	if ((selected_ext != NULL) &&
		(mime_type_desc[selected_ext->mime_type].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES) &&
		adw_expander_row_get_enable_expansion (ADW_EXPANDER_ROW (GET_WIDGET ("split_in_volumes_expander_row"))))
	{
		double value = gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")));
		volume_size = floor (value * MEGABYTE);
	}

	return volume_size;
}
