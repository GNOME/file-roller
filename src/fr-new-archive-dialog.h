/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2012 The Free Software Foundation, Inc.
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

#ifndef FR_NEW_ARCHIVE_DIALOG_H
#define FR_NEW_ARCHIVE_DIALOG_H

#include <gtk/gtk.h>

typedef enum {
	FR_NEW_ARCHIVE_ACTION_NEW_MANY_FILES,
	FR_NEW_ARCHIVE_ACTION_NEW_SINGLE_FILE,
	FR_NEW_ARCHIVE_ACTION_SAVE_AS
} FrNewArchiveAction;

G_DECLARE_FINAL_TYPE (FrNewArchiveDialog, fr_new_archive_dialog, FR, NEW_ARCHIVE_DIALOG, GtkDialog)

typedef void (*FrNewArchiveDialogCallback) (FrNewArchiveDialog *dialog,
					    GFile              *file,
					    const char         *mime_type,
					    gpointer            user_data);

GtkWidget *     fr_new_archive_dialog_new                 (const char          *title,
							   GtkWindow           *parent,
							   FrNewArchiveAction   action,
							   GFile               *folder,
							   const char          *default_name,
							   GFile               *original_file);
void		fr_new_archive_dialog_show                (FrNewArchiveDialog  *dialog);
void		fr_new_archive_dialog_show_options        (FrNewArchiveDialog  *self);

/**
 * fr_new_archive_dialog_set_files_to_add:
 * @file_list: (element-type GFile)
 */
void		fr_new_archive_dialog_set_files_to_add    (FrNewArchiveDialog  *dialog,
							   GList               *file_list);

void            fr_new_archive_dialog_get_file            (FrNewArchiveDialog  *dialog,
							   FrNewArchiveDialogCallback callback,
							   gpointer user_data);
const char *    fr_new_archive_dialog_get_password        (FrNewArchiveDialog  *dialog);
gboolean        fr_new_archive_dialog_get_encrypt_header  (FrNewArchiveDialog  *dialog);
int             fr_new_archive_dialog_get_volume_size     (FrNewArchiveDialog  *dialog);

#endif /* FR_NEW_ARCHIVE_DIALOG_H */
