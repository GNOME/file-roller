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

#ifndef MENU_CALLBACKS_H
#define MENU_CALLBACKS_H


void   close_window_cb               (GtkWidget *widget, 
				      void *data);

void   new_archive_cb                (GtkWidget *widget, 
				      void *data);

void   open_archive_cb               (GtkWidget *widget, 
				      void *data);

void   close_archive_cb              (GtkWidget *widget, 
				      void *data);

void   save_as_archive_cb            (GtkWidget *widget, 
				      void *data);

void   move_archive_cb               (GtkWidget *widget, 
				      void *data);

void   copy_archive_cb               (GtkWidget *widget, 
				      void *data);

void   rename_archive_cb             (GtkWidget *widget, 
				      void *data);

void   delete_archive_cb             (GtkWidget *widget, 
				      void *data);

void   quit_cb                       (GtkWidget *widget, 
				      void *data);

void   view_cb                       (GtkWidget *widget, 
				      void *data);

void   view_or_open_cb               (GtkWidget *widget, 
				      void *data);

void   go_home_cb                    (GtkWidget *widget, 
				      void *data);

void   go_up_one_level_cb            (GtkWidget *widget, 
				      void *data);

void   go_back_cb                    (GtkWidget *widget, 
				      void *data);

void   go_forward_cb                 (GtkWidget *widget, 
				      void *data);

void   set_list_mode_flat_cb         (GtkWidget *widget, 
				      void *data);

void   set_list_mode_as_dir_cb       (GtkWidget *widget, 
				      void *data);

void   sort_list_by_name             (GtkWidget *widget, 
				      void *data);

void   sort_list_by_type             (GtkWidget *widget, 
				      void *data);

void   sort_list_by_size             (GtkWidget *widget, 
				      void *data);

void   sort_list_by_time             (GtkWidget *widget, 
				      void *data);

void   sort_list_by_path             (GtkWidget *widget, 
				      void *data);

void   sort_list_reversed            (GtkWidget *widget, 
				      void *data);

void   select_all_cb                 (GtkWidget *widget, 
				      void *data);

void   deselect_all_cb               (GtkWidget *widget, 
				      void *data);

void   manual_cb                     (GtkWidget *widget, 
				      void *data);

void   about_cb                      (GtkWidget *widget, 
				      void *data);

void   stop_cb                       (GtkWidget *widget, 
				      void *data);

void   reload_cb                     (GtkWidget *widget, 
				      void *data);

void   test_cb                       (GtkWidget *widget, 
				      void *data);

void   last_output_cb                (GtkWidget *widget, 
				      void      *data);

void   view_toolbar_cb               (GtkWidget *widget, 
				      void      *data);

void   view_statusbar_cb             (GtkWidget *widget, 
				      void      *data);

void   rename_cb                     (GtkWidget *widget, 
				      void *data);

#endif /* MENU_CALLBACKS_H */
