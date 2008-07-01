/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-vfs-extensions.h - gnome-vfs extensions.  Its likely some of these will 
                          be part of gnome-vfs in the future.

   Copyright (C) 1999, 2000 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Darin Adler <darin@eazel.com>
	    Pavel Cisler <pavel@eazel.com>
	    Mike Fleming  <mfleming@eazel.com>
            John Sullivan <sullivan@eazel.com>
	    Seth Nickell <snickell@stanford.edu>
*/

#ifndef GNOME_VFS_HELPERS_H
#define GNOME_VFS_HELPERS_H

#include <libgnomevfs/gnome-vfs-file-size.h>
#include <libgnomevfs/gnome-vfs-result.h>
#include <libgnomevfs/gnome-vfs-uri.h>

G_BEGIN_DECLS

#define	GNOME_VFS_X_TRASH_URI "trash:"

typedef void     (* GnomeVFSXReadFileCallback) (GnomeVFSResult result,
					  GnomeVFSFileSize file_size,
					  char *file_contents,
					  gpointer callback_data);
typedef gboolean (* GnomeVFSXReadMoreCallback) (GnomeVFSFileSize file_size,
					  const char *file_contents,
					  gpointer callback_data);

typedef struct GnomeVFSXReadFileHandle GnomeVFSXReadFileHandle;

/* Read an entire file at once with gnome-vfs. */
GnomeVFSResult     gnome_vfs_x_read_entire_file                  (const char           *uri,
							  int                  *file_size,
							  char                **file_contents);
GnomeVFSXReadFileHandle *gnome_vfs_x_read_entire_file_async            (const char           *uri,
							  int                         priority,
							  GnomeVFSXReadFileCallback   callback,
							  gpointer              callback_data);
GnomeVFSXReadFileHandle *gnome_vfs_x_read_file_async                   (const char           *uri,
							  int                         priority,
							  GnomeVFSXReadFileCallback   callback,
							  GnomeVFSXReadMoreCallback   read_more_callback,
							  gpointer              callback_data);
void               gnome_vfs_x_read_file_cancel                  (GnomeVFSXReadFileHandle    *handle);

gboolean           gnome_vfs_x_uri_is_trash                      (const char           *uri);
gboolean           gnome_vfs_x_uri_is_trash_folder               (const char           *uri);
gboolean           gnome_vfs_x_uri_is_in_trash                   (const char           *uri);

char *             gnome_vfs_x_format_uri_for_display            (const char           *uri);
char *             gnome_vfs_x_make_uri_from_input               (const char           *location);
char *             gnome_vfs_x_make_uri_from_shell_arg           (const char           *location);
char *             gnome_vfs_x_make_uri_canonical                (const char           *uri);
char *             gnome_vfs_x_make_uri_canonical_strip_fragment (const char           *uri);
char *             gnome_vfs_x_make_uri_from_half_baked_uri      (const char           *half_baked_uri);
gboolean           gnome_vfs_x_uris_match                        (const char           *uri_1,
							  const char           *uri_2);
gboolean           gnome_vfs_x_uris_match_ignore_fragments       (const char           *uri_1,
							  const char           *uri_2);
char *             gnome_vfs_x_uri_get_basename                  (const char           *uri);
char *		   gnome_vfs_x_uri_get_dirname 			 (const char *uri);

char *             gnome_vfs_x_uri_get_scheme                    (const char           *uri);
char *             gnome_vfs_x_uri_make_full_from_relative       (const char           *base_uri,
							  const char           *uri);

/* Convenience routine for simple file copying using text-based uris */
GnomeVFSResult     gnome_vfs_x_copy_uri_simple                   (const char           *source_uri,
							  const char           *dest_uri);

/* gnome-vfs cover to make a directory and parents */
GnomeVFSResult     gnome_vfs_x_make_directory_and_parents        (GnomeVFSURI          *uri,
							  guint                 permissions);

/* Convenience routine to test if a string is a remote URI. */
gboolean           gnome_vfs_x_is_remote_uri                     (const char           *uri);

G_END_DECLS

#endif /* GNOME_VFS_HELPERS_H */
