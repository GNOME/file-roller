/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2003 Free Software Foundation, Inc.
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

#ifndef __FR_ERROR_H__
#define __FR_ERROR_H__

#include <glib.h>
#include <glib-object.h>

#define FR_ERROR fr_error_quark ()
#define FR_TYPE_ERROR (fr_error_get_type ())

typedef enum { /*< skip >*/
	FR_ERROR_NONE,
	FR_ERROR_GENERIC,
	FR_ERROR_COMMAND_ERROR,
	FR_ERROR_COMMAND_NOT_FOUND,
	FR_ERROR_EXITED_ABNORMALLY,
	FR_ERROR_SPAWN,
	FR_ERROR_STOPPED,
	FR_ERROR_ASK_PASSWORD,
	FR_ERROR_MISSING_VOLUME,
	FR_ERROR_IO_CHANNEL,
	FR_ERROR_BAD_CHARSET,
	FR_ERROR_UNSUPPORTED_FORMAT
} FrErrorType;

typedef struct {
	GError      *gerror;
	FrErrorType  type;
	int          status;
} FrError;

GQuark     fr_error_quark          (void);
GType      fr_error_get_type       (void);
FrError *  fr_error_new            (FrErrorType   type,
		 		    int           status,
			 	    GError       *gerror);
FrError *  fr_error_copy           (FrError      *error);
void       fr_error_free           (FrError      *error);
void       fr_error_set            (FrError      *error,
				    FrErrorType   type,
				    int           status,
				    GError       *gerror);
void       fr_error_take_gerror    (FrError      *error,
				    GError       *gerror);
void       fr_error_clear_gerror   (FrError      *error);
void       fr_clear_error          (FrError     **error);

#endif /* __FR_ERROR_H__ */
