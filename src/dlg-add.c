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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gnome.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "file-utils.h"
#include "window.h"
#include "gtk-utils.h"


typedef struct {
	FRWindow  *window;
	GtkWidget *dialog;
	GtkWidget *include_subfold_checkbutton;
	GtkWidget *add_if_newer_checkbutton;
	GtkWidget *exclude_symlinks;
	GtkWidget *exclude_other_fs;
	GtkWidget *exclude_backup_files;
	GtkWidget *exclude_dot_files;
	GtkWidget *exclude_files_checkbutton;
	GtkWidget *exclude_files_entry;
	GtkWidget *ignore_case;
	GtkWidget *load_button;
	GtkWidget *save_button;
} DialogData;


static void
open_file_destroy_cb (GtkWidget *w,
		      GtkWidget *file_sel)
{
	DialogData *data;

	data = g_object_get_data (G_OBJECT (file_sel), "fr_dialog_data");
	g_free (data);
}


static gboolean
only_spaces (const char *text)
{
	int l, i;
	
	if (text == NULL)
		return TRUE;

	l = strlen (text);
	for (i = 0; i < l; i++)
		if (*text != ' ')
			return FALSE;

	return TRUE;
}


static int
open_file_ok_cb (GtkWidget *w,
                 GtkWidget *file_sel)
{
	FRWindow   *window;
	const char *file_sel_path;
	char       *path;
	DialogData *data;
	gboolean    update;
	char       *base_dir;

	data = g_object_get_data (G_OBJECT (file_sel), "fr_dialog_data");
	window = data->window;

	file_sel_path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (file_sel));

        if (file_sel_path == NULL)
                return FALSE;

	path = remove_ending_separator (file_sel_path);
	base_dir = remove_level_from_path (path);

	/* check directory permissions. */

	if (path_is_dir (base_dir) 
	    && access (base_dir, R_OK | X_OK) != 0) {
		GtkWidget *d;
		char      *utf8_path;

		utf8_path = g_locale_to_utf8 (base_dir, -1, NULL, NULL, NULL);
		d = gtk_message_dialog_new (GTK_WINDOW (window->app),
					    GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_CLOSE,
					    _("You don't have the right permissions to read files from folder \"%s\""),
					    utf8_path);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		g_free (utf8_path);

		g_free (base_dir);
		g_free (path);

		return FALSE;
	}

	window_set_add_default_dir (window, base_dir);

	update = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->add_if_newer_checkbutton));

	if ((strchr (path, '*') == NULL) && (strchr (path, '?') == NULL)) {
		GList  *files;
		char  **scan;
		char  **selections;

		selections = gtk_file_selection_get_selections (GTK_FILE_SELECTION (file_sel));

		files = NULL;
		if (*selections == NULL)
                        files = g_list_prepend (files, (gpointer) file_name_from_path (path));

		for (scan = selections; *scan != NULL; scan++) {
			files = g_list_prepend (files, (gpointer) file_name_from_path (*scan));
#ifdef DEBUG
			g_print ("add %s\n", *scan);
#endif
		}

#ifdef DEBUG
		g_print ("basedir : %s\n", base_dir);
#endif

		if (files != NULL) {
			gchar *first = files->data;
			if (path_is_dir (first))
				fr_archive_add_directory (window->archive, 
							  file_name_from_path (first), 
							  base_dir, 
							  FALSE,
							  window->password,
							  window->compression);
			else 
				fr_archive_add (window->archive,
						files,
						base_dir,
						update,
						window->password,
						window->compression);
		}
		
		g_list_free (files);
		g_strfreev (selections); 
	} else {
		gboolean    recursive;
		gboolean    no_symlinks;
		gboolean    same_fs;
		gboolean    no_backup_files;
		gboolean    no_dot_files;
		gboolean    ignore_case;
		const char *exclude_files;

		recursive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton));
		no_symlinks = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_symlinks));
		same_fs = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_other_fs));
		no_backup_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_backup_files));
		no_dot_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_dot_files));
		ignore_case = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->ignore_case));

		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_files_checkbutton))) {
			exclude_files = gtk_entry_get_text (GTK_ENTRY (data->exclude_files_entry));
			if (only_spaces (exclude_files))
				exclude_files = NULL;
		} else
			exclude_files = NULL;

		fr_archive_add_with_wildcard (window->archive,
					      file_name_from_path (path),
					      exclude_files,
					      base_dir,
					      update,
					      recursive,
					      ! no_symlinks,
					      same_fs,
					      no_backup_files,
					      no_dot_files,
					      ignore_case,
					      window->password,
					      window->compression);
	}

	g_free (path);
	g_free (base_dir);
	gtk_widget_destroy (file_sel);

	return TRUE;
}


static void
selection_entry_changed (GtkWidget  *widget, 
			 DialogData *data)
{
	char *path;
	gboolean    wildcard;

	path = _gtk_entry_get_locale_text (GTK_ENTRY (GTK_FILE_SELECTION (data->dialog)->selection_entry));

	wildcard = (strchr (path, '*') != NULL) || (strchr (path, '?') != NULL);
	gtk_widget_set_sensitive (data->include_subfold_checkbutton, wildcard);
	gtk_widget_set_sensitive (data->exclude_symlinks, GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton)->active && wildcard);
	gtk_widget_set_sensitive (data->exclude_other_fs, GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton)->active && wildcard);
	gtk_widget_set_sensitive (data->exclude_backup_files, wildcard);
	gtk_widget_set_sensitive (data->exclude_dot_files, wildcard);
	gtk_widget_set_sensitive (data->exclude_files_checkbutton, wildcard);
	gtk_widget_set_sensitive (data->exclude_files_entry,  GTK_TOGGLE_BUTTON (data->exclude_files_checkbutton)->active && wildcard);
	gtk_widget_set_sensitive (data->include_subfold_checkbutton, wildcard);
	gtk_widget_set_sensitive (data->ignore_case, wildcard);

	g_free (path);
}


static int
include_subfold_toggled_cb (GtkWidget *widget, 
			    gpointer   callback_data)
{
	DialogData *data = callback_data;

	gtk_widget_set_sensitive (data->exclude_symlinks,
				  GTK_TOGGLE_BUTTON (widget)->active);
	gtk_widget_set_sensitive (data->exclude_other_fs,
				  GTK_TOGGLE_BUTTON (widget)->active);
	return FALSE;
}


static int
exclude_files_toggled_cb (GtkWidget *widget, 
			  gpointer   callback_data)
{
	DialogData *data = callback_data;
	
	gtk_widget_set_sensitive (data->exclude_files_entry,
				  GTK_TOGGLE_BUTTON (widget)->active);
	return FALSE;   
}


static void load_options_cb (GtkWidget *w, GtkWidget *data);
static void save_options_cb (GtkWidget *w, GtkWidget *data);


static GtkWidget *
create_button (const char *stock_id, 
               const char *text)
{
        GtkWidget    *button;
        GtkWidget    *hbox;
        GtkWidget    *image;
        GtkWidget    *label;
        GtkWidget    *align;
	const char   *label_text;
        gboolean      text_is_stock;
        GtkStockItem  stock_item;

        button = gtk_button_new ();

        if (gtk_stock_lookup (text, &stock_item)) {
                label_text = stock_item.label;
                text_is_stock = TRUE;
        } else {
		label_text = text;
		text_is_stock = FALSE;
        }

        if (text_is_stock)
                image = gtk_image_new_from_stock (text, GTK_ICON_SIZE_BUTTON);
        else
                image = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON);
        label = gtk_label_new_with_mnemonic (label_text);
        hbox = gtk_hbox_new (FALSE, 2);
        align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);

	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (button));

        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        gtk_container_add (GTK_CONTAINER (button), align);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        gtk_widget_show_all (button);

        return button;
}


/* create the "add" dialog. */
void
add_cb (GtkWidget *widget, 
	void      *callback_data)
{
	GtkWidget   *file_sel;
	DialogData  *data;
	gchar       *dir;
	GtkWidget   *main_box;
	GtkWidget   *vbox, *hbox;
	GtkWidget   *frame;
	GtkTooltips *tooltips;
	
	tooltips = gtk_tooltips_new ();
 
	data = g_new (DialogData, 1);
	data->window = callback_data;
	data->dialog = file_sel = gtk_file_selection_new (_("Add Files and Folders"));
	gtk_file_selection_hide_fileop_buttons (GTK_FILE_SELECTION (file_sel));

	gtk_file_selection_set_select_multiple (GTK_FILE_SELECTION (file_sel), TRUE);

	data->add_if_newer_checkbutton = gtk_check_button_new_with_mnemonic (_("_Add only if newer"));
	data->include_subfold_checkbutton = gtk_check_button_new_with_mnemonic (_("_Include sub-folders"));
	data->exclude_symlinks = gtk_check_button_new_with_mnemonic (_("Exclude folders that are symbolic lin_ks"));
	data->exclude_other_fs = gtk_check_button_new_with_mnemonic (_("Exclude o_ther file systems"));
	data->exclude_backup_files = gtk_check_button_new_with_mnemonic (_("Exclude _backup files (*~)"));
	data->exclude_dot_files = gtk_check_button_new_with_mnemonic (_("Exclude _hidden files (.*)"));
	data->exclude_files_checkbutton = gtk_check_button_new_with_mnemonic (_("E_xclude files:"));
	data->exclude_files_entry = gtk_entry_new ();
	gtk_tooltips_set_tip (tooltips, data->exclude_files_entry, _("example: *.o; *.bak"), NULL);
	data->ignore_case = gtk_check_button_new_with_mnemonic (_("Igno_re case"));
	data->load_button = create_button (GTK_STOCK_OPEN, _("_Load Options"));
        data->save_button = create_button (GTK_STOCK_SAVE, _("Sa_ve Options"));

	frame = GTK_FILE_SELECTION (file_sel)->action_area;

	main_box = gtk_hbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (main_box), 0);
	gtk_container_add (GTK_CONTAINER (frame), main_box);

	vbox = gtk_vbox_new (FALSE, 1);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (main_box), vbox, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (vbox), data->add_if_newer_checkbutton, 
			    TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (vbox), data->include_subfold_checkbutton,
			    TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_symlinks,
			    TRUE, TRUE, 15);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_other_fs,
			    TRUE, TRUE, 15);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_backup_files,
			    TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_dot_files,
			    TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox,
			    TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_files_checkbutton,
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), data->exclude_files_entry,
			    TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (vbox), data->ignore_case,
			    TRUE, TRUE, 0);

	/**/
	
	gtk_box_pack_start (GTK_BOX (main_box), 
			    gtk_vseparator_new (), 
			    FALSE, FALSE, 0);
	
	/**/
	
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 0);
	gtk_box_pack_start (GTK_BOX (main_box), vbox, FALSE, FALSE, 0);
	
	gtk_box_pack_start (GTK_BOX (vbox), data->load_button,
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), data->save_button,
			    FALSE, FALSE, 0);
	
	/**/

	gtk_widget_show_all (frame);

	/* set data */

	dir = g_strconcat (data->window->add_default_dir, "/", "*", NULL);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (file_sel), dir);
	g_free (dir);

	gtk_editable_select_region (GTK_EDITABLE (GTK_FILE_SELECTION (data->dialog)->selection_entry), 0, -1);

	g_object_set_data (G_OBJECT (file_sel), "fr_dialog_data", data);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_backup_files), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_dot_files), TRUE);

	gtk_widget_set_sensitive (data->exclude_symlinks, FALSE);
	gtk_widget_set_sensitive (data->exclude_other_fs, FALSE);
	gtk_widget_set_sensitive (data->exclude_files_entry, FALSE);

	/* signals */
	
	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (open_file_destroy_cb),
			  file_sel);

	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (file_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (file_sel));

	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (file_sel)->ok_button),
			  "clicked",
			  G_CALLBACK (open_file_ok_cb), 
			  file_sel);

	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (file_sel)->selection_entry), 
			  "changed",
			  G_CALLBACK (selection_entry_changed),
			  data);
	
	g_signal_connect (G_OBJECT (data->include_subfold_checkbutton),
			  "toggled", 
			  G_CALLBACK (include_subfold_toggled_cb),
			  data);

	g_signal_connect (G_OBJECT (data->exclude_files_checkbutton),
			  "toggled", 
			  G_CALLBACK (exclude_files_toggled_cb),
			  data);
	
	g_signal_connect (G_OBJECT (data->load_button),
			  "clicked",
			  G_CALLBACK (load_options_cb), 
			  file_sel);
	
	g_signal_connect (G_OBJECT (data->save_button),
			  "clicked",
			  G_CALLBACK (save_options_cb), 
			  file_sel);
	
	g_object_set_data (G_OBJECT (file_sel), "tooltips", tooltips);

	gtk_window_set_modal (GTK_WINDOW (file_sel),TRUE);
	gtk_widget_show (file_sel);
}





static gboolean
config_get_bool (const char *config_file,
		 const char *option)
{
	char     *path;
	gboolean  value;
	
	path = g_strconcat ("=", config_file, "=/Options/", option, NULL);
	value = gnome_config_get_bool (path);
	g_free (path);
	
	return value;
}


static char*
config_get_string (const char *config_file,
		   const char *option)
{
	char *path;
	char *value;
	
	path = g_strconcat ("=", config_file, "=/Options/", option, NULL);
	value = gnome_config_get_string (path);
	g_free (path);
	
	return value;
}


static void
load_options_ok_cb (GtkWidget *w,
		    GtkWidget *opt_sel)
{
	GtkWidget  *file_sel;
	DialogData *data;
	FRWindow   *window;
	char       *opt_file_path;
	char       *file_sel_path, *path;
	gboolean    update;
	gchar      *base_dir;
	gboolean    recursive;
	gboolean    no_symlinks;
	gboolean    same_fs;
	gboolean    no_backup_files;
	gboolean    no_dot_files;
	gboolean    ignore_case;
	char       *exclude_files;
	
	file_sel = g_object_get_data (G_OBJECT (opt_sel), "fr_file_sel");
	data = g_object_get_data (G_OBJECT (file_sel), "fr_dialog_data");
	window = data->window;

	opt_file_path = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (opt_sel)));
	
        if (opt_file_path == NULL) {
		g_free (opt_file_path);
		gtk_widget_destroy (opt_sel);
                return;
	}
	
	/* Load options. */
	
	base_dir = config_get_string (opt_file_path, "base_dir");
	path = config_get_string (opt_file_path, "include_files");
	exclude_files = config_get_string (opt_file_path, "exclude_files");
	update = config_get_bool (opt_file_path, "update");
	recursive = config_get_bool (opt_file_path, "recursive");
	no_symlinks = config_get_bool (opt_file_path, "no_symlinks");
	same_fs = config_get_bool (opt_file_path, "same_fs");
	no_backup_files = config_get_bool (opt_file_path, "no_backup_files");
	no_dot_files = config_get_bool (opt_file_path, "no_dot_files");
	ignore_case = config_get_bool (opt_file_path, "ignore_case");
	
	file_sel_path = g_strconcat (base_dir, "/", path, NULL);

	/* Sync widgets with options. */

	gtk_file_selection_complete (GTK_FILE_SELECTION (file_sel),
				     file_sel_path);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->add_if_newer_checkbutton), update);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton), recursive);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_symlinks), no_symlinks);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_other_fs), same_fs);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_backup_files), no_backup_files);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_dot_files), no_dot_files);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->ignore_case), ignore_case);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->exclude_files_checkbutton), ! only_spaces (exclude_files));
	gtk_entry_set_text (GTK_ENTRY (data->exclude_files_entry), exclude_files);
	
	/**/
	
	g_free (base_dir);
	g_free (path);
	g_free (file_sel_path);
	g_free (exclude_files);
	g_free (opt_file_path);
	
	gtk_widget_destroy (opt_sel);
}


static void 
load_options_cb (GtkWidget  *w,
		 GtkWidget  *file_sel)
{
	GtkWidget  *opt_sel;
	char       *options_dir;
	char       *options_dir_slash;
	
	options_dir = get_home_relative_dir (RC_OPTIONS_DIR);
	options_dir_slash = g_strconcat (options_dir, "/", NULL);

	ensure_dir_exists (options_dir, 0700);
	
	opt_sel = gtk_file_selection_new (_("Load Options"));
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (opt_sel), 
					 options_dir_slash);

	g_free (options_dir);
	g_free (options_dir_slash);
	
	g_object_set_data (G_OBJECT (opt_sel), "fr_file_sel", file_sel);
	
	/* Signals */
	
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (opt_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (opt_sel));
	
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (opt_sel)->ok_button),
			  "clicked",
			  G_CALLBACK (load_options_ok_cb), 
			  opt_sel);
	
	gtk_window_set_modal (GTK_WINDOW (opt_sel), TRUE);
	gtk_widget_show (opt_sel);
}





static void
config_set_bool (const char *config_file,
		 const char *option,
		 gboolean    value)
{
       char *path;
       
       path = g_strconcat ("=", config_file, "=/Options/", option, NULL);
       gnome_config_set_bool (path, value);

       g_free (path);
}


static void
config_set_string (const char *config_file,
		   const char *option,
		   const char *value)
{
	char *path;
	
	path = g_strconcat ("=", config_file, "=/Options/", option, NULL);
	gnome_config_set_string (path, value);

	g_free (path);
}


static void
save_options_ok_cb (GtkWidget *w,
		    GtkWidget *opt_sel)
{
	DialogData *data;
	FRWindow   *window;
	GtkWidget  *file_sel;
	char       *opt_file_path;
	char       *file_sel_path;
	char       *path;
	gboolean    update;
	gchar      *base_dir;
	gboolean    recursive;
	gboolean    no_symlinks;
	gboolean    same_fs;
	gboolean    no_backup_files;
	gboolean    no_dot_files;
	gboolean    ignore_case;
	const char *exclude_files;
	
	file_sel = g_object_get_data (G_OBJECT (opt_sel), "fr_file_sel");
	data = g_object_get_data (G_OBJECT (file_sel), "fr_dialog_data");
	window = data->window;
	
	opt_file_path = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (opt_sel)));

        if (opt_file_path == NULL) {
		g_free (opt_file_path);
		gtk_widget_destroy (opt_sel);
                return;
	}
	
	/* Get options. */
	
	file_sel_path = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (file_sel)));
	path = remove_ending_separator (file_sel_path);
	base_dir = remove_level_from_path (path);
	
	update = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->add_if_newer_checkbutton));
	
	recursive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->include_subfold_checkbutton));
	no_symlinks = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_symlinks));
	same_fs = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_other_fs));
	no_backup_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_backup_files));
	no_dot_files = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_dot_files));
	ignore_case = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->ignore_case));
	
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->exclude_files_checkbutton))) {
		exclude_files = gtk_entry_get_text (GTK_ENTRY (data->exclude_files_entry));
		if (only_spaces (exclude_files))
			exclude_files = NULL;
	} else
		exclude_files = NULL;
	
	/* Save options. */
	
	config_set_string (opt_file_path, "base_dir", base_dir);
	config_set_string (opt_file_path, "include_files", file_name_from_path (path));
	config_set_string (opt_file_path, "exclude_files", exclude_files);
	config_set_bool   (opt_file_path, "update", update);
	config_set_bool   (opt_file_path, "recursive", recursive);
	config_set_bool   (opt_file_path, "no_symlinks", no_symlinks);
	config_set_bool   (opt_file_path, "same_fs", same_fs);
	config_set_bool   (opt_file_path, "no_backup_files", no_backup_files);
	config_set_bool   (opt_file_path, "no_dot_files", no_dot_files);
	config_set_bool   (opt_file_path, "ignore_case", ignore_case);
	gnome_config_sync ();

	/**/
	
	g_free (path);
	g_free (base_dir);
	g_free (opt_file_path);
	g_free (file_sel_path);
	
	gtk_widget_destroy (opt_sel);
}


static void 
save_options_cb (GtkWidget  *w,
		 GtkWidget  *file_sel)
{
	GtkWidget *opt_sel;
	char      *options_dir;
	char      *options_dir_slash;

	options_dir = get_home_relative_dir (RC_OPTIONS_DIR);	
	options_dir_slash = g_strconcat (options_dir, "/", NULL);

	ensure_dir_exists (options_dir, 0700);
	
	opt_sel = gtk_file_selection_new (_("Save Options"));
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (opt_sel), 
					 options_dir_slash);

	g_free (options_dir);
	g_free (options_dir_slash);
	
	g_object_set_data (G_OBJECT (opt_sel), "fr_file_sel", file_sel);
	
	/* Signals */
	
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (opt_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (opt_sel));
	
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (opt_sel)->ok_button),
			  "clicked",
			  G_CALLBACK (save_options_ok_cb), 
			  opt_sel);
	
	gtk_window_set_modal (GTK_WINDOW (opt_sel), TRUE);
	gtk_widget_show (opt_sel);
}
