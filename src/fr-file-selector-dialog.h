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

#ifndef FR_FILE_SELECTOR_DIALOG_H
#define FR_FILE_SELECTOR_DIALOG_H

#include <gtk/gtk.h>
#include "typedefs.h"

G_DECLARE_FINAL_TYPE (FrFileSelectorDialog, fr_file_selector_dialog, FR, FILE_SELECTOR_DIALOG, GtkDialog)

GtkWidget *     fr_file_selector_dialog_new                 (FrFileSelectorMode      mode,
							     const char             *title,
							     GtkWindow              *parent);
void            fr_file_selector_dialog_set_extra_widget    (FrFileSelectorDialog   *dialog,
							     GtkWidget              *extra_widget);

/**
 * fr_file_selector_dialog_get_extra_widget:
 * Returns: (transfer none)
 */
GtkWidget *     fr_file_selector_dialog_get_extra_widget    (FrFileSelectorDialog   *dialog);
void            fr_file_selector_dialog_set_current_folder  (FrFileSelectorDialog   *dialog,
							     GFile                  *folder);

/**
 * fr_file_selector_dialog_get_current_folder:
 * Returns: (transfer full)
 */
GFile *         fr_file_selector_dialog_get_current_folder  (FrFileSelectorDialog   *dialog);

/**
 * fr_file_selector_dialog_set_selected_files:
 * @files: (element-type GFile)
 */
void            fr_file_selector_dialog_set_selected_files  (FrFileSelectorDialog   *dialog,
							     GList                  *files);

/**
 * fr_file_selector_dialog_get_selected_files:
 * Returns: (element-type GFile) (transfer full)
 */
GList *         fr_file_selector_dialog_get_selected_files  (FrFileSelectorDialog   *dialog);

/**
 * fr_file_selector_dialog_get_action_map:
 * Returns: (transfer none)
 */
GSimpleActionGroup *fr_file_selector_dialog_get_action_map (FrFileSelectorDialog   *dialog);

#endif /* FR_FILE_SELECTOR_DIALOG_H */
