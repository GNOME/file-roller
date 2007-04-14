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
#include <libgnomeui/gnome-icon-theme.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-types.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-ops.h>

#include "file-utils.h"
#include "fr-stock.h"
#include "gconf-utils.h"
#include "window.h"
#include "typedefs.h"
#include "gtk-utils.h"
#include "glib-utils.h"
#include "preferences.h"
#include "main.h"


#define GLADE_FILE "file-roller.glade"
#define UPDATE_DROPPED_FILES (FALSE)
#define ARCHIVE_ICON_SIZE (48)
#define DEFAULT_EXTENSION ".tar.gz"
#define BAD_CHARS "/\\*"

typedef struct {
	FRWindow   *window;
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
		window_pop_message (data->window);
		window_batch_mode_stop (data->window);
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
	FRWindow   *window = data->window;
	char       *archive_name;
	char       *archive_dir;
	char       *archive_file;
	char       *tmp;
	const char *archive_ext;
	gboolean    do_not_add = FALSE;

	data->add_clicked = TRUE;

	/* Collect data */

/* FIXME
  	window->update_dropped_files = UPDATE_DROPPED_FILES; 
  	*/
	archive_name = g_filename_from_utf8 (gtk_entry_get_text (GTK_ENTRY (data->a_add_to_entry)), -1, NULL, NULL, NULL);

	/* check whether the user entered a valid archive name. */

	if (*archive_name == '\0') {
		GtkWidget  *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not create the archive"),
					     _("You have to specify an archive name."),
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);

		return;

	} else if (strchrs (archive_name, BAD_CHARS)) {
		GtkWidget  *d;
		char       *utf8_name = g_filename_display_name (archive_name);
		char       *reason = g_strdup_printf (_("The name \"%s\" is not valid because it cannot contain the characters: %s\n\n%s"), utf8_name, BAD_CHARS, _("Please use a different name."));

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not create the archive"),
					     reason,
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (reason);
		g_free (archive_name);

		return;
	}

	/* Check directory existence. */

	archive_dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (data->a_location_filechooserbutton));

	if (! check_permissions (archive_dir, R_OK|W_OK|X_OK)) {
		GtkWidget  *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not create the archive"),
					     _("You don't have the right permissions to create an archive in the destination folder."),
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);

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
		const char *error;
		char       *message;

		error = gnome_vfs_result_to_string (gnome_vfs_result_from_errno ());
		message = g_strdup_printf (_("Could not create the destination folder: %s."), error);
		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not create the archive"),
					     message,
					     GTK_STOCK_OK, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);
		g_free (message);

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_dir);
		g_free (archive_name);
		return;
	}

	if (do_not_add) {
		GtkWidget *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Archive not created"),
					     NULL,
					     GTK_STOCK_CLOSE, GTK_RESPONSE_CANCEL,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_CANCEL);
		gtk_dialog_run (GTK_DIALOG (d));
		g_free (archive_dir);
		g_free (archive_name);
		gtk_widget_destroy (GTK_WIDGET (d));
		return;
	}

	/**/

	archive_ext = get_ext (data);
	tmp = archive_name;
	archive_name = g_strconcat (tmp, archive_ext, NULL);
	g_free (tmp);
	archive_file = g_build_filename (archive_dir, archive_name, NULL);
	eel_gconf_set_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, archive_ext);

	if (path_is_dir (archive_file)) {
		GtkWidget  *d;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_DIALOG_ERROR,
					     _("Could not create the archive"),
					     _("You have to specify an archive name."),
					     GTK_STOCK_OK, GTK_RESPONSE_OK,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (archive_name);
		g_free (archive_dir);
		g_free (archive_file);
		return;
	}

	if (path_is_file (archive_file)
	    && ((strcmp (archive_ext, ".gz") == 0)
		|| (strcmp (archive_ext, ".z") == 0)
		|| (strcmp (archive_ext, ".Z") == 0)
		|| (strcmp (archive_ext, ".bz") == 0)
		|| (strcmp (archive_ext, ".bz2") == 0)
		|| (strcmp (archive_ext, ".lzo") == 0))) {
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

		if (r == GTK_RESPONSE_YES)
			gnome_vfs_unlink (archive_file);
		else {
			g_free (archive_name);
			g_free (archive_dir);
			g_free (archive_file);
			return;
		}
	}

	if (! path_is_file (archive_file)) {
		/* FIXME: use batch actions
		if (window->dropped_file_list != NULL)
			path_list_free (window->dropped_file_list);
		window->dropped_file_list = path_list_dup (data->file_list);
		window->add_after_creation = TRUE;
		window_archive_new (window, archive_file);
		*/

	} else {
		/* FIXME: use match action to add after opening
		window->add_after_opening = TRUE; */

		window_batch_mode_add_next_action (window,
						   FR_BATCH_ACTION_ADD,
						   path_list_dup (data->file_list),
						   (GFreeFunc) path_list_free);
		window_archive_open (window, archive_file, GTK_WINDOW (window->app));
	}

	g_free (archive_name);
	g_free (archive_dir);
	g_free (archive_file);

	gtk_widget_destroy (data->dialog);
}


/* taken from egg-recent-util.c */
static GdkPixbuf *
scale_icon (GdkPixbuf *pixbuf,
	    double    *scale)
{
	guint width, height;

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	width = floor (width * *scale + 0.5);
	height = floor (height * *scale + 0.5);

	return gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
}


/* taken from egg-recent-util.c */
static GdkPixbuf *
load_icon_file (char          *filename,
		guint          base_size,
		guint          nominal_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	guint      width, height, size;
	double     scale;

	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

	if (pixbuf == NULL) {
		return NULL;
	}

	if (base_size == 0) {
		width = gdk_pixbuf_get_width (pixbuf);
		height = gdk_pixbuf_get_height (pixbuf);
		size = MAX (width, height);
		if (size > nominal_size) {
			base_size = size;
		} else {
			/* Don't scale up small icons */
			base_size = nominal_size;
		}

	}

	if (base_size != nominal_size) {
		scale = (double) nominal_size / base_size;
		scaled_pixbuf = scale_icon (pixbuf, &scale);
		g_object_unref (pixbuf);
		pixbuf = scaled_pixbuf;
	}

	return pixbuf;
}


static GdkPixbuf *
get_pixbuf_from_mime_type (const char *mime_type,
			   int         icon_size)
{
	GnomeIconTheme *icon_theme;
	char           *icon_name = NULL;
	GdkPixbuf      *icon = NULL;

	icon_theme = gnome_icon_theme_new ();

	if (icon_theme == NULL)
		return NULL;

	gnome_icon_theme_set_allow_svg (icon_theme, TRUE);

	icon_name = gnome_icon_lookup (icon_theme,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       mime_type,
				       GNOME_ICON_LOOKUP_FLAGS_NONE,
				       NULL);

	if (icon_name != NULL) {
		char                *icon_path = NULL;
		const GnomeIconData *icon_data;
		int                  base_size;

		icon_path = gnome_icon_theme_lookup_icon (icon_theme,
							  icon_name,
							  icon_size,
							  &icon_data,
							  &base_size);

		if (icon_path != NULL) {
			icon = load_icon_file (icon_path,
					       base_size, 
					       icon_size);
			g_free (icon_path);
		}
	}

	g_free (icon_name);
	g_object_unref (icon_theme);

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

	for (i = 0; save_type_list[i] != FR_FILE_TYPE_NULL; i++) {
		if (strcmp (ext, file_type_desc[save_type_list[i]].ext) == 0) {
			idx = i;
			break;
		}
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (data->a_archive_type_combo_box), idx);
}


void
dlg_batch_add_files (FRWindow *window,
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

	data->window = window;
	data->file_list = file_list;
	data->add_clicked = FALSE;

	data->single_file = ((file_list->next == NULL) && path_is_file ((char*) file_list->data));

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (!data->gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	/* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "batch_add_files_dialog");
	data->a_add_to_entry = glade_xml_get_widget (data->gui, "a_add_to_entry");
	data->a_location_filechooserbutton = glade_xml_get_widget (data->gui, "a_location_filechooserbutton");

	add_button = glade_xml_get_widget (data->gui, "a_add_button");
	cancel_button = glade_xml_get_widget (data->gui, "a_cancel_button");
	a_archive_type_box = glade_xml_get_widget (data->gui, "a_archive_type_box");

	data->add_image = glade_xml_get_widget (data->gui, "a_add_image");

	/* Set widgets data. */

	first_filename = (char*) file_list->data;
	parent = remove_level_from_path (first_filename);

	if (file_list->next == NULL)
		automatic_name = g_strdup (file_name_from_path ((char*) file_list->data));
	else {
		automatic_name = g_strdup (file_name_from_path (parent));
		if ((automatic_name == NULL) || (automatic_name[0] == '\0')) {
			g_free (automatic_name);
			automatic_name = g_strdup (file_name_from_path (first_filename));
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
