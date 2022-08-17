/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2010 The Free Software Foundation, Inc.
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

#ifndef FR_INIT_H
#define FR_INIT_H

#include "preferences.h"
#include "fr-process.h"
#include "fr-window.h"

typedef struct {
	FrWindow  *window;
	FrProcess *process;
	char      *command;
	GAppInfo  *app;
	GList     *file_list;
	GFile     *temp_dir;
} FrCommandData;

extern GList                 *CommandList;
extern gint                   ForceDirectoryCreation;
extern GHashTable            *ProgramsCache;
extern GPtrArray             *Registered_Archives;
extern FrMimeTypeDescription  mime_type_desc[];
extern FrExtensionType        file_ext_type[];
extern int                    single_file_save_type[]; /* File types that can be saved when
 	 	 	 	 	 	 	* a single file is selected.
 	 	 	 	 	 	 	* Includes single file compressors
 	 	 	 	 	 	 	* such as gzip, compress, etc. */
extern int                    save_type[];             /* File types that can be saved. */
extern int                    open_type[];             /* File types that can be opened. */
extern int                    create_type[];           /* File types that can be created. */

GType        fr_get_archive_type_from_mime_type         (const char    *mime_type,
						      FrArchiveCaps  requested_capabilities);
GType        fr_get_preferred_archive_for_mime_type     (const char    *mime_type,
						      FrArchiveCaps  requested_capabilities);
void         fr_update_registered_archives_capabilities (void);
const char * _g_mime_type_get_from_extension         (const char    *ext);
const char * _g_mime_type_get_from_filename          (GFile         *file);
const char * fr_get_archive_filename_extension          (const char    *uri);
int          fr_get_mime_type_index                     (const char    *mime_type);
void         fr_sort_mime_types_by_extension            (int           *a);
void         fr_initialize_data                         (void);
void         fr_release_data                            (void);

#endif /* FR_INIT_H */
