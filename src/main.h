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

#ifndef MAIN_H
#define MAIN_H


#include <glib.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "preferences.h"
#include "fr-process.h"
#include "fr-window.h"


typedef struct {
	FrWindow  *window;
	FRProcess *process;
	char      *filename;
	char      *e_filename;
	char      *temp_dir;
} ViewerData;

typedef struct {
	FrWindow           *window;
	FRProcess          *process;
	char               *command;
	GnomeVFSMimeApplication *app;
	GList              *file_list;
	char               *temp_dir;
} CommandData;


void  viewer_done      (ViewerData  *vdata);
void  command_done     (CommandData *cdata);


extern GList       *WindowList;
extern GList       *ViewerList;
extern GList       *CommandList;
extern gint         ForceDirectoryCreation;
extern GHashTable  *ProgramsCache;

extern FRFileTypeDescription file_type_desc[];
extern FRFileType single_file_save_type[]; /* File types that can be saved when
					    * a single file is selected.  
					    * Includes single file compressors
					    * such as gzip, compress, etc. */
extern FRFileType save_type[];             /* File types that can be saved. */
extern FRFileType open_type[];             /* File types that can be opened. */

#endif /* MAIN_H */
