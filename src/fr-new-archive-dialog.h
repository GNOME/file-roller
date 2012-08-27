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

#define FR_TYPE_NEW_ARCHIVE_DIALOG            (fr_new_archive_dialog_get_type ())
#define FR_NEW_ARCHIVE_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_NEW_ARCHIVE_DIALOG, FrNewArchiveDialog))
#define FR_NEW_ARCHIVE_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_NEW_ARCHIVE_DIALOG, FrNewArchiveDialogClass))
#define FR_IS_NEW_ARCHIVE_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_NEW_ARCHIVE_DIALOG))
#define FR_IS_NEW_ARCHIVE_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_NEW_ARCHIVE_DIALOG))
#define FR_NEW_ARCHIVE_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), FR_TYPE_NEW_ARCHIVE_DIALOG, FrNewArchiveDialogClass))

typedef struct _FrNewArchiveDialog FrNewArchiveDialog;
typedef struct _FrNewArchiveDialogClass FrNewArchiveDialogClass;
typedef struct _FrNewArchiveDialogPrivate FrNewArchiveDialogPrivate;

struct _FrNewArchiveDialog {
	GtkDialog parent_instance;
	FrNewArchiveDialogPrivate *priv;
};

struct _FrNewArchiveDialogClass {
	GtkDialogClass parent_class;
};

GType           fr_new_archive_dialog_get_type            (void);
GtkWidget *     fr_new_archive_dialog_new                 (const char          *title,
							   GtkWindow           *parent,
							   FrNewArchiveAction   action,
							   GFile               *folder,
							   const char          *default_name);
GFile *         fr_new_archive_dialog_get_file            (FrNewArchiveDialog  *dialog,
							   const char         **mime_type);
const char *    fr_new_archive_dialog_get_password        (FrNewArchiveDialog  *dialog);
gboolean        fr_new_archive_dialog_get_encrypt_header  (FrNewArchiveDialog  *dialog);
int             fr_new_archive_dialog_get_volume_size     (FrNewArchiveDialog  *dialog);

#endif /* FR_NEW_ARCHIVE_DIALOG_H */
