/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* 
 * Copyright (C) 2002 James Willcox
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more av.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author:  James Willcox  <jwillcox@gnome.org>
 */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade-xml.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libbonobo.h>
#include "file-roller-component.h"

#include <stdlib.h>


static char *
get_path_from_url (const char *url)
{
	GnomeVFSURI *uri     = NULL;
	char        *escaped = NULL;
	char        *path    = NULL;
	
	uri = gnome_vfs_uri_new (url);

	if (uri != NULL)
		escaped = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD);

	if (escaped != NULL)
		path = gnome_vfs_unescape_string (escaped, NULL);
	
	if (uri != NULL)
		gnome_vfs_uri_unref (uri);
	g_free (escaped);
	
	return path;
}


static void
impl_Bonobo_Listener_event (PortableServer_Servant servant,
			    const CORBA_char *event_name,
			    const CORBA_any *args,
			    CORBA_Environment *ev)
{
	FileRollerComponent *frc;
	const CORBA_sequence_CORBA_string *list;
	char    *cmd, *current_dir, *first_path;
	char    *cmd_option;
	GString *str;
	int      i;

	frc = FILE_ROLLER_COMPONENT (bonobo_object_from_servant (servant));
	list = (CORBA_sequence_CORBA_string *)args;

	g_return_if_fail (frc != NULL);
	g_return_if_fail (list != NULL);

	/* they could in fact be in different directories, but we will
	 * only look at the first one
	 */

	first_path = get_path_from_url (list->_buffer[0]);
	current_dir = g_path_get_dirname (first_path);

	str = g_string_new ("file-roller");

	if (strcmp (event_name, "AddToArchive") == 0) 
		cmd_option = g_strdup_printf ("--default-dir=\"%s\" --add", current_dir);

	else if (strcmp (event_name, "ExtractTo") == 0) 
		cmd_option = g_strdup_printf ("--default-dir=\"%s\" --extract", current_dir);

	else if (strcmp (event_name, "ExtractToSubfolder") == 0) {
		char *base, *dir, *strip;
		
		base = g_path_get_basename (first_path);

		strip = strstr (base, ".");

		if (strip != NULL) 
			/* strip the extension */
			strip[0] = '\0';

		else {
			char *tmp;

			/* append something pseudo-quasi-unique....if it
			 * exists, file-roller will complain, and they can
			 * try again specifying a folder.
			 */
			tmp = g_strdup_printf ("%s-1", base);
			g_free (base);

			base = tmp;
		}

		dir = g_strconcat (current_dir, "/", base, NULL);
		g_free (base);

		cmd_option = g_strdup_printf ("--force --extract-to=\"%s\"", dir);
		g_free (dir);

	} else if (strcmp (event_name, "ExtractHere") == 0) 
		cmd_option = g_strdup_printf ("--force --extract-to=\"%s\"", current_dir);

	g_free (first_path);

       	g_string_append_printf (str, " %s ", cmd_option);

	for (i = 0; i < list->_length; i++) {
		char *path = get_path_from_url (list->_buffer[i]);

		if (path == NULL) 
			continue;
		
		g_string_append_printf (str, " \"%s\"", path);
		g_free (path);
	}

	cmd = g_string_free (str, FALSE);

	g_spawn_command_line_async (cmd, NULL);

	g_free (cmd);
	g_free (cmd_option);
	g_free (current_dir);
}


/* initialize the class */
static void
file_roller_component_class_init (FileRollerComponentClass *class)
{
	POA_Bonobo_Listener__epv *epv = &class->epv;
	epv->event = impl_Bonobo_Listener_event;
}


static void
file_roller_component_init (FileRollerComponent *frc)
{
}


BONOBO_TYPE_FUNC_FULL (FileRollerComponent, 
		       Bonobo_Listener, 
		       BONOBO_TYPE_OBJECT,
		       file_roller_component);
