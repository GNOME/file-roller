/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2004 Free Software Foundation, Inc.
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
#include <gnome.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gnome.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "actions.h"
#include "dlg-add-files.h"
#include "dlg-add-folder.h"
#include "dlg-extract.h"
#include "dlg-delete.h"
#include "dlg-open-with.h"
#include "dlg-password.h"
#include "main.h"
#include "gtk-utils.h"
#include "window.h"
#include "file-utils.h"
#include "fr-process.h"
#include "gconf-utils.h"
#include "dlg-prop.h"


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
	FRWindow *archive_window;
	gboolean  new_window;

	new_window = window->archive_present;

	if (new_window) 
		archive_window = window_new ();
	else
		archive_window = window;

	/* Pass the add_after_creation options to the new window. */
	if (archive_window != window) {
		archive_window->add_after_creation = window->add_after_creation;
		if (archive_window->add_after_creation) {
			archive_window->dropped_file_list = window->dropped_file_list;
			window->dropped_file_list = NULL;
		}
		window->add_after_creation = FALSE;
	}

	if (window_archive_new (archive_window, path)) {
		gtk_window_present (GTK_WINDOW (archive_window->app));
		gtk_widget_destroy (file_sel);
	} else if (new_window)
		window_close (archive_window);
}


typedef struct {
	char *name;
	char *ext;
} FileTypeDescription;


static FileTypeDescription write_type_desc[] = { 
	{ N_("Automatic"),                             NULL},
	{ N_("Ar (.ar)"),                              ".ar" },
	{ N_("Arj (.arj)"),                            ".arj" },
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
	{ N_("Zip (.zip)"),                            ".zip" },
	{ N_("Zoo (.zoo)"),                            ".zoo" }
};


static char *
get_full_path (GtkWidget *file_sel)
{
	GtkWidget   *combo_box;
	char        *full_path;
	char        *path;
	const char  *filename;
	int          file_type_idx;

	combo_box = g_object_get_data (G_OBJECT (file_sel), "fr_combo_box");
	path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));

	if ((path == NULL) || (*path == 0))
		return NULL;

	filename = file_name_from_path (path);
	if ((filename == NULL) || (*filename == 0)) {
		g_free (path);
		return NULL;
	}
	
	file_type_idx = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
	if (file_type_idx > 0) {
		full_path = g_strconcat (path, 
					 write_type_desc[file_type_idx].ext,
					 NULL);
		g_free (path);
	} else {
		full_path = path;
	}

	return full_path;
}


static void 
new_file_response_cb (GtkWidget *w,
		      gint response,
		      GtkWidget *file_sel)
{
	FRWindow *window;
	gchar    *path;
	gchar    *dir;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");

	path = get_full_path (file_sel);

	if ((path == NULL) || (*path == 0)) {
		GtkWidget *dialog;

		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not create the archive"),
						  _("You have to specify an archive name."),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}
	
	dir = remove_level_from_path (path);
	if (access (dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *dialog;

		g_free (dir);
		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not create the archive"),
						  _("You don't have permission to create an archive in this folder"),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}
	g_free (dir);

	/* if the user do not specify an extension use tgz as default */
	if (strchr (path, '.') == NULL) {
		char *new_path;
		new_path = g_strconcat (path, ".tgz", NULL);
		g_free (path);
		path = new_path;
	}

#ifdef DEBUG
	g_print ("create %s\n", path); 
#endif

	if (path_is_file (path)) {
		GtkWidget *dialog;
		int        r;

		dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_QUESTION,
						  _("The archive already exists.  Do you want to overwrite it?"),
						  NULL,
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  _("Overwrite"), GTK_RESPONSE_YES,
						  NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (r != GTK_RESPONSE_YES) {
			g_free (path);
			return;
		}

		if (unlink (path) != 0) {
			GtkWidget *dialog;
			dialog = _gtk_message_dialog_new (GTK_WINDOW (file_sel),
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not delete the old archive."),
							  NULL,
							  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			return;
		}
	} 

	new_archive (file_sel, window, path);
	g_free (path);
}


void
activate_action_new (GtkAction *action, 
		     gpointer   data)
{
	static const char * save_mime_type[] = {
		"application/x-ar",
		"application/x-arj",
		"application/x-bzip",
		"application/x-bzip-compressed-tar",
		"application/x-cd-image",
		"application/x-compress",
		"application/x-compressed-tar",
		"application/x-gzip",
		"application/x-java-archive",
		"application/x-jar",
		"application/x-lha",
		"application/x-lzop",
		"application/x-rar",
		"application/x-rar-compressed",
		"application/x-tar",
		"application/x-zoo",
		"application/zip"
	};
	FRWindow  *window = data;
	GtkWidget *file_sel;
	GtkWidget *hbox;
	GtkWidget *combo_box;
	GtkFileFilter *filter;
	int i;

	file_sel = gtk_file_chooser_dialog_new (
			_("New"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_NEW, GTK_RESPONSE_OK,
			NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; i < G_N_ELEMENTS (save_mime_type); i++)
		gtk_file_filter_add_mime_type (filter, save_mime_type[i]);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (file_sel), hbox);

	gtk_box_pack_start (GTK_BOX (hbox), 
			    gtk_label_new (_("Archive type:")),
			    FALSE, FALSE, 0);

	combo_box = gtk_combo_box_new_text ();
	for (i = 0; i < G_N_ELEMENTS (write_type_desc); i++)
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
					   _(write_type_desc[i].name));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	gtk_box_pack_start (GTK_BOX (hbox), combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);
	
	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	g_object_set_data (G_OBJECT (file_sel), "fr_combo_box", combo_box);
	g_object_set_data (G_OBJECT (window->app), "fr_file_sel", file_sel);
	
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (new_file_response_cb), 
			  file_sel);

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
open_file_response_cb (GtkWidget *w,
		       gint response,
		       GtkWidget *file_sel)
{
	FRWindow   *window = NULL;
	char *path;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");
	path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));

        if (path == NULL)
                return;

	if (window_archive_open (window, path, GTK_WINDOW (file_sel)))
		gtk_widget_destroy (file_sel);

	g_free (path);
}


void
activate_action_open (GtkAction *action, 
		      gpointer   data)
{
	static const char * open_mime_type[] = {
		"application/x-ar",
		"application/x-arj",
		"application/x-bzip",
		"application/x-bzip-compressed-tar",
		"application/x-cd-image",
		"application/x-compress",
		"application/x-compressed-tar",
		"application/x-deb",
		"application/x-gzip",
		"application/x-jar",
		"application/x-java-archive",
		"application/x-lha",
		"application/x-lzop",
		"application/x-lzop-compressed-tar",
		"application/x-rar",
		"application/x-rar-compressed",
		"application/x-rpm",
		"application/x-stuffit",
		"application/x-tar",
		"application/x-zip",
		"application/x-zoo",
		"application/zip"
	};
	GtkWidget     *file_sel;
	FRWindow      *window = data;
	GtkFileFilter *filter;
	int            i;

	file_sel = gtk_file_chooser_dialog_new (
			_("Open"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_OK,
			NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; i < G_N_ELEMENTS (open_mime_type); i++)
		gtk_file_filter_add_mime_type (filter, open_mime_type[i]);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (open_file_response_cb), 
			  file_sel);
 
	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (open_file_destroy_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show (file_sel);
}


/* -- save archive -- */


static void
save_file_destroy_cb (GtkWidget *w,
		      GtkWidget *file_sel)
{
}


static void 
save_file_response_cb (GtkWidget *w,
		       gint response,
		       GtkWidget *file_sel)
{
	FRWindow *window;
	char     *path;
	char     *dir;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (file_sel);
		return;
	}

	window = g_object_get_data (G_OBJECT (file_sel), "fr_window");

	path = get_full_path (file_sel);

	if ((path == NULL) || (*path == 0)) {
		GtkWidget *dialog;

		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not save the archive"),
						  _("You have to specify an archive name."),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}
	
	dir = remove_level_from_path (path);
	if (access (dir, R_OK | W_OK | X_OK) != 0) {
		GtkWidget *dialog;

		g_free (dir);
		g_free (path);

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_DESTROY_WITH_PARENT,
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not save the archive"),
						  _("You don't have permission to create an archive in this folder"),
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}
	g_free (dir);

#ifdef DEBUG
	g_print ("save as %s\n", path); 
#endif

	if (path_is_file (path)) {
		GtkWidget *dialog;
		int r;

		dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_QUESTION,
						  _("The archive already exists.  Do you want to overwrite it?"),
						  NULL,
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  _("Overwrite"), GTK_RESPONSE_YES,
						  NULL);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		if (r != GTK_RESPONSE_YES) {
			g_free (path);
			return;
		}

		if (unlink (path) != 0) {
			GtkWidget *dialog;
			dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not delete the old archive."),
							  NULL,
							  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (GTK_WIDGET (dialog));
			return;
		}
	} 

	window_archive_save_as (window, path);
	gtk_widget_destroy (file_sel);
	g_free (path);
}


void
activate_action_save_as (GtkAction *action, 
			 gpointer   data)
{
	static const char * save_mime_type[] = {
		"application/x-ar",
		"application/x-arj",
		"application/x-bzip",
		"application/x-bzip-compressed-tar",
		"application/x-cd-image",
		"application/x-compress",
		"application/x-compressed-tar",
		"application/x-gzip",
		"application/x-lha",
		"application/x-lzop",
		"application/x-jar",
		"application/x-java-archive",
		"application/x-rar",
		"application/x-rar-compressed",
		"application/x-tar",
		"application/x-zoo",
		"application/zip",
	};
	FRWindow  *window = data;
	GtkWidget *file_sel;
	GtkWidget *hbox;
	GtkWidget *combo_box;
	GtkFileFilter *filter;
	int i;

	file_sel = gtk_file_chooser_dialog_new (
			_("Save"),
			GTK_WINDOW (window->app),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_SAVE, GTK_RESPONSE_OK,
			NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; i < G_N_ELEMENTS (save_mime_type); i++)
		gtk_file_filter_add_mime_type (filter, save_mime_type[i]);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	/**/

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (hbox), 
			    gtk_label_new (_("Archive type:")),
			    FALSE, FALSE, 0);
	combo_box = gtk_combo_box_new_text ();
	for (i = 0; i < G_N_ELEMENTS (write_type_desc); i++)
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
					   _(write_type_desc[i].name));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	gtk_box_pack_start (GTK_BOX (hbox), combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);

	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (file_sel), hbox);	
	/**/

	g_object_set_data (G_OBJECT (file_sel), "fr_window", window);
	g_object_set_data (G_OBJECT (file_sel), "fr_combo_box", combo_box);
	
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (save_file_response_cb), 
			  file_sel);

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (save_file_destroy_cb),
			  file_sel);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show_all (file_sel);
}


/* -- copy and move archive implementation -- */


typedef struct {
	FRWindow  *window;
	gboolean   copy;
	gboolean   overwrite;
	GtkWidget *file_sel;
} CopyMoveData;


static void
file_sel_destroy_cb (GtkWidget    *w,
		     CopyMoveData *data)
{
	g_free (data);
}


static void 
copy_or_move_archive_response_cb (GtkWidget    *w,
				  int           response,
				  CopyMoveData *data)
{
	FRWindow    *window = data->window;
	char        *folder;
	char        *new_path;

	if ((response == GTK_RESPONSE_CANCEL) || (response == GTK_RESPONSE_DELETE_EVENT)) {
		gtk_widget_destroy (w);
		return;
	}

	gtk_widget_hide (data->file_sel);

	folder = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (data->file_sel));

	if ((folder == NULL) || (*folder == 0)) {
		g_free (folder);
		gtk_widget_show (data->file_sel);
		return;
	}

	new_path = g_build_filename (folder, file_name_from_path (window->archive_filename), NULL);
	g_free (folder);

	if (strcmp (new_path, window->archive_filename) == 0) {
		gtk_widget_show (data->file_sel);
		return;
	}

	if (path_is_file (new_path)) {
		GtkWidget *d;
		int        r;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("The archive already exists.  Do you want to overwrite it?"),
					     NULL,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Overwrite"), GTK_RESPONSE_YES,
					     NULL);
	
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		
		if (r != GTK_RESPONSE_YES) {
			g_free (new_path);
			gtk_widget_destroy (data->file_sel);
			return;
		}
	}
	
	if (data->copy) {
#ifdef DEBUG
		g_print ("cp %s %s\n", window->archive_filename, new_path);
#endif
		if (! file_copy (window->archive_filename, new_path)) {
			GtkWidget *dialog;
			dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not copy the archive"),
							  NULL,
							  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	} else {
#ifdef DEBUG
		g_print ("mv %s %s\n", window->archive_filename, new_path);
#endif
		if (file_move (window->archive_filename, new_path)) 
			window_archive_rename (window, new_path);

		else {
			GtkWidget *dialog;
			dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
							  GTK_DIALOG_DESTROY_WITH_PARENT,
							  GTK_STOCK_DIALOG_ERROR,
							  _("Could not move the archive"),
							  NULL,
							  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
							  NULL);
			gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	}

	g_free (new_path);
	gtk_widget_destroy (data->file_sel);
}


static void
copy_or_move_archive (FRWindow *window, 
		      gboolean  copy,
		      gboolean  overwrite)
{
	GtkWidget    *file_sel;
	CopyMoveData *data;

	data = g_new (CopyMoveData, 1);
	data->window = window;
	data->copy = copy;
	data->overwrite = overwrite;
	data->file_sel = file_sel = 
		gtk_file_chooser_dialog_new (
		     copy ? _("Copy") : _("Move"),
		     GTK_WINDOW (window->app),
		     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		     GTK_STOCK_CANCEL,
		     GTK_RESPONSE_CANCEL,
		     copy ? _("_Copy"): _("_Move"),
		     GTK_RESPONSE_OK,
		     NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (file_sel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel),
					     window->open_default_dir);
 
	g_signal_connect (G_OBJECT (file_sel),
			  "response", 
			  G_CALLBACK (copy_or_move_archive_response_cb), 
			  data);

	g_signal_connect (G_OBJECT (file_sel),
			  "destroy", 
			  G_CALLBACK (file_sel_destroy_cb),
			  data);

	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_widget_show (file_sel);
}


void
activate_action_copy_archive (GtkAction *action,
			      gpointer   data)
{
	copy_or_move_archive ((FRWindow *) data, TRUE, FALSE);
}


void
activate_action_move_archive (GtkAction *action,
			      gpointer   data)
{
	copy_or_move_archive ((FRWindow *) data, FALSE, FALSE);
}


/* -- rename archive -- */


static char*
remove_extension (const char *filename)
{
	const char *ext;

	ext = fr_archive_utils__get_file_name_ext (filename);
	if (ext == NULL)
		return g_strdup (filename);
	else
		return g_strndup (filename, strlen (filename) - strlen (ext));
}


void
activate_action_rename_archive (GtkAction *action,
				gpointer   data)
{
	FRWindow   *window = data;
	char       *name;
	char       *utf8_old_string;
	char       *utf8_string;
	char       *string;
	char       *dir, *filename, *new_filename;

	name = remove_extension (file_name_from_path (window->archive_filename));
	utf8_old_string = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);
	utf8_string = _gtk_request_dialog_run (GTK_WINDOW (window->app),
					       (GTK_DIALOG_DESTROY_WITH_PARENT 
						| GTK_DIALOG_MODAL),
					       _("Rename"),
					       _("New archive name (without extension)"),
					       utf8_old_string,
					       1024,
					       GTK_STOCK_CANCEL,
					       _("_Rename"));
	g_free (utf8_old_string);
	if (utf8_string == NULL)
		return;

	string = g_filename_from_utf8 (utf8_string, -1, NULL, NULL, NULL);
	g_free (utf8_string);

	filename = window->archive->filename;
	dir = remove_level_from_path (filename);
	new_filename = g_strconcat (dir, 
				    "/", 
				    file_name_from_path (string),
				    fr_archive_utils__get_file_name_ext (window->archive->filename),
				    NULL);
	g_free (dir);
	g_free (string);

	if (path_is_file (new_filename)) {
		GtkWidget *d;
		int        r;

		d = _gtk_message_dialog_new (GTK_WINDOW (window->app),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     _("The archive already exists.  Do you want to overwrite it?"),
					     NULL,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Overwrite"), GTK_RESPONSE_YES,
					     NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
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


static char *
get_trash_path (const char *filename)
{
	char        *e_filename;
	GnomeVFSURI *uri, *trash_uri;

	e_filename = gnome_vfs_escape_path_string (filename);
	uri = gnome_vfs_uri_new (e_filename);
	g_free (e_filename);

	gnome_vfs_find_directory (uri, 
				  GNOME_VFS_DIRECTORY_KIND_TRASH,
				  &trash_uri, 
				  TRUE,
				  TRUE,
				  0777);
	gnome_vfs_uri_unref (uri);

	if (trash_uri == NULL)
		return NULL;
	else {
		char *trash_path;
		trash_path = gnome_vfs_uri_to_string (trash_uri, GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);
		gnome_vfs_uri_unref (trash_uri);
		return trash_path;
	}
}


void
activate_action_delete_archive (GtkAction *action,
				gpointer   data)
{
	FRWindow *window = data;
	char     *trash_path;

#ifdef DEBUG
	g_print ("rm %s\n", window->archive->filename);
#endif

	trash_path = get_trash_path (window->archive->filename);

	if (trash_path == NULL) {
		GtkWidget *d;
		int        r;
		d = _gtk_yesno_dialog_new (GTK_WINDOW (window->app),
					   GTK_DIALOG_MODAL,
					   _("The archive cannot be moved to the Trash. Do you want to delete it permanently?"),
					   GTK_STOCK_NO,
					   GTK_STOCK_DELETE);
		
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
		r = gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (GTK_WIDGET (d));
		
		if (r == GTK_RESPONSE_YES) {
			if (unlink (window->archive->filename) == 0)
				window_archive_close (window);
		}
		
	} else {
		const char *filename;
		char       *dest_filename;
		
		filename = file_name_from_path (window->archive->filename);
		if (trash_path[strlen (trash_path) - 1] == '/')
			dest_filename = g_strconcat (trash_path, 
						     filename,
						     NULL);
		else
			dest_filename = g_strconcat (trash_path, 
						     "/", 
						     filename,
						     NULL);
		
		if (file_move (window->archive_filename, dest_filename))
			window_archive_close (window);
	}
	g_free (trash_path);
}


void
activate_action_test_archive (GtkAction *action,
			      gpointer   data)
{
	FRWindow *window = data;
	fr_archive_test (window->archive, window->password);
}


void
activate_action_properties (GtkAction *action,
			    gpointer   data)
{
	FRWindow *window = data;
	dlg_prop (window);
}


void
activate_action_close (GtkAction *action, 
		       gpointer   data)
{
	window_archive_close ((FRWindow*) data);
}


void
activate_action_quit (GtkAction *action, 
		      gpointer   data)
{
	FRWindow *window = data;
	window_close (window);
}


void
activate_action_add_files (GtkAction *action, 
			   gpointer   data)
{
	add_files_cb (NULL, data);
}


void
activate_action_add_folder (GtkAction *action, 
			    gpointer   data)
{
	add_folder_cb (NULL, data);
}


void
activate_action_extract (GtkAction *action, 
			 gpointer   data)
{
	dlg_extract (NULL, data);
}


void
activate_action_copy (GtkAction *action, 
		      gpointer   data)
{
	window_copy_selection ((FRWindow*) data);
}


void
activate_action_cut (GtkAction *action, 
		     gpointer   data)
{
	window_cut_selection ((FRWindow*) data);
}


void
activate_action_paste (GtkAction *action, 
		       gpointer   data)
{
	window_paste_selection ((FRWindow*) data);
}


void
activate_action_rename (GtkAction *action, 
			gpointer   data)
{
	window_rename_selection ((FRWindow*) data);
}


void
activate_action_delete (GtkAction *action, 
			gpointer   data)
{
	dlg_delete (NULL, data);
}


void
activate_action_select_all (GtkAction *action, 
			    gpointer   data)
{
	FRWindow *window = data;
	gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
activate_action_deselect_all (GtkAction *action, 
			      gpointer   data)
{
	FRWindow *window = data;
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->list_view)));
}


void
activate_action_open_with (GtkAction *action, 
			   gpointer   data)
{
	open_with_cb (NULL, (FRWindow*) data);
}


void
activate_action_view_or_open (GtkAction *action, 
			      gpointer   data)
{
	FRWindow *window = data;
	GList    *file_list;

	file_list = window_get_file_list_selection (window, FALSE, NULL);
	if (file_list == NULL) 
		return;

	window_view_or_open_file (window, (gchar*) file_list->data);
	path_list_free (file_list);	
}


void
activate_action_password (GtkAction *action, 
			  gpointer   data)
{
	dlg_password (NULL, (FRWindow*) data);
}


void
activate_action_view_toolbar (GtkAction *action, 
			      gpointer   data)
{
	eel_gconf_set_boolean (PREF_UI_TOOLBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}


void
activate_action_view_statusbar (GtkAction *action, 
				gpointer   data)
{
	eel_gconf_set_boolean (PREF_UI_STATUSBAR, gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}


void
activate_action_stop (GtkAction *action, 
		      gpointer   data)
{
	FRWindow *window = data;
	window_stop (window);
}


void
activate_action_reload (GtkAction *action, 
			gpointer   data)
{
	FRWindow *window = data;
	if (window->activity_ref == 0)
		window_archive_reload (window);
}


void
activate_action_sort_reverse_order (GtkAction *action, 
				    gpointer   data)
{
	FRWindow *window = data;
	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
		window->sort_type = GTK_SORT_DESCENDING;
	else
		window->sort_type = GTK_SORT_ASCENDING;
	window_update_list_order (window);
}


void
activate_action_last_output (GtkAction *action, 
			     gpointer   data)
{
	FRWindow *window = data;
	window_view_last_output (window, _("Last Output"));
}


void
activate_action_manual (GtkAction *action, 
			gpointer   data)
{
	FRWindow *window = data;
	GError   *err;
	
        err = NULL;  
        gnome_help_display ("file-roller", NULL, &err);
        
        if (err != NULL) {
                GtkWidget *dialog;
                
                dialog = _gtk_message_dialog_new (GTK_WINDOW (window->app),
						  GTK_DIALOG_DESTROY_WITH_PARENT, 
						  GTK_STOCK_DIALOG_ERROR,
						  _("Could not display help"),
						  err->message,
						  GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
						  NULL);
                gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
                g_signal_connect (G_OBJECT (dialog), "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  NULL);
                
                gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
                
                gtk_widget_show (dialog);
                
                g_error_free (err);
        }
}


void
activate_action_about (GtkAction *action, 
		       gpointer   data)
{
	FRWindow         *window = data;
	static GtkWidget *about = NULL;
	GdkPixbuf        *logo;
	const char       *authors[] = {
		"Paolo Bacchilega <paolo.bacchilega@libero.it>", NULL
	};
	const char       *documenters [] = {
		"Alexander Kirillov", 
		"Breda McColgan",
		NULL
	};
	const char       *translator_credits = _("translator_credits");

	if (about != NULL) {
		gtk_window_present (GTK_WINDOW (about));
		return;
	}

	logo = gdk_pixbuf_new_from_file (PIXMAPSDIR "/file-roller.png", NULL);
	about = gnome_about_new (_("File Roller"), 
				 VERSION,
				 "Copyright \xc2\xa9 2001-2004 Free Software Foundation, Inc.",
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
