/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors:
 *   James Willcox <jwillcox@cs.indiana.edu>
 */


#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include "egg-recent-item.h"

EggRecentItem *
egg_recent_item_new (void)
{
	EggRecentItem *item;

	item = g_new (EggRecentItem, 1);

	item->private = FALSE;
	item->uri = NULL;
	item->mime_type = NULL;
	item->app = NULL;
	item->refcount = 1;

	return item;
}

static void
egg_recent_item_free (EggRecentItem *item)
{
	g_free (item->uri);
	g_free (item->mime_type);
	g_free (item->app);
	g_free (item);
}

EggRecentItem *
egg_recent_item_ref (EggRecentItem *item)
{
	item->refcount++;
	return item;
}

EggRecentItem *
egg_recent_item_unref (EggRecentItem *item)
{
	item->refcount--;

	if (item->refcount == 0) {
		egg_recent_item_free (item);
	}

	return item;
}


EggRecentItem * 
egg_recent_item_new_from_uri (const gchar *uri)
{
	EggRecentItem *item;

	g_return_val_if_fail (uri != NULL, NULL);

	item = egg_recent_item_new ();

	if (!egg_recent_item_set_uri (item ,uri)) {
		egg_recent_item_free (item);
		return NULL;
	}
	
	item->mime_type = gnome_vfs_get_mime_type (item->uri);

	if (!item->mime_type)
		item->mime_type = g_strdup (GNOME_VFS_MIME_TYPE_UNKNOWN);

	return item;
}


gboolean
egg_recent_item_set_uri (EggRecentItem *item, const gchar *uri)
{
	gchar *utf8_uri;

	/* if G_BROKEN_FILENAMES is not set, this should succede */
	if (g_utf8_validate (uri, -1, NULL)) {
		item->uri = gnome_vfs_make_uri_from_input (uri);
	} else {
		utf8_uri = g_filename_to_utf8 (uri, -1, NULL, NULL, NULL);

		if (utf8_uri == NULL) {
			g_warning ("Couldn't convert URI to UTF-8");
			return FALSE;
		}

		if (g_utf8_validate (utf8_uri, -1, NULL)) {
			item->uri = gnome_vfs_make_uri_from_input (utf8_uri);
		} else {
			g_free (utf8_uri);
			return FALSE;
		}

		g_free (utf8_uri);
	}

	return TRUE;
}

gchar * 
egg_recent_item_get_uri (const EggRecentItem *item)
{
	return g_strdup (item->uri);
}

G_CONST_RETURN gchar * 
egg_recent_item_peek_uri (const EggRecentItem *item)
{
	return item->uri;
}

gchar * 
egg_recent_item_get_uri_utf8 (const EggRecentItem *item)
{
	/* this could fail, but it's not likely, since we've already done it
	 * once in set_uri()
	 */
	return g_filename_to_utf8 (item->uri, -1, NULL, NULL, NULL);
}

gchar *
egg_recent_item_get_uri_for_display (const EggRecentItem *item)
{
	return gnome_vfs_format_uri_for_display (item->uri);
}

void 
egg_recent_item_set_mime_type (EggRecentItem *item, const gchar *mime)
{
	item->mime_type = g_strdup (mime);
}

gchar * 
egg_recent_item_get_mime_type (const EggRecentItem *item)
{
	return g_strdup (item->mime_type);
}

void 
egg_recent_item_set_timestamp (EggRecentItem *item, time_t timestamp)
{
	if (timestamp == (time_t) -1)
		time (&timestamp);

	item->timestamp = timestamp;
}

time_t 
egg_recent_item_get_timestamp (const EggRecentItem *item)
{
	return item->timestamp;
}

void
egg_recent_item_set_private (EggRecentItem *item, gboolean priv)
{
	item->private = priv;
}

gboolean
egg_recent_item_get_private (const EggRecentItem *item)
{
	return item->private;
}

char *
egg_recent_item_get_application (EggRecentItem *item)
{
	return g_strdup (item->app);
}

void
egg_recent_item_set_application (EggRecentItem *item,
				 const char *app)
{
	item->app = g_strdup (app);
}
     
GType
egg_recent_item_get_type (void)
{
	static GType boxed_type = 0;
	
	if (!boxed_type) {
		boxed_type = g_boxed_type_register_static ("EggRecentItem",
					(GBoxedCopyFunc)egg_recent_item_ref,
					(GBoxedFreeFunc)egg_recent_item_unref);
	}
	
	return boxed_type;
}
