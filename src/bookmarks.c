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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "bookmarks.h"
#include "glib-utils.h"
#include "file-utils.h"


Bookmarks *
bookmarks_new (gchar *rc_filename)
{
	Bookmarks *bookmarks;

	g_return_val_if_fail (rc_filename != NULL, NULL);

	bookmarks = g_new (Bookmarks, 1);
	bookmarks->list = NULL;
	bookmarks->rc_filename = g_strdup (rc_filename);
	bookmarks->max_lines = 50;

	return bookmarks;
}


static void
bookmarks_free_data (Bookmarks *bookmarks)
{
	if (bookmarks->list != NULL) {
		path_list_free (bookmarks->list);
		bookmarks->list = NULL;
	}
}


void
bookmarks_free (Bookmarks *bookmarks)
{
	g_return_if_fail (bookmarks != NULL);

	bookmarks_free_data (bookmarks);

	if (bookmarks->rc_filename != NULL)
		g_free (bookmarks->rc_filename);

	g_free (bookmarks);
}


void
bookmarks_add (Bookmarks   *bookmarks,
	       const gchar *path)
{
	GList *scan;

	g_return_if_fail (bookmarks != NULL);
	g_return_if_fail (path != NULL);	

	for (scan = bookmarks->list; scan; scan = scan->next) 
		if (strcmp (path, scan->data) == 0) 
			return;

	bookmarks->list = g_list_prepend (bookmarks->list, g_strdup (path));
}


void
bookmarks_remove (Bookmarks *bookmarks,
		  const gchar *path)
{
	GList *link;

	g_return_if_fail (bookmarks != NULL);
	g_return_if_fail (path != NULL);	

	link = bookmarks->list;
	while (link) {
		if (strcmp ((gchar*) link->data, path) == 0)
			break;
		link = link->next;
	}
	if (link == NULL)
		return;

	bookmarks->list = g_list_remove_link (bookmarks->list, link);

	g_free (link->data);
	g_list_free (link);
}


#define MAX_LINE_LENGTH 4096
void
bookmarks_load_from_disk (Bookmarks *bookmarks)
{
	gchar  line [MAX_LINE_LENGTH];
	gchar *path;
	FILE  *f;

	g_return_if_fail (bookmarks != NULL);

	bookmarks_free_data (bookmarks);

	path = get_home_relative_dir (bookmarks->rc_filename);

	f = fopen (path, "r");
	g_free (path);

	if (!f)	return;

	while (fgets (line, sizeof (line), f)) {
		gchar *path;

		if (line[0] != '"')
			continue;

		line[strlen (line) - 2] = 0;
		path = line + 1;

		bookmarks->list = g_list_prepend (bookmarks->list, 
						  g_strdup (path));
	}
	bookmarks->list = g_list_reverse (bookmarks->list);
	fclose (f);
}
#undef MAX_LINE_LENGTH	


void
bookmarks_write_to_disk (Bookmarks *bookmarks)
{
	FILE  *f;
	gchar *path;
	GList *scan;
	gint   lines;

	g_return_if_fail (bookmarks != NULL);

	path = get_home_relative_dir (bookmarks->rc_filename);

	f = fopen (path, "w+");
	g_free (path);
	
	if (!f)	{
		debug (DEBUG_INFO, "ERROR opening bookmark file\n");
		return;
	}

	/* write the file list. */
	lines = 0;
	scan = bookmarks->list;
	while ((lines < bookmarks->max_lines) && scan) {
		if (! fprintf (f, "\"%s\"\n", (gchar*) scan->data)) {
			debug (DEBUG_INFO, "ERROR saving to bookmark file\n");
			fclose (f);
			return;
		}
		lines++;
		scan = scan->next;
	}
	fclose (f);
}


void
bookmarks_set_max_lines (Bookmarks *bookmarks,
			 gint max_lines)
{
	g_return_if_fail (bookmarks != NULL);
	bookmarks->max_lines = max_lines;
}
