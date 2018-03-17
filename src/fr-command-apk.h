/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2006 The Free Software Foundation, Inc.
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

#ifndef FR_COMMAND_APK_H
#define FR_COMMAND_APK_H

#include <glib.h>
#include "fr-command-zip.h"

#define FR_TYPE_COMMAND_APK            (fr_command_apk_get_type ())
#define FR_COMMAND_APK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_COMMAND_APK, FrCommandApk))
#define FR_COMMAND_APK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_COMMAND_APK, FrCommandApkClass))
#define FR_IS_COMMAND_APK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_COMMAND_APK))
#define FR_IS_COMMAND_APK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_COMMAND_APK))
#define FR_COMMAND_APK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_COMMAND_APK, FrCommandApkClass))

typedef struct _FrCommandApk       FrCommandApk;
typedef struct _FrCommandApkClass  FrCommandApkClass;

struct _FrCommandApk
{
	FrCommandZip  __parent;
};

struct _FrCommandApkClass
{
	FrCommandZipClass __parent_class;
};

GType fr_command_apk_get_type (void);

#endif /* FR_COMMAND_APK_H */
