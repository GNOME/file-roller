/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <glade/glade.h>
#include <gio.h>

#include "file-utils.h"
#include "fr-stock.h"
#include "gconf-utils.h"
#include "fr-window.h"
#include "typedefs.h"
#include "gtk-utils.h"
#include "glib-utils.h"
#include "preferences.h"
#include "main.h"


#define GLADE_FILE "batch-add-files.glade"
#define ARCHIVE_ICON_SIZE (48)
#define DEFAULT_EXTENSION ".tar.gz"
#define BAD_CHARS "/\\*"

typedef struct {
	FrWindow   *window;
	GladeXML   *gui;

	GtkWidget  *dialog;
	GtkWidget  *a_add_to_entry;
	GtkWidget  *a_location_filechooserbutton;
	GtkWidget  *add_image;
	GtkWidget  *a_archive_type_combo_box;

	GList      *file_list;
	gboolean    add_clicked;
	const char *last_mime_type;
	gboolean    single_file;
} DialogData;


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
	    DialogData *data)
{
	if (! data->add_clicked) {
		fr_window_pop_message (data->window);
		fr_window_stop_batch (data->window);
	}

	g_object_unref (data->gui);
	g_free (data);
}


static const char *
get_ext (DialogData *data)
{
	FRFileType *save_type_list;
	int         idx;

	if (data->single_file)
		save_type_list = single_file_save_type;
	else
		save_type_list =  save_type;

	idx = gtk_combo_box_get_active (GTK_COMBO_BOX (data->a_archive_type_combo_box));

	return file_type_desc[save_type_list[idx]].ext;
}


/* called when the "add" button is pressed. */
static void
add_clicked_cb (GtkWidget  *widget,
		DialogData *data)
{
	FrWindow   *window = data->window;
	char       *archive_name;
	char       *archive_dir;
	char       *archive_file;
	char       *tmp;
	const char *archive_ext;
	gboolean    do_not_add = FALSE;

	data->add_clicked = TRUE;

	/* Collect data */

	archive_name = gnome_vfs_escape_string (gtk_entry_get_text (GTK_ENTRY (data->a_add_to_entry)));

	/* Check whether the user entered a valid archive name. */

	if ((archive_name == NULL) || (*archive_name == '\0')) {
		GtkWidget *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Could not create the archive"),
					   _("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);

		return;
	}
	else if (strchrs (archive_name, BAD_CHARS)) {
		GtkWidget *d;
		char      *utf8_name = g_filename_display_name (archive_name);

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Could not create the archive"),
					   _("The name \"%s\" is not valid because it cannot contain the characters: %s\n\n%s"), 
					   utf8_name, 
					   BAD_CHARS, 
					   _("Please use a different name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (utf8_name);
		g_free (archive_name);

		return;
	}

	/* Check directory existence. */

	archive_dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (data->a_location_filechooserbutton));
	if (archive_dir == NULL) {
		g_free (archive_dir);
		g_free (archive_name);
		return;
	}
		
	if (! check_permissions (archive_dir, R_OK|W_OK|X_OK)) {
		GtkWidget  *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Could not create the archive"),
					   _("You don't have the right permissions to create an archive in the destination folder."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (archive_dir);
		g_free (archive_name);
		return;
	}

	if (! path_is_dir (archive_dir)) {
		GtkWidget *d;
		int        r;
		char      *folder_name;
		char      *msg;

		folder_name = g_filename_display_name (archive_dir);
		msg = g_strdup_printf (_("Destination folder \"%s\" does not exist.\n\nDo you want to create it?"), folder_name);
		g_free (folder_name);

		d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     msg,
					     NULL,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Create _Folder"), GTK_RESPONSE_YES,
					     NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (msg);

		do_not_add = (r != GTK_RESPONSE_YES);
	}

	if (! do_not_add && ! ensure_dir_exists (archive_dir, 0755)) {
		GtkWidget  *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Could not create the archive"),
					   _("Could not create the destination folder: %s."),
					   gnome_vfs_result_to_string (gnome_vfs_result_from_errno ()));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (archive_dir);
		g_free (archive_name);
		return;
	}

	if (do_not_add) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_WARNING,
					     _("Archive not created"),
					     NULL,
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (archive_dir);
		g_free (archive_name);

		return;
	}

	/**/

	archive_ext = get_ext (data);
	tmp = archive_name;
	archive_name = g_strconcat (tmp, archive_ext, NULL);
	g_free (tmp);
	archive_file = g_strconcat (archive_dir, "/", archive_name, NULL);
	eel_gconf_set_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, archive_ext);

	if (path_is_dir (archive_file)) {
		GtkWidget  *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   NULL,
					   _("Could not create the archive"),
					   _("You have to specify an archive name."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		g_free (archive_name);
		g_free (archive_dir);
		g_free (archive_file);

		return;
	}

	if (path_is_file (archive_file)) {
		GtkWidget *d;
		int        r;

		d = _gtk_message_dialog_new (GTK_WINDOW (data->dialog),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("The archive is already present.  Do you want to overwrite it?"),
					     NULL,
					     GTK_STOCK_NO, GTK_RESPONSE_NO,
					     _("_Overwrite"), GTK_RESPONSE_YES,
					     NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));

		if (r == GTK_RESPONSE_YES) {
			GFile  *file;
			GError *err = NULL;

			/* FIXME: convert this code in a function in file-utils.c */
			file = g_file_new_for_uri (archive_file);
			g_file_delete (file, NULL, &err);
			if (err != NULL) {
				g_warning ("Failed to delete file %s: %s", 
					   archive_file, 
					   err->message);
				g_clear_error (&err);
			}
			g_object_unref (file);
		}
		else {
			g_free (archive_name);
			g_free (archive_dir);
			g_free (archive_file);
			return;
		}
	}

	gtk_widget_destroy (data->dialog);

	if (! path_is_file (archive_file))
		fr_window_archive_new (window, archive_file);
	else
		fr_window_archive_open (window, archive_file, GTK_WINDOW (window));

	g_free (archive_name);
	g_free (archive_dir);
	g_free (archive_file);
}


static GdkPixbuf *
get_pixbuf_from_mime_type (const char *mime_type,
			   int         icon_size)
{
	GtkIconTheme *icon_theme;
	char         *icon_name = NULL;
	GdkPixbuf    *icon = NULL;

	icon_theme = gtk_icon_theme_get_default ();

	if (icon_theme == NULL)
		return NULL;

	icon_name = gnome_icon_lookup (icon_theme,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       mime_type,
				       GNOME_ICON_LOOKUP_FLAGS_NONE,
				       NULL);

	if (icon_name != NULL) 
		icon = gtk_icon_theme_load_icon (icon_theme,
						 icon_name,
						 icon_size,
						 0,
						 NULL);

	g_free (icon_name);
	return icon;
}


static void
archive_type_combo_box_changed_cb (GtkComboBox *combo_box,
				   DialogData  *data)
{
	FRFileType *save_type_list;
	const char *mime_type;
	int         idx = gtk_combo_box_get_active (combo_box);

	if (data->single_file)
		save_type_list = single_file_save_type;
	else
		save_type_list =  save_type;
	mime_type = file_type_desc[save_type_list[idx]].mime_type;

	gtk_image_set_from_pixbuf (GTK_IMAGE (data->add_image), get_pixbuf_from_mime_type (mime_type, ARCHIVE_ICON_SIZE));
}


static void
update_archive_type_combo_box_from_ext (DialogData  *data,
					const char  *ext)
{
	FRFileType *save_type_list;
	int         idx = 0;
	int         i;

	if (ext == NULL) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (data->a_archive_type_combo_box), 0);
		return;
	}

	if (data->single_file)
		save_type_list = single_file_save_type;
	else
		save_type_list =  save_type;

	for (i = 0; save_type_list[i] != FR_FILE_TYPE_NULL; i++)
		if (strcmp (ext, file_type_desc[save_type_list[i]].ext) == 0) {
			idx = i;
			break;
		}

	gtk_combo_box_set_active (GTK_COMBO_BOX (data->a_archive_type_combo_box), idx);
}


void
dlg_batch_add_files (FrWindow *window,
		     GList    *file_list)
{
	DialogData *data;
	GtkWidget  *cancel_button;
	GtkWidget  *add_button;
	GtkWidget  *a_archive_type_box;
	char       *automatic_name = NULL;
	char       *default_ext;
	const char *first_filename;
	char       *parent;
	int         i;
	FRFileType *save_type_list;

	if (file_list == NULL)
		return;

	data = g_new0 (DialogData, 1);

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (data->gui == NULL) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	data->window = window;
	data->file_list = file_list;
	data->single_file = ((file_list->next == NULL) && path_is_file ((char*) file_list->data));
	data->add_clicked = FALSE;

	/* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "batch_add_files_dialog");
	data->a_add_to_entry = glade_xml_get_widget (data->gui, "a_add_to_entry");
	data->a_location_filechooserbutton = glade_xml_get_widget (data->gui, "a_location_filechooserbutton");

	add_button = glade_xml_get_widget (data->gui, "a_add_button");
	cancel_button = glade_xml_get_widget (data->gui, "a_cancel_button");
	a_archive_type_box = glade_xml_get_widget (data->gui, "a_archive_type_box");

	data->add_image = glade_xml_get_widget (data->gui, "a_add_image");

	/* Set widgets data. */

	gtk_button_set_use_stock (GTK_BUTTON (add_button), TRUE);
	gtk_button_set_label (GTK_BUTTON (add_button), FR_STOCK_CREATE_ARCHIVE);
	
	first_filename = (char*) file_list->data;
	parent = remove_level_from_path (first_filename);

	if (file_list->next == NULL)
		automatic_name = gnome_vfs_unescape_string (file_name_from_path ((char*) file_list->data), "");
	else {
		automatic_name = gnome_vfs_unescape_string (file_name_from_path (parent), "");
		if ((automatic_name == NULL) || (automatic_name[0] == '\0')) {
			g_free (automatic_name);
			automatic_name = gnome_vfs_unescape_string (file_name_from_path (first_filename), "");
		}
	}

	_gtk_entry_set_filename_text (GTK_ENTRY (data->a_add_to_entry), automatic_name);
	g_free (automatic_name);

	if (check_permissions (parent, R_OK|W_OK|X_OK))
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (data->a_location_filechooserbutton), parent);
	else
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (data->a_location_filechooserbutton), get_home_uri ());
	g_free (parent);

	/* archive type combobox */

	data->a_archive_type_combo_box = gtk_combo_box_new_text ();
	if (data->single_file)
		save_type_list = single_file_save_type;
	else
		save_type_list = save_type;

	for (i = 0; save_type_list[i] != FR_FILE_TYPE_NULL; i++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (data->a_archive_type_combo_box),
					   file_type_desc[save_type_list[i]].ext);
	}

	gtk_box_pack_start (GTK_BOX (a_archive_type_box), data->a_archive_type_combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (a_archive_type_box);

	/* Set the signals handlers. */

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);
	g_signal_connect_swapped (G_OBJECT (cancel_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (add_button),
			  "clicked",
			  G_CALLBACK (add_clicked_cb),
			  data);
	g_signal_connect (G_OBJECT (data->a_archive_type_combo_box),
			  "changed",
			  G_CALLBACK (archive_type_combo_box_changed_cb),
			  data);

	/* Run dialog. */

	default_ext = eel_gconf_get_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, DEFAULT_EXTENSION);
	update_archive_type_combo_box_from_ext (data, default_ext);
	g_free (default_ext);

	gtk_widget_grab_focus (data->a_add_to_entry);
	gtk_editable_select_region (GTK_EDITABLE (data->a_add_to_entry),
				    0, -1);

	gtk_window_set_modal (GTK_WINDOW (data->dialog), FALSE);
	gtk_window_present (GTK_WINDOW (data->dialog));
}
