/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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
#include <glib.h>
#include <glib/gi18n.h>
#ifdef ENABLE_INTROSPECTION
#  include <girepository.h>
#endif
#include "fr-application.h"

int
main (int argc, char **argv)
{
#ifdef ENABLE_INTROSPECTION
	const char *introspect_dump_prefix = "--introspect-dump=";

	if (argc == 2 && g_str_has_prefix (argv[1], introspect_dump_prefix)) {
		g_autoptr (GError) error = NULL;
		if (!g_irepository_dump (argv[1] + strlen(introspect_dump_prefix), &error)) {
			g_critical ("Failed to dump introspection data: %s", error->message);
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	}
#endif

	// We need to run gtk_init() before GtkApplication startup,
	// otherwise non-ASCII CLI arguments will trip up the parser.
	gtk_init ();
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_set_prgname ("org.gnome.FileRoller");

	GtkApplication *app = fr_application_new ();
	int status = g_application_run (G_APPLICATION (app), argc, argv);

	g_object_unref (app);

	return status;
}
