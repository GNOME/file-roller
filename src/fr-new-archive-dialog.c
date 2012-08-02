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
#include "fr-stock.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_WIDGET(x)       (_gtk_builder_get_widget (self->priv->builder, (x)))
#define DEFAULT_EXTENSION   ".tar.gz"
#define MEGABYTE            (1024 * 1024)
#define MIME_TYPE_INDEX_KEY "fr-mime-type-idx"


struct _FrNewArchiveDialogPrivate {
	GtkBuilder *builder;
	int        *supported_types;
	GHashTable *supported_ext;
	gboolean    can_encrypt;
	gboolean    can_encrypt_header;
	gboolean    can_create_volumes;
};


G_DEFINE_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, GTK_TYPE_FILE_CHOOSER_DIALOG)


static void
fr_new_archive_dialog_finalize (GObject *object)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (object);

	g_object_unref (self->priv->builder);
	g_hash_table_unref (self->priv->supported_ext);

	G_OBJECT_CLASS (fr_new_archive_dialog_parent_class)->finalize (object);
}


static void
fr_new_archive_dialog_class_init (FrNewArchiveDialogClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (FrNewArchiveDialogPrivate));

	object_class = (GObjectClass*) klass;
	object_class->finalize = fr_new_archive_dialog_finalize;
}


static void
fr_new_archive_dialog_init (FrNewArchiveDialog *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_NEW_ARCHIVE_DIALOG, FrNewArchiveDialogPrivate);
	self->priv->builder = NULL;
	self->priv->supported_ext = g_hash_table_new (g_str_hash, g_str_equal);
}


static void
_fr_new_archive_dialog_update_sensitivity (FrNewArchiveDialog *self)
{
	gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")), ! self->priv->can_encrypt_header);
	gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), self->priv->can_encrypt_header);
	gtk_widget_set_sensitive (GET_WIDGET ("volume_spinbutton"), ! self->priv->can_create_volumes || gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("volume_checkbutton"))));
}


static void
_fr_new_archive_dialog_update_sensitivity_for_ext (FrNewArchiveDialog *self,
						   const char         *ext)
{
	const char *mime_type;
	int         i;

	self->priv->can_encrypt = FALSE;
	self->priv->can_encrypt_header = FALSE;
	self->priv->can_create_volumes = FALSE;

	mime_type = get_mime_type_from_extension (ext);

	if (mime_type == NULL) {
		gtk_widget_set_sensitive (GET_WIDGET ("password_entry"), FALSE);
		gtk_widget_set_sensitive (GET_WIDGET ("password_label"), FALSE);
		gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), FALSE);
		gtk_widget_set_sensitive (GET_WIDGET ("volume_box"), FALSE);
		return;
	}

	for (i = 0; mime_type_desc[i].mime_type != NULL; i++) {
		if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0) {
			self->priv->can_encrypt = mime_type_desc[i].capabilities & FR_ARCHIVE_CAN_ENCRYPT;
			gtk_widget_set_sensitive (GET_WIDGET ("password_entry"), self->priv->can_encrypt);
			gtk_widget_set_sensitive (GET_WIDGET ("password_label"), self->priv->can_encrypt);

			self->priv->can_encrypt_header = mime_type_desc[i].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER;
			gtk_widget_set_sensitive (GET_WIDGET ("encrypt_header_checkbutton"), self->priv->can_encrypt_header);

			self->priv->can_create_volumes = mime_type_desc[i].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES;
			gtk_widget_set_sensitive (GET_WIDGET ("volume_box"), self->priv->can_create_volumes);

			break;
		}
	}

	_fr_new_archive_dialog_update_sensitivity (self);
}


static void
password_entry_changed_cb (GtkEditable *editable,
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


static int
_fr_new_archive_dialog_get_format (FrNewArchiveDialog *self,
				   const char         *uri)
{
	GtkFileFilter *filter;
	int            idx;

	filter = gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (self));
	if (filter == NULL) {
		const char *ext;

		/* get the format from the extension */

		ext = _g_filename_get_extension (uri);
		idx = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->supported_ext, ext));

		return idx - 1;
	}

	idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (filter), MIME_TYPE_INDEX_KEY));
	return idx - 1;
}


static void
filter_notify_cb (GObject    *gobject,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
	FrNewArchiveDialog *self = user_data;
	const char         *uri;
	const char         *ext;
	int                 n_format;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self));
	if (uri == NULL)
		return;

	n_format = _fr_new_archive_dialog_get_format (self, uri);
	if (n_format < 0)
		return;

	ext = get_archive_filename_extension (uri);
	if (ext == NULL)
		ext = mime_type_desc[self->priv->supported_types[n_format]].default_ext;

	_fr_new_archive_dialog_update_sensitivity_for_ext (self, ext);

	if (uri != NULL) {
		const char *new_ext;
		const char *basename;
		char       *basename_noext;
		char       *new_basename;
		char       *new_basename_uft8;

		new_ext = mime_type_desc[self->priv->supported_types[n_format]].default_ext;
		basename = _g_path_get_file_name (uri);
		if (g_str_has_suffix (basename, ext))
			basename_noext = g_strndup (basename, strlen (basename) - strlen (ext));
		else
			basename_noext = g_strdup (basename);
		new_basename = g_strconcat (basename_noext, new_ext, NULL);
		new_basename_uft8 = g_uri_unescape_string (new_basename, NULL);

		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (self), new_basename_uft8);
		_fr_new_archive_dialog_update_sensitivity_for_ext (self, new_ext);

		g_free (new_basename_uft8);
		g_free (new_basename);
		g_free (basename_noext);
	}
}


static gboolean
all_supported_files_filter_func (const GtkFileFilterInfo *filter_info,
				 gpointer                 data)
{
	FrNewArchiveDialog *self = data;
	const char         *ext;

	ext = _g_filename_get_extension (filter_info->uri);
	if (ext != NULL)
		return g_hash_table_lookup (self->priv->supported_ext, ext) != NULL;
	else
		return FALSE;
}


static void
_fr_new_archive_dialog_construct (FrNewArchiveDialog *self,
				  GtkWindow          *parent,
				  FrNewArchiveAction  action,
				  const char         *default_name)
{
	GSettings     *settings;
	GtkFileFilter *filter;
	int            i;

	gtk_file_chooser_set_action (GTK_FILE_CHOOSER (self), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (self), FALSE);
	gtk_window_set_modal (GTK_WINDOW (self), TRUE);
	gtk_window_set_transient_for (GTK_WINDOW (self), parent);

	self->priv->builder = _gtk_builder_new_from_resource ("new-archive-dialog-options.ui");
	if (self->priv->builder == NULL)
		return;

	gtk_dialog_add_button (GTK_DIALOG (self), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	switch (action) {
	case FR_NEW_ARCHIVE_ACTION_NEW:
		self->priv->supported_types = create_type;
		gtk_window_set_title (GTK_WINDOW (self), C_("File", "New"));
		gtk_dialog_add_button (GTK_DIALOG (self), FR_STOCK_CREATE_ARCHIVE, GTK_RESPONSE_OK);
		break;
	case FR_NEW_ARCHIVE_ACTION_SAVE_AS:
		self->priv->supported_types = save_type;
		gtk_window_set_title (GTK_WINDOW (self), C_("File", "Save"));
		gtk_dialog_add_button (GTK_DIALOG (self), GTK_STOCK_SAVE, GTK_RESPONSE_OK);
		break;
	}
	gtk_dialog_set_default_response (GTK_DIALOG (self), GTK_RESPONSE_OK);

	sort_mime_types_by_description (self->priv->supported_types);

	/* Set widgets data. */

	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (self), GET_WIDGET ("extra_widget"));

	if (default_name != NULL)
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (self), default_name);

	/**/

	gtk_expander_set_expanded (GTK_EXPANDER (GET_WIDGET ("other_options_expander")), FALSE);

	settings = g_settings_new (FILE_ROLLER_SCHEMA_GENERAL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("encrypt_header_checkbutton")), g_settings_get_boolean (settings, PREF_GENERAL_ENCRYPT_HEADER));
	g_object_unref (settings);

	settings = g_settings_new (FILE_ROLLER_SCHEMA_BATCH_ADD);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (GET_WIDGET("volume_spinbutton")), (double) g_settings_get_int (settings, PREF_BATCH_ADD_VOLUME_SIZE) / MEGABYTE);
	g_object_unref (settings);

	/* file filter */

	/* all files */

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_filter_set_name (filter, _("All Files"));
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (self), filter);

	/* all supported files */

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_custom (filter,
				    GTK_FILE_FILTER_URI,
				    all_supported_files_filter_func,
				    self,
				    NULL);
	gtk_file_filter_set_name (filter, _("All Supported Files"));
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (self), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (self), filter);

	for (i = 0; self->priv->supported_types[i] != -1; i++) {
		int      idx;
		GString *name;
		int      n_ext;
		int      e;

		filter = gtk_file_filter_new ();
		g_object_set_data (G_OBJECT (filter), MIME_TYPE_INDEX_KEY, GINT_TO_POINTER (i + 1));

		idx = self->priv->supported_types[i];
		name = g_string_new (_(mime_type_desc[idx].name));
		g_string_append (name, " (");

		n_ext = 0;
		for (e = 0; file_ext_type[e].ext != NULL; e++) {
			if (_g_str_equal (file_ext_type[e].mime_type, mime_type_desc[idx].mime_type)) {
				char *ext;
				char *pattern;

				ext = file_ext_type[e].ext;
				g_hash_table_insert (self->priv->supported_ext, ext, GINT_TO_POINTER (idx + 1));
				n_ext++;

				if (n_ext > 1)
					g_string_append (name, ", ");
				g_string_append (name, "*");
				g_string_append (name, ext);

				pattern = g_strdup_printf ("*%s", ext);
				gtk_file_filter_add_pattern (filter, pattern);

				g_free (pattern);
			}
		}

		g_string_append (name, ")");
		gtk_file_filter_set_name (filter, name->str);
		gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (self), filter);

		g_string_free (name, TRUE);
	}

	gtk_widget_set_vexpand (GET_WIDGET ("extra_widget"), FALSE);

	_fr_new_archive_dialog_update_sensitivity (self);

	/* Set the signals handlers. */

	g_signal_connect (GET_WIDGET ("password_entry"),
			  "changed",
			  G_CALLBACK (password_entry_changed_cb),
			  self);
	g_signal_connect (GET_WIDGET ("volume_checkbutton"),
			  "toggled",
			  G_CALLBACK (volume_toggled_cb),
			  self);
	g_signal_connect (self,
			  "notify::filter",
			  G_CALLBACK (filter_notify_cb),
			  self);
}


GtkWidget *
fr_new_archive_dialog_new (GtkWindow          *parent,
			   FrNewArchiveAction  action,
			   const char         *default_name)
{
	FrNewArchiveDialog *self;

	self = g_object_new (FR_TYPE_NEW_ARCHIVE_DIALOG, NULL);
	_fr_new_archive_dialog_construct (self, parent, action, default_name);

	return (GtkWidget *) self;
}


static int
_fr_new_archive_dialog_get_archive_type (FrNewArchiveDialog *self)
{
	const char *uri;
	const char *ext;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self));
	if (uri == NULL)
		return -1;

	ext = get_archive_filename_extension (uri);
	if (ext == NULL) {
		int n_format;

		n_format = _fr_new_archive_dialog_get_format (self, uri);
		if (n_format >= 0)
			return self->priv->supported_types[n_format];

		ext = DEFAULT_EXTENSION;
	}

	return get_mime_type_index (get_mime_type_from_extension (ext));
}


char *
fr_new_archive_dialog_get_uri (FrNewArchiveDialog  *self,
			       const char         **mime_type)
{
	char       *uri = NULL;
	int         n_format;
	const char *file_mime_type;
	GFile      *file, *dir;
	GFileInfo  *dir_info;
	GError     *err = NULL;
	GtkWidget  *dialog;

	/* uri */

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self));
	if ((uri == NULL) || (*uri == 0)) {
		g_free (uri);

		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));

		return NULL;
	}

	/* mime type */

	n_format = _fr_new_archive_dialog_get_format (self, uri);
	if (n_format >= 0)
		file_mime_type = mime_type_desc[self->priv->supported_types[n_format]].mime_type;
	else
		file_mime_type = NULL;
	if (mime_type != NULL)
		*mime_type = file_mime_type;

	if (file_mime_type == NULL) {
		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("Archive type not supported."));
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));
		g_free (uri);

		return NULL;
	}

	/* check permissions */

	file = g_file_new_for_uri (uri);
	dir = g_file_get_parent (file);
	dir_info = g_file_query_info (dir,
				      G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				      G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				      G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE","
				      G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
				      0, NULL, &err);

	g_object_unref (dir);
	g_object_unref (file);

	if (err != NULL) {
		g_warning ("Failed to get permission for extraction dir: %s",
			   err->message);
		g_clear_error (&err);
		g_object_unref (dir_info);
		g_free (uri);
		return NULL;
	}

	if (! g_file_info_get_attribute_boolean (dir_info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) {
		g_object_unref (dir_info);
		g_free (uri);

		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("You don't have permission to create an archive in this folder"));
		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (GTK_WIDGET (dialog));
		return NULL;
	}

	/* overwrite confirmation */

	file = g_file_new_for_uri (uri);

	if (g_file_query_exists (file, NULL)) {
		char     *filename;
		char     *message;
		char     *secondary_message;
		gboolean  overwrite;

		filename = _g_uri_display_basename (uri);
		message = g_strdup_printf (_("A file named \"%s\" already exists.  Do you want to replace it?"), filename);
		secondary_message = g_strdup_printf (_("The file already exists in \"%s\".  Replacing it will overwrite its contents."), g_file_info_get_display_name (dir_info));
		dialog = _gtk_message_dialog_new (GTK_WINDOW (self),
						  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_QUESTION,
						  message,
						  secondary_message,
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  _("_Replace"), GTK_RESPONSE_OK,
						  NULL);
		overwrite = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

		gtk_widget_destroy (dialog);
		g_free (secondary_message);
		g_free (message);
		g_free (filename);

		if (overwrite) {
			g_file_delete (file, NULL, &err);
			if (err != NULL) {
				dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
								GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
								NULL,
								_("Could not delete the old archive."),
								"%s",
								err->message);
				gtk_dialog_run (GTK_DIALOG (dialog));

				gtk_widget_destroy (GTK_WIDGET (dialog));
				g_error_free (err);
				g_free (uri);
				g_object_unref (file);

				return NULL;
			}
		}
		else {
			g_free (uri);
			uri = NULL;
		}
	}

	g_object_unref (file);
	g_object_unref (dir_info);

	return uri;
}


const char *
fr_new_archive_dialog_get_password (FrNewArchiveDialog *self)
{
	const char *password = NULL;
	int         idx;

	idx = _fr_new_archive_dialog_get_archive_type (self);
	if (idx < 0)
		return NULL;

	if (mime_type_desc[idx].capabilities & FR_ARCHIVE_CAN_ENCRYPT)
		password = (char*) gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("password_entry")));

	return password;
}


gboolean
fr_new_archive_dialog_get_encrypt_header (FrNewArchiveDialog *self)
{
	gboolean encrypt_header = FALSE;
	int      idx;

	idx = _fr_new_archive_dialog_get_archive_type (self);
	if (idx < 0)
		return FALSE;

	if (mime_type_desc[idx].capabilities & FR_ARCHIVE_CAN_ENCRYPT) {
		const char *password = gtk_entry_get_text (GTK_ENTRY (GET_WIDGET ("password_entry")));
		if (password != NULL) {
			if (strcmp (password, "") != 0) {
				if (mime_type_desc[idx].capabilities & FR_ARCHIVE_CAN_ENCRYPT_HEADER)
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
	int   idx;

	idx = _fr_new_archive_dialog_get_archive_type (self);
	if (idx < 0)
		return 0;

	if ((mime_type_desc[idx].capabilities & FR_ARCHIVE_CAN_CREATE_VOLUMES)
	    && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (GET_WIDGET ("volume_checkbutton"))))
	{
		double value;

		value = gtk_spin_button_get_value (GTK_SPIN_BUTTON (GET_WIDGET ("volume_spinbutton")));
		volume_size = floor (value * MEGABYTE);

	}

	return volume_size;
}
