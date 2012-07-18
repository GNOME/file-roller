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
#include "eggfileformatchooser.h"
#include "file-utils.h"
#include "fr-init.h"
#include "fr-new-archive-dialog.h"
#include "fr-stock.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "preferences.h"


#define GET_WIDGET(x)     (_gtk_builder_get_widget (self->priv->builder, (x)))
#define DEFAULT_EXTENSION ".tar.gz"
#define MEGABYTE          (1024 * 1024)


struct _FrNewArchiveDialogPrivate {
	GtkBuilder           *builder;
	EggFileFormatChooser *format_chooser;
	int                  *supported_types;
	gboolean              can_encrypt;
	gboolean              can_encrypt_header;
	gboolean              can_create_volumes;
};


G_DEFINE_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, GTK_TYPE_FILE_CHOOSER_DIALOG)


static void
fr_new_archive_dialog_finalize (GObject *object)
{
	FrNewArchiveDialog *self;

	self = FR_NEW_ARCHIVE_DIALOG (object);

	g_object_unref (self->priv->builder);

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


/* FIXME
static void
archive_type_combo_box_changed_cb (GtkComboBox *combo_box,
				   DlgNewData  *data)
{
	const char *uri;
	const char *ext;
	int         idx;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (data->dialog));

	ext = get_archive_filename_extension (uri);
	idx = gtk_combo_box_get_active (GTK_COMBO_BOX (data->n_archive_type_combo_box)) - 1;
	if ((ext == NULL) && (idx >= 0))
		ext = mime_type_desc[self->priv->supported_types[idx]].default_ext;

	_fr_new_archive_dialog_update_sensitivity_for_ext (data, ext);

	if ((idx >= 0) && (uri != NULL)) {
		const char *new_ext;
		const char *basename;
		char       *basename_noext;
		char       *new_basename;
		char       *new_basename_uft8;

		new_ext = mime_type_desc[self->priv->supported_types[idx]].default_ext;
		basename = _g_path_get_file_name (uri);
		if (g_str_has_suffix (basename, ext))
			basename_noext = g_strndup (basename, strlen (basename) - strlen (ext));
		else
			basename_noext = g_strdup (basename);
		new_basename = g_strconcat (basename_noext, new_ext, NULL);
		new_basename_uft8 = g_uri_unescape_string (new_basename, NULL);

		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (data->dialog), new_basename_uft8);
		_fr_new_archive_dialog_update_sensitivity_for_ext (data, new_ext);

		g_free (new_basename_uft8);
		g_free (new_basename);
		g_free (basename_noext);
	}
}
*/


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


static void
format_chooser_selection_changed_cb (EggFileFormatChooser *format_chooser,
				     gpointer              user_data)
{
	FrNewArchiveDialog *self = user_data;
	const char         *uri;
	const char         *ext;
	int                 n_format;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self));
	if (uri == NULL)
		return;

	ext = get_archive_filename_extension (uri);
	n_format = egg_file_format_chooser_get_format (EGG_FILE_FORMAT_CHOOSER (self->priv->format_chooser), uri);
	if (ext == NULL)
		ext = mime_type_desc[self->priv->supported_types[n_format - 1]].default_ext;

	_fr_new_archive_dialog_update_sensitivity_for_ext (self, ext);

	if (uri != NULL) {
		const char *new_ext;
		const char *basename;
		char       *basename_noext;
		char       *new_basename;
		char       *new_basename_uft8;

		new_ext = mime_type_desc[self->priv->supported_types[n_format - 1]].default_ext;
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


static void
options_expander_unmap_cb (GtkWidget *widget,
			   gpointer   user_data)
{
	egg_file_format_chooser_emit_size_changed ((EggFileFormatChooser *) user_data);
}


static char *
get_icon_name_for_type (const char *mime_type)
{
	char *name = NULL;

	if (mime_type != NULL) {
		char *s;

		name = g_strconcat ("gnome-mime-", mime_type, NULL);
		for (s = name; *s; ++s)
			if (! g_ascii_isalpha (*s))
				*s = '-';
	}

	if ((name == NULL) || ! gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), name)) {
		g_free (name);
		name = g_strdup ("package-x-generic");
	}

	return name;
}


static void
_fr_new_archive_dialog_construct (FrNewArchiveDialog *self,
				  GtkWindow          *parent,
				  FrNewArchiveAction  action,
				  const char         *default_name)
{
	GSettings *settings;
	int        i;

	gtk_file_chooser_set_action (GTK_FILE_CHOOSER (self), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (self), TRUE);
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

	/* format chooser */

	self->priv->format_chooser = (EggFileFormatChooser *) egg_file_format_chooser_new ();
	for (i = 0; self->priv->supported_types[i] != -1; i++) {
		int   idx = self->priv->supported_types[i];
		char *exts[4];
		int   e;
		int   n_exts;
		char *icon_name;

		n_exts = 0;
		for (e = 0; (n_exts < 4) && file_ext_type[e].ext != NULL; e++) {
			if (strcmp (file_ext_type[e].ext, mime_type_desc[idx].default_ext) == 0)
				continue;
			if (strcmp (file_ext_type[e].mime_type, mime_type_desc[idx].mime_type) == 0)
				exts[n_exts++] = file_ext_type[e].ext;
		}
		while (n_exts < 4)
			exts[n_exts++] = NULL;

		/* g_print ("%s => %s, %s, %s, %s\n", mime_type_desc[idx].mime_type, exts[0], exts[1], exts[2], exts[3]); */

		icon_name = get_icon_name_for_type (mime_type_desc[idx].mime_type);
		egg_file_format_chooser_add_format (self->priv->format_chooser,
						    0,
						    _(mime_type_desc[idx].name),
						    icon_name,
						    mime_type_desc[idx].default_ext,
						    exts[0],
						    exts[1],
						    exts[2],
						    exts[3],
						    NULL);

		g_free (icon_name);
	}
	egg_file_format_chooser_set_format (self->priv->format_chooser, 0);
	gtk_widget_show (GTK_WIDGET (self->priv->format_chooser));
	gtk_box_pack_start (GTK_BOX (GET_WIDGET ("format_chooser_box")), GTK_WIDGET (self->priv->format_chooser), TRUE, TRUE, 0);

	gtk_widget_set_vexpand (GET_WIDGET ("extra_widget"), FALSE);

	_fr_new_archive_dialog_update_sensitivity (self);

	/* Set the signals handlers. */

	/* FIXME g_signal_connect (GET_WIDGET ("archive_type_combo_box"),
			  "changed",
			  G_CALLBACK (archive_type_combo_box_changed_cb),
			  self); */
	g_signal_connect (GET_WIDGET ("password_entry"),
			  "changed",
			  G_CALLBACK (password_entry_changed_cb),
			  self);
	g_signal_connect (GET_WIDGET ("volume_checkbutton"),
			  "toggled",
			  G_CALLBACK (volume_toggled_cb),
			  self);
	g_signal_connect (self->priv->format_chooser,
			  "selection-changed",
			  G_CALLBACK (format_chooser_selection_changed_cb),
			  self);
	g_signal_connect_after (GET_WIDGET ("other_options_alignment"),
				"unmap",
				G_CALLBACK (options_expander_unmap_cb),
				self->priv->format_chooser);
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
		int idx;

		idx = egg_file_format_chooser_get_format (EGG_FILE_FORMAT_CHOOSER (self->priv->format_chooser), uri);
		/*idx = gtk_combo_box_get_active (GTK_COMBO_BOX (data->n_archive_type_combo_box)) - 1;*/
		if (idx >= 0)
			return self->priv->supported_types[idx];

		ext = DEFAULT_EXTENSION;
	}

	return get_mime_type_index (get_mime_type_from_extension (ext));
}


/* when on Automatic the user provided extension needs to be supported,
   otherwise an existing unsupported archive can be deleted (if the user
   provided name matches with its name) before we find out that the
   archive is unsupported
*/
static gboolean
is_supported_extension (char *filename,
			int  *file_types)
{
	int i;
	for (i = 0; file_types[i] != -1; i++)
		if (_g_filename_has_extension (filename, mime_type_desc[file_types[i]].default_ext))
			return TRUE;
	return FALSE;
}


static char *
_fr_new_archive_dialog_get_uri (FrNewArchiveDialog *self)
{
	char        *full_uri = NULL;
	char        *uri;
	const char  *filename;
	int          idx;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self));

	if ((uri == NULL) || (*uri == 0))
		return NULL;

	filename = _g_path_get_file_name (uri);
	if ((filename == NULL) || (*filename == 0)) {
		g_free (uri);
		return NULL;
	}

	idx = egg_file_format_chooser_get_format (EGG_FILE_FORMAT_CHOOSER (self->priv->format_chooser), uri);
	if (idx > 0) {
		const char *uri_ext;
		char       *default_ext;

		uri_ext = get_archive_filename_extension (uri);
		default_ext = mime_type_desc[self->priv->supported_types[idx-1]].default_ext;
		if (_g_strcmp_null_tolerant (uri_ext, default_ext) != 0) {
			full_uri = g_strconcat (uri, default_ext, NULL);
			g_free (uri);
		}
	}
	if (full_uri == NULL)
		full_uri = uri;

	return full_uri;
}


char *
fr_new_archive_dialog_get_uri (FrNewArchiveDialog *self)
{
	char      *uri = NULL;
	GFile     *file, *dir;
	GFileInfo *info;
	GError    *err = NULL;

	uri = _fr_new_archive_dialog_get_uri (self);
	if ((uri == NULL) || (*uri == 0)) {
		GtkWidget *dialog;

		g_free (uri);

		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		return NULL;
	}

	file = g_file_new_for_uri (uri);

	dir = g_file_get_parent (file);
	info = g_file_query_info (dir,
				  G_FILE_ATTRIBUTE_ACCESS_CAN_READ ","
				  G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE ","
				  G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
				  0, NULL, &err);
	if (err != NULL) {
		g_warning ("Failed to get permission for extraction dir: %s",
			   err->message);
		g_clear_error (&err);
		g_object_unref (info);
		g_object_unref (dir);
		g_object_unref (file);
		g_free (uri);
		return NULL;
	}

	if (! g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) {
		GtkWidget *dialog;

		g_object_unref (info);
		g_object_unref (dir);
		g_object_unref (file);
		g_free (uri);

		dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
						GTK_DIALOG_DESTROY_WITH_PARENT,
						NULL,
						_("Could not create the archive"),
						"%s",
						_("You don't have permission to create an archive in this folder"));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return NULL;
	}
	g_object_unref (info);
	g_object_unref (dir);

	/* if the user did not specify a valid extension use the filetype combobox current type
	 * or tar.gz if automatic is selected. */
	if (get_archive_filename_extension (uri) == NULL) {
		int   idx;
		char *new_uri;
		char *ext = NULL;

		idx = egg_file_format_chooser_get_format (EGG_FILE_FORMAT_CHOOSER (self->priv->format_chooser), uri);
		if (idx > 0)
			ext = mime_type_desc[self->priv->supported_types[idx-1]].default_ext;
		else
			ext = ".tar.gz";
		new_uri = g_strconcat (uri, ext, NULL);
		g_free (uri);
		uri = new_uri;
	}

	debug (DEBUG_INFO, "create/save %s\n", uri);

	if (_g_uri_query_exists (uri)) {
		GtkWidget *dialog;

		if (! is_supported_extension (uri, self->priv->supported_types)) {
			dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
							GTK_DIALOG_MODAL,
							NULL,
							_("Could not create the archive"),
							"%s",
							_("Archive type not supported."));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			g_free (uri);

			return NULL;
		}

		g_file_delete (file, NULL, &err);
		if (err != NULL) {
			GtkWidget *dialog;

			dialog = _gtk_error_dialog_new (GTK_WINDOW (self),
							GTK_DIALOG_DESTROY_WITH_PARENT,
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

	g_object_unref (file);

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
