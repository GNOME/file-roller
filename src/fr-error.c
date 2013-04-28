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


#include <config.h>
#include "fr-error.h"


GQuark
fr_error_quark (void)
{
	static GQuark quark;

        if (!quark)
                quark = g_quark_from_static_string ("FrError");

        return quark;
}


G_DEFINE_BOXED_TYPE (FrError,
		     fr_error,
		     fr_error_copy,
		     fr_error_free);


FrError *
fr_error_new (FrErrorType  type,
	      int          status,
	      GError      *gerror)
{
	FrError *error;

	error = g_new0 (FrError, 1);
	fr_error_set (error, type, status, gerror);

	return error;
}


FrError *
fr_error_copy (FrError *error)
{
	if (error != NULL)
		return fr_error_new (error->type, error->status, error->gerror);
	else
		return NULL;
}


void
fr_error_free (FrError *error)
{
	if (error == NULL)
		return;
	g_clear_error (&error->gerror);
	g_free (error);
}


void
fr_error_set (FrError     *error,
	      FrErrorType  type,
	      int          status,
	      GError      *gerror)
{
	error->type = type;
	error->status = status;
	if (gerror != error->gerror) {
		g_clear_error (&error->gerror);
		if (gerror != NULL)
			error->gerror = g_error_copy (gerror);
	}
}


void
fr_error_take_gerror (FrError *error,
		      GError  *gerror)
{
	if (gerror != error->gerror) {
		g_clear_error (&error->gerror);
		error->gerror = gerror;
	}
}


void
fr_error_clear_gerror (FrError *error)
{
	g_clear_error (&error->gerror);
}


void
fr_clear_error (FrError **error)
{
	if ((error == NULL) || (*error == NULL))
		return;

	fr_error_free (*error);
	*error = NULL;
}
