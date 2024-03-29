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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DLG_EXTRACT_H
#define DLG_EXTRACT_H

#include "fr-archive.h"
#include "fr-window.h"

void dlg_extract (GtkWidget *widget, gpointer data);
void dlg_extract_folder_from_sidebar (GtkWidget *widget, gpointer data);
void dlg_extract_all_by_default (GtkWidget *widget, gpointer callback_data);

#endif /* DLG_EXTRACT_H */
