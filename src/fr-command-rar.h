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

#ifndef FR_COMMAND_RAR_H
#define FR_COMMAND_RAR_H

#include <glib.h>
#include "file-data.h"
#include "fr-command.h"
#include "fr-process.h"

#define FR_TYPE_COMMAND_RAR            (fr_command_rar_get_type ())
#define FR_COMMAND_RAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_COMMAND_RAR, FRCommandRar))
#define FR_COMMAND_RAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_COMMAND_RAR, FRCommandRarClass))
#define FR_IS_COMMAND_RAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_COMMAND_RAR))
#define FR_IS_COMMAND_RAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_COMMAND_RAR))
#define FR_COMMAND_RAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_COMMAND_RAR, FRCommandRarClass))

typedef struct _FRCommandRar       FRCommandRar;
typedef struct _FRCommandRarClass  FRCommandRarClass;

struct _FRCommandRar
{
	FRCommand  __parent;

	gboolean list_started;
	gboolean odd_line;
	FileData *fdata;
};

struct _FRCommandRarClass
{
	FRCommandClass __parent_class;
};

GType        fr_command_rar_get_type        (void);
FRCommand*   fr_command_rar_new             (FRProcess *process,
					     const char *filename);

#endif /* FR_COMMAND_RAR_H */
