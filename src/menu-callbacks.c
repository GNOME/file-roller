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
#include <sys/stat.h>
#include <fcntl.h>
#include <gnome.h>
#include "main.h"
#include "gtk-utils.h"
#include "window.h"
#include "file-utils.h"
#include "fr-process.h"
#include "gconf-utils.h"
#include "menu-callbacks.h"


void
new_window_cb (GtkWidget *widget, 
	       void      *data)
{
	FRWindow *window;

	window = window_new ();
	gtk_widget_show (window->app);
}


void
close_window_cb (GtkWidget *widget, 
		 void      *data)
{
	window_close ((FRWindow *) data);
}


/* -- new archive -- */


static void
new_file_destroy_cb (GtkWidget *w,
		     GtkWidget *file_sel)
{
}


static void
new_archive (GtkWidget *file_sel, 
	     FRWindow  *window, 
	     gchar     *path)
{
	window_archive_new (window, path);	
	gtk_widget_destroy (file_sel);
}


static guint
opt_menu_get_active_idx (GtkWidget *opt_menu)
{
        GtkWidget *item;
        guint      idx;
        GList     *scan;
        GtkWidget *menu;

        menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (opt_menu));
        item = gtk_menu_get_active (GTK_MENU (menu));

        idx = 0;
        scan = GTK_MENU_SHELL (menu)->children;
        while (scan && (item != scan->data)) {
                idx++;
                scan = scan->next;
        }

        return idx;
}


typedef struct {
	char *name;
	char *ext;
} FileTypeDescription;


FileTypeDescription type_desc[] = { 
	{ N_("Automatic"),                             NULL},
	{ N_("Ear (.ear)"),                            ".ear" },
	{ N_("Jar (.jar)"),                            ".jar" },
	{ N_("Lha (.lzh)"),                            ".lzh" },
	{ N_("Rar (.rar)"),                            ".rar" },
	{ N_("Tar uncompressed (.tar)"),               ".tar" }, 
	{ N_("Tar compressed with bzip (.tar.bz)"),    ".tar.bz" }, 
	{ N_("Tar compressed with bzip2 (.tar.bz2)"),  ".tar.bz2" }, 
	{ N_("Tar compressed with gzip (.tar.gz)"),    ".tar.gz" }, 
	{ N_("Tar compressed with lzop (.tar.lzo)"),   ".tar.lzo" }, 
	{ N_("Tar compressed with compress (.tar.Z)"), ".tar.Z" }, 
	{ N_("War (.war)"),                            ".war" },
	{ N_("Zip (.zip)"),                            ".zip" }
};

static int type_desc_n = sizeof (type_desc) / sizeof (FileTypeDescription);


static char *
get_full_path (GtkWidget *file_sel)
{
	GtkWidget   *opt_menu;
	char        *full_path;
	const char  *path;
	int          file_type_idx;

	opt_menu = g_object_get_data (G_OBJECT (file_sel), "fr_opt_menu");
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (file_sel));

	file_type_idx = opt_menu_get_active_idx (opt_menu);
	if (file_type_idx > 0) 
		full_path = g_strconcat (path, 
					 type_desc[file_type_idx].ext,
					 NULL);
	else
		full_path = g_strdup (path);

	return full_path;
}


static void 
new_file_ok_cb (GtkWidget *w,
		GtkWidget *file_sel)
{
	FRWindow *window;
	gchar    *path;
	gchar    *dir;

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");

	path = get_full_path (file_sel);
        if (path == NULL) {
		gtk_widget_destroy (file_sel);
                return;
	}
	
	dir = remove_level_from_path (path);
	if (access (dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *dialog;

		g_free (dir);
		g_free (path);

		dialog = gtk_message_dialog_new (GTK_WINDOW (window->app),
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("You don't have permission to create an archive in this folder"));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}
	g_free (dir);

#ifdef DEBUG
	g_print ("create %s\n", path); 
#endif

	if (path_is_file (path)) {
		GtkWidget *dialog;
		int r;

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_QUESTION,
						  _("Archive already exists.  Do you want to overwrite it ?"),
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  _("Overwrite"), GTK_RESPONSE_YES,
						  NULL);

		r = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (r != GTK_RESPONSE_YES) {
			g_free (path);
			return;
		}

		if (unlink (path) != 0) {
			GtkWidget *dialog;
			dialog = gtk_message_dialog_new (GTK_WINDOW (window->app),
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_CLOSE,
							 _("Could not delete old archive."));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			return;
		}
	} 

	new_archive (file_sel, window, path);
	g_free (path);
}


static GtkWidget *
build_file_type_menu (FRWindow *window)
{
        GtkWidget *menu;
        GtkWidget *item;
	int        i;

        menu = gtk_menu_new ();
        for (i = 0; i < type_desc_n; i++) {
                item = gtk_menu_item_new_with_label (_(type_desc[i].name));
		gtk_widget_show (item);
                gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }

	return menu;
}


void
new_archive_cb (GtkWidget *widget, 
		void      *data)
{
	FRWindow  *window = data;
	GtkWidget *file_sel;
	GtkWidget *frame;
	GtkWidget *vbox;
	GtkWidget *opt_menu;
	GtkWidget *menu;
	char      *dir;

	file_sel = gtk_file_selection_new (_("New Archive"));

	dir = g_strconcat (window->open_default_dir, "/", NULL);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (file_sel), dir);
	g_free (dir);

	/**/

	frame = gtk_frame_new (_("File type :"));
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
	gtk_box_pack_start (GTK_BOX (GTK_FILE_SELECTION (file_sel)->action_area), frame, TRUE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_container_add (GTK_CONTAINER (frame), vbox);

	opt_menu = gtk_option_menu_new ();
	menu = build_file_type_menu (window);
        gtk_option_menu_set_menu (GTK_OPTION_MENU (opt_menu), menu);
	gtk_widget_show (opt_menu);
	gtk_box_pack_start (GTK_BOX (vbox), opt_menu, FALSE, FALSE, 5);

	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	g_object_set_data (G_OBJECT (file_sel), "fr_opt_menu", opt_menu);
	
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (file_sel)->ok_button),
			  "clicked", 
			  G_CALLBACK (new_file_ok_cb), 
			  file_sel);

	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (file_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (file_sel));

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (new_file_destroy_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel),TRUE);
	gtk_widget_show_all (file_sel);
}


/* -- open archive -- */


static void
open_file_destroy_cb (GtkWidget *w,
		      GtkWidget *file_sel)
{
}


static void 
open_file_ok_cb (GtkWidget *w,
                 GtkWidget *file_sel)
{
	FRWindow *window;
	const gchar *path;

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (file_sel));

        if (path == NULL)
                return;

	window_archive_open (window, path);

	gtk_widget_destroy (file_sel);
}


void
open_archive_cb (GtkWidget *widget, 
		 void      *data)
{
	GtkWidget *file_sel;
	FRWindow *window = data;
	gchar *dir;

        file_sel = gtk_file_selection_new (_("Open Archive"));

	dir = g_strconcat (window->open_default_dir, "/", NULL);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (file_sel), dir);
	g_free (dir);

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (file_sel)->ok_button),
			  "clicked", 
			  G_CALLBACK (open_file_ok_cb), 
			  file_sel);
 
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (file_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (file_sel));

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (open_file_destroy_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel),TRUE);
	gtk_widget_show (file_sel);
}


void
close_archive_cb (GtkWidget *widget, 
		  void      *data)
{
	window_archive_close ((FRWindow*) data);
}


/* -- copy and move archive implementation -- */


typedef struct {
	FRWindow  *window;
	gboolean   copy;
	GtkWidget *file_sel;
} CopyMoveData;


static void
file_sel_destroy_cb (GtkWidget    *w,
		     CopyMoveData *data)
{
	g_free (data);
}


static void 
copy_archive_ok_cb (GtkWidget    *w,
		    CopyMoveData *data)
{
	FRWindow   *window;
	const char *path;
	char       *new_filename;

	gtk_widget_hide (data->file_sel);

	window = data->window;
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (data->file_sel));

	if (path == NULL) {
                gtk_widget_destroy (data->file_sel);
		return;
	}

	if (path[strlen (path) - 1] == '/')
		new_filename = g_strconcat (path, file_name_from_path (window->archive_filename), NULL);
	else
		new_filename = g_strconcat (path, "/", file_name_from_path (window->archive_filename), NULL);

	if (path_is_file (new_filename)) {
		GtkWidget *d;
		int        r;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("Archive already exists.  Do you want to overwrite it ?"),
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Overwrite"), GTK_RESPONSE_YES,
					     NULL);
	
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		
		if (r != GTK_RESPONSE_YES) {
			g_free (new_filename);
			gtk_widget_destroy (data->file_sel);
			return;
		}
	}
	
	if (data->copy) {
#ifdef DEBUG
		g_print ("cp %s %s\n", window->archive_filename, new_filename);
#endif
		if (! file_copy (window->archive_filename, new_filename)) {
			GtkWidget *dialog;
			dialog = gtk_message_dialog_new (GTK_WINDOW (window->app),
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_CLOSE,
							 _("Could not copy archive"));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	} else {
#ifdef DEBUG
		g_print ("mv %s %s\n", window->archive_filename, new_filename);
#endif
		if (file_move (window->archive_filename, new_filename)) 
			window_archive_rename (window, new_filename);
		else {
			GtkWidget *dialog;
			dialog = gtk_message_dialog_new (GTK_WINDOW (window->app),
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_ERROR,
							 GTK_BUTTONS_CLOSE,
							 _("Could not move archive"));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	}

	g_free (new_filename);
	gtk_widget_destroy (data->file_sel);
}


static void
copy_or_move_archive (FRWindow *window, 
		      gboolean  copy)
{
	GtkWidget    *file_sel;
	char         *dir;
	CopyMoveData *data;

	data = g_new (CopyMoveData, 1);
	data->window = window;
	data->copy = copy;
	data->file_sel = file_sel = gtk_file_selection_new (copy ? _("Copy archive") : _("Move archive"));

	dir = g_strconcat (window->open_default_dir, "/", NULL);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (file_sel), dir);
	g_free (dir);

	gtk_widget_set_sensitive (GTK_FILE_SELECTION (file_sel)->file_list,
				  FALSE);

	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (file_sel)->ok_button),
			  "clicked", 
			  G_CALLBACK (copy_archive_ok_cb), 
			  data);

	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (file_sel)->cancel_button),
				  "clicked", 
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (file_sel));

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (file_sel_destroy_cb),
			  data);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show (file_sel);
}


void
move_archive_cb (GtkWidget *widget, 
		 void      *data)
{
	copy_or_move_archive ((FRWindow *) data, FALSE);
}


void
copy_archive_cb (GtkWidget *widget, 
		 void      *data)
{
	copy_or_move_archive ((FRWindow *) data, TRUE);
}


/* -- rename archive -- */


static char*
remove_extension (const char *filename)
{
	const char *ext;

	ext = fr_archive_utils_get_file_name_ext (filename);
	if (ext == NULL)
		return g_strdup (filename);
	else
		return g_strndup (filename, strlen (filename) - strlen (ext));
}


void
rename_archive_cb (GtkWidget *widget, 
		   void      *data)
{	
	FRWindow   *window = data;
	char       *name;
	char       *utf8_old_string;
	char       *utf8_string;
	char       *string;
	char       *dir, *filename, *new_filename;

	name = remove_extension (file_name_from_path (window->archive_filename));
	utf8_old_string = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);
	utf8_string = _gtk_request_dialog_run (GTK_WINDOW (window->app),
					       (GTK_DIALOG_DESTROY_WITH_PARENT 
						| GTK_DIALOG_MODAL),
					       _("New archive name (without extension)"),
					       utf8_old_string,
					       1024,
					       GTK_STOCK_CANCEL,
					       _("_Rename"));
	g_free (utf8_old_string);
	if (utf8_string == NULL)
		return;

	string = g_locale_from_utf8 (utf8_string, -1, NULL, NULL, NULL);
	g_free (utf8_string);

	filename = window->archive->filename;
	dir = remove_level_from_path (filename);
	new_filename = g_strconcat (dir, 
				    "/", 
				    file_name_from_path (string),
				    fr_archive_utils_get_file_name_ext (window->archive->filename),
				    NULL);
	g_free (dir);
	g_free (string);

	if (path_is_file (new_filename)) {
		GtkWidget *d;
		int        r;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("Archive already exists.  Do you want to overwrite it ?"),
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Overwrite"), GTK_RESPONSE_YES,
					     NULL);
	
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		
		if (r != GTK_RESPONSE_YES) {
			g_free (new_filename);
			return;
		}
	}

#ifdef DEBUG
	g_print ("rename %s --> %s\n", filename, new_filename);
#endif

	if (rename (filename, new_filename) == 0) 
		window_archive_rename (window, new_filename);

	g_free (new_filename);
}


/* -- delete archive -- */


void
delete_archive_cb (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;
	GtkWidget *dialog;
	int r;

	dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					  GTK_DIALOG_MODAL,
					  GTK_STOCK_DIALOG_QUESTION,
					  _("Archive will be deleted, are you sure ?"),
					  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					  GTK_STOCK_DELETE, GTK_RESPONSE_YES,
					  NULL);

	r = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if (r == GTK_RESPONSE_YES) {
#ifdef DEBUG
		g_print ("rm %s\n", window->archive->filename);
#endif
		if (unlink (window->archive->filename) == 0)
			window_archive_close (window);
	}
}


void
quit_cb (GtkWidget *widget, 
	 void      *data)
{
	while (window_list)
                window_close ((FRWindow*) window_list->data);
}


void
view_cb (GtkWidget *widget, 
	 void      *data)
{
	FRWindow *window = data;
	GList    *file_list;

	file_list = window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL) 
		return;

	window_view_file (window, file_list->data);
	path_list_free (file_list);
}


void
view_or_open_cb (GtkWidget *widget, 
		 void *data)
{
	FRWindow *window = data;
	GList *file_list;

	file_list = window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL) 
		return;

	window_view_or_open_file (window, (gchar*) file_list->data);
	path_list_free (file_list);	
}


void
go_up_one_level_cb (GtkWidget *widget, 
		    void *data)
{
	window_go_up_one_level ((FRWindow*) data);
}


void
set_list_mode_flat_cb (GtkWidget *widget, 
		       void *data)
{
	GtkCheckMenuItem *mitem = GTK_CHECK_MENU_ITEM (widget);
        if (! mitem->active)
                return;
	window_set_list_mode ((FRWindow*) data, WINDOW_LIST_MODE_FLAT);
}


void
set_list_mode_as_dir_cb (GtkWidget *widget, 
			 void *data)
{
	GtkCheckMenuItem *mitem = GTK_CHECK_MENU_ITEM (widget);
        if (! mitem->active)
                return;
	window_set_list_mode ((FRWindow*) data, WINDOW_LIST_MODE_AS_DIR);
}


void
select_all_cb (GtkWidget *widget, 
	       void *data)
{
	FRWindow *window = data;
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
deselect_all_cb (GtkWidget *widget, 
		 void *data)
{
	FRWindow *window = data;
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
manual_cb (GtkWidget *widget, 
           void      *data)
{
	FRWindow *window = data;
	GError   *err;
	
        err = NULL;  
        gnome_help_display ("file-roller", NULL, &err);
        
        if (err != NULL) {
                GtkWidget *dialog;
                
                dialog = gtk_message_dialog_new (GTK_WINDOW (window->app),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 _("Could not display help: %s"),
                                                 err->message);
                
                g_signal_connect (G_OBJECT (dialog), "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  NULL);
                
                gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
                
                gtk_widget_show (dialog);
                
                g_error_free (err);
        }
}


void
about_cb (GtkWidget *widget, 
	  void *data)
{
	FRWindow         *window = data;
	static GtkWidget *about = NULL;
	GdkPixbuf        *logo;
	const char       *authors[] = {
		"Paolo Bacchilega", NULL
	};
	const char       *documenters [] = {
		"Alexander Kirillov", NULL
	};
	const char       *translator_credits = _("translator_credits");

	if (about != NULL) {
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	logo = gdk_pixbuf_new_from_file (PIXMAPSDIR "/file-roller.png", NULL);
	about = gnome_about_new ("File Roller", 
				 VERSION,
				 "Copyright (C) 2001 The Free Software Foundation, Inc.",
				 _("An archive manager for GNOME."),
				 authors,
				 documenters,
				 strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
				 logo);
	if (logo != NULL)
                g_object_unref (logo);

	gtk_window_set_destroy_with_parent (GTK_WINDOW (about), TRUE);
	gtk_window_set_transient_for (GTK_WINDOW (about), 
				      GTK_WINDOW (window->app));

	g_signal_connect (G_OBJECT (about), 
			  "destroy",
			  G_CALLBACK (gtk_widget_destroyed), 
			  &about);

	gtk_widget_show_all (about);
}


void
sort_list_by_name (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	window->sort_method = WINDOW_SORT_BY_NAME;
	window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
sort_list_by_type (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	window->sort_method = WINDOW_SORT_BY_TYPE;
	window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
sort_list_by_size (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	window->sort_method = WINDOW_SORT_BY_SIZE;
	window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
sort_list_by_time (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	window->sort_method = WINDOW_SORT_BY_TIME;
	window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
sort_list_by_path (GtkWidget *widget, 
		   void *data)
{
	FRWindow *window = data;

	if (! GTK_CHECK_MENU_ITEM (widget)->active)
		return;

	window->sort_method = WINDOW_SORT_BY_PATH;
	window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
sort_list_reversed (GtkWidget *widget, 
		    void *data)
{
	FRWindow *window = data;
	if (window->sort_type == GTK_SORT_ASCENDING)
		window->sort_type = GTK_SORT_DESCENDING;
	else
		window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
stop_cb (GtkWidget *widget, 
	 void *data)
{
	FRWindow *window = data;
	if (window->activity_ref > 0)
		fr_process_stop (window->archive->process);
}


void
reload_cb (GtkWidget *widget, 
	   void *data)
{
	FRWindow *window = data;
	if (window->activity_ref == 0)
		window_archive_reload (window);
}


void
test_cb (GtkWidget *widget, 
	 void      *data)
{
	FRWindow *window = data;
	fr_archive_test (window->archive, window->password);
}


void
last_output_cb (GtkWidget *widget, 
		void      *data)
{
	FRWindow *window = data;
	window_view_last_output (window);
}


void
view_toolbar_cb (GtkWidget *widget, 
		 void      *data)
{
	eel_gconf_set_boolean (PREF_UI_TOOLBAR, GTK_CHECK_MENU_ITEM (widget)->active);
}


void
view_statusbar_cb (GtkWidget *widget, 
		   void      *data)
{
	eel_gconf_set_boolean (PREF_UI_STATUSBAR, GTK_CHECK_MENU_ITEM (widget)->active);
}
