/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* gnome-vfs-helpers.c - gnome-vfs extensions formerly known as
                         eel-vfs-extensions

   Copyright (C) 1999, 2000 Eazel, Inc.
   Copyright (C) 2001, Free Software Foundation

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gnome-vfs-helpers.h"

#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-find-directory.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-xfer.h>
#include <pthread.h>

#include <string.h>

#include <glib/gstrfuncs.h>
#include <glib/gutils.h>


#define READ_CHUNK_SIZE 8192

struct GnomeVFSXReadFileHandle {
	GnomeVFSAsyncHandle *handle;
	GnomeVFSXReadFileCallback callback;
	GnomeVFSXReadMoreCallback read_more_callback;
	gpointer callback_data;
	gboolean is_open;
	char *buffer;
	GnomeVFSFileSize bytes_read;
};

#undef PTHREAD_ASYNC_READ

#ifndef PTHREAD_ASYNC_READ
static void read_file_read_chunk (GnomeVFSXReadFileHandle *handle);
#endif

static int
stolen_strcmp (const char *string_a, const char *string_b)
{
	return strcmp (string_a == NULL ? "" : string_a,
		       string_b == NULL ? "" : string_b);
}
static gboolean
str_is_equal (const char *string_a, const char *string_b)
{
	return stolen_strcmp (string_a, string_b) == 0;
}

static gboolean
str_has_prefix (const char *haystack, const char *needle)
{
	const char *h, *n;

	/* Eat one character at a time. */
	h = haystack == NULL ? "" : haystack;
	n = needle == NULL ? "" : needle;
	do {
		if (*n == '\0') {
			return TRUE;
		}
		if (*h == '\0') {
			return FALSE;
		}
	} while (*h++ == *n++);
	return FALSE;
}

static gboolean
istr_has_prefix (const char *haystack, const char *needle)
{
	const char *h, *n;
	char hc, nc;

	/* Eat one character at a time. */
	h = haystack == NULL ? "" : haystack;
	n = needle == NULL ? "" : needle;
	do {
		if (*n == '\0') {
			return TRUE;
		}
		if (*h == '\0') {
			return FALSE;
		}
		hc = *h++;
		nc = *n++;
		hc = g_ascii_tolower (hc);
		nc = g_ascii_tolower (nc);
	} while (hc == nc);
	return FALSE;
}


GnomeVFSResult
gnome_vfs_x_read_entire_file (const char *uri,
			   int *file_size,
			   char **file_contents)
{
	GnomeVFSResult result;
	GnomeVFSHandle *handle;
	char *buffer;
	GnomeVFSFileSize total_bytes_read;
	GnomeVFSFileSize bytes_read;

	*file_size = 0;
	*file_contents = NULL;

	/* Open the file. */
	result = gnome_vfs_open (&handle, uri, GNOME_VFS_OPEN_READ);
	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* Read the whole thing. */
	buffer = NULL;
	total_bytes_read = 0;
	do {
		buffer = g_realloc (buffer, total_bytes_read + READ_CHUNK_SIZE);
		result = gnome_vfs_read (handle,
					 buffer + total_bytes_read,
					 READ_CHUNK_SIZE,
					 &bytes_read);
		if ((result != GNOME_VFS_OK) && (result != GNOME_VFS_ERROR_EOF)) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return result;
		}

		/* Check for overflow. */
		if ((total_bytes_read + bytes_read) < total_bytes_read) {
			g_free (buffer);
			gnome_vfs_close (handle);
			return GNOME_VFS_ERROR_TOO_BIG;
		}

		total_bytes_read += bytes_read;

		/*FIXME: I should not check bytes_read != 0, but there are problems with 
		 * ftp
		 */
	} while ((result == GNOME_VFS_OK) && (bytes_read != 0));

	/* Close the file. */

	result = gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		g_free (buffer);
		return result;
	}
	
	/* Return the file. */
	*file_size = total_bytes_read;
	*file_contents = g_realloc (buffer, total_bytes_read);
	return GNOME_VFS_OK;
}

#ifndef PTHREAD_ASYNC_READ
/* When close is complete, there's no more work to do. */
static void
read_file_close_callback (GnomeVFSAsyncHandle *handle,
			  GnomeVFSResult result,
			  gpointer callback_data)
{
}

/* Do a close if it's needed.
 * Be sure to get this right, or we have extra threads hanging around.
 */
static void
read_file_close (GnomeVFSXReadFileHandle *read_handle)
{
	if (read_handle->is_open) {
		gnome_vfs_async_close (read_handle->handle,
				       read_file_close_callback,
				       NULL);
		read_handle->is_open = FALSE;
	}
}

/* Close the file and then tell the caller we succeeded, handing off
 * the buffer to the caller.
 */
static void
read_file_succeeded (GnomeVFSXReadFileHandle *read_handle)
{
	read_file_close (read_handle);
	
	/* Reallocate the buffer to the exact size since it might be
	 * around for a while.
	 */
	(* read_handle->callback) (GNOME_VFS_OK,
				   read_handle->bytes_read,
				   g_realloc (read_handle->buffer,
					      read_handle->bytes_read),
				   read_handle->callback_data);

	g_free (read_handle);
}

/* Tell the caller we failed. */
static void
read_file_failed (GnomeVFSXReadFileHandle *read_handle, GnomeVFSResult result)
{
	read_file_close (read_handle);
	g_free (read_handle->buffer);
	
	(* read_handle->callback) (result, 0, NULL, read_handle->callback_data);
	g_free (read_handle);
}

/* A read is complete, so we might or might not be done. */
static void
read_file_read_callback (GnomeVFSAsyncHandle *handle,
				GnomeVFSResult result,
				gpointer buffer,
				GnomeVFSFileSize bytes_requested,
				GnomeVFSFileSize bytes_read,
				gpointer callback_data)
{
	GnomeVFSXReadFileHandle *read_handle;
	gboolean read_more;

	/* Do a few reality checks. */
	g_assert (bytes_requested == READ_CHUNK_SIZE);
	read_handle = callback_data;
	g_assert (read_handle->handle == handle);
	g_assert (read_handle->buffer + read_handle->bytes_read == buffer);
	g_assert (bytes_read <= bytes_requested);

	/* Check for a failure. */
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_EOF) {
		read_file_failed (read_handle, result);
		return;
	}

	/* Check for the extremely unlikely case where the file size overflows. */
	if (read_handle->bytes_read + bytes_read < read_handle->bytes_read) {
		read_file_failed (read_handle, GNOME_VFS_ERROR_TOO_BIG);
		return;
	}

	/* Bump the size. */
	read_handle->bytes_read += bytes_read;

	/* Read more unless we are at the end of the file. */
	if (bytes_read == 0 || result != GNOME_VFS_OK) {
		read_more = FALSE;
	} else {
		if (read_handle->read_more_callback == NULL) {
			read_more = TRUE;
		} else {
			read_more = (* read_handle->read_more_callback)
				(read_handle->bytes_read,
				 read_handle->buffer,
				 read_handle->callback_data);
		}
	}
	if (read_more) {
		read_file_read_chunk (read_handle);
		return;
	}

	/* If at the end of the file, we win! */
	read_file_succeeded (read_handle);
}

/* Start reading a chunk. */
static void
read_file_read_chunk (GnomeVFSXReadFileHandle *handle)
{
	handle->buffer = g_realloc (handle->buffer, handle->bytes_read + READ_CHUNK_SIZE);
	gnome_vfs_async_read (handle->handle,
			      handle->buffer + handle->bytes_read,
			      READ_CHUNK_SIZE,
			      read_file_read_callback,
			      handle);
}

/* Once the open is finished, read a first chunk. */
static void
read_file_open_callback (GnomeVFSAsyncHandle *handle,
			 GnomeVFSResult result,
			 gpointer callback_data)
{
	GnomeVFSXReadFileHandle *read_handle;
	
	read_handle = callback_data;
	g_assert (read_handle->handle == handle);

	/* Handle the failure case. */
	if (result != GNOME_VFS_OK) {
		read_file_failed (read_handle, result);
		return;
	}

	/* Handle success by reading the first chunk. */
	read_handle->is_open = TRUE;
	read_file_read_chunk (read_handle);
}

#else

typedef struct {
	GnomeVFSXReadFileCallback callback;
	GnomeVFSXReadMoreCallback more_callback;
	gpointer callback_data;
	pthread_mutex_t *callback_result_ready_semaphore;
	gboolean synch_callback_result;

	GnomeVFSResult result;
	GnomeVFSFileSize file_size;
	char *buffer;
} GnomeVFSXAsyncReadFileCallbackData;

static int
pthread_gnome_vfs_x_read_file_callback_idle_binder (void *cast_to_context)
{
	GnomeVFSXAsyncReadFileCallbackData *context;
	
	context = (GnomeVFSXAsyncReadFileCallbackData *)cast_to_context;

	if (context->more_callback) {
		g_assert (context->callback_result_ready_semaphore != NULL);
		/* Synchronous callback flavor, wait for the return value. */
		context->synch_callback_result = (* context->more_callback) (context->file_size, 
			context->buffer, context->callback_data);
		/* Got the result, release the master thread */
		pthread_mutex_unlock (context->callback_result_ready_semaphore);
	} else {
		/* Asynchronous callback flavor, don't wait for the result. */
		(* context->callback) (context->result, context->file_size, 
			context->buffer, context->callback_data);

		/* We assume ownership of data here in the async call and have to
		 * free it.
		 */
		g_free (context);
	}

	return FALSE;
}

static gboolean
pthread_gnome_vfs_x_read_file_callback_common (GnomeVFSXReadFileCallback callback,
	GnomeVFSXReadMoreCallback more_callback, gpointer callback_data, 
	GnomeVFSResult error, GnomeVFSFileSize file_size,
	char *buffer, pthread_mutex_t *callback_result_ready_semaphore)
{
	GnomeVFSXAsyncReadFileCallbackData *data;
	gboolean result;

	g_assert ((callback == NULL) != (more_callback == NULL));
	g_assert ((more_callback != NULL) == (callback_result_ready_semaphore != NULL));

	result = FALSE;
	data = g_new0 (GnomeVFSXAsyncReadFileCallbackData, 1);
	data->callback = callback;
	data->more_callback = more_callback;
	data->callback_data = callback_data;
	data->callback_result_ready_semaphore = callback_result_ready_semaphore;
	data->result = error;
	data->file_size = file_size;
	data->buffer = buffer;
	
	/* Set up the callback to get called in the main thread. */
	g_idle_add (pthread_gnome_vfs_x_read_file_callback_idle_binder, data);

	if (callback_result_ready_semaphore != NULL) {
		/* Block until callback deposits the return value. This is not optimal but we do it
		 * to emulate the gnome_vfs_x_read_file_async call behavior.
		 */
		pthread_mutex_lock (callback_result_ready_semaphore);
		result = data->synch_callback_result;

		/* In the synch call we still own data here and need to free it. */
		g_free (data);

	}

	return result;
}

static gboolean
pthread_gnome_vfs_x_read_file_synchronous_callback (GnomeVFSXReadMoreCallback callback,
	gpointer callback_data, GnomeVFSFileSize file_size,
	char *buffer, pthread_mutex_t *callback_result_ready_semaphore)
{
	return pthread_gnome_vfs_x_read_file_callback_common(NULL, callback,
		callback_data, GNOME_VFS_OK, file_size, buffer, callback_result_ready_semaphore);
}

static void
pthread_gnome_vfs_x_read_file_asynchronous_callback (GnomeVFSXReadFileCallback callback,
	gpointer callback_data, GnomeVFSResult result, GnomeVFSFileSize file_size,
	char *buffer)
{
	pthread_gnome_vfs_x_read_file_callback_common(callback, NULL,
		callback_data, result, file_size, buffer, NULL);
}

typedef struct {
	GnomeVFSXReadFileHandle handle;
	char *uri;
	volatile gboolean cancel_requested;
	/* Expose the synch callback semaphore to allow the cancel call to unlock it. */
	pthread_mutex_t *callback_result_ready_semaphore;
} GnomeVFSXAsyncReadFileData;

static void *
pthread_gnome_vfs_x_read_file_thread_entry (void *cast_to_data)
{
	GnomeVFSXAsyncReadFileData *data;
	GnomeVFSResult result;
	char *buffer;
	GnomeVFSFileSize total_bytes_read;
	GnomeVFSFileSize bytes_read;
	pthread_mutex_t callback_result_ready_semaphore;
	
	data = (GnomeVFSXAsyncReadFileData *)cast_to_data;
	buffer = NULL;
	total_bytes_read = 0;

	result = gnome_vfs_open ((GnomeVFSHandle **)&data->handle.handle, data->uri, GNOME_VFS_OPEN_READ);
	if (result == GNOME_VFS_OK) {
	
		if (data->handle.read_more_callback != NULL) {
			/* read_more_callback is a synchronous callback, allocate a semaphore
			 * to provide for synchoronization with the callback.
			 * We are using the default mutex attributes that give us a fast mutex
			 * that behaves like a semaphore.
			 */
			pthread_mutex_init (&callback_result_ready_semaphore, NULL);
			/* Grab the semaphore -- the next lock will block us and
			 * we will need the callback to unblock the semaphore.
			 */
			pthread_mutex_lock (&callback_result_ready_semaphore);
			data->callback_result_ready_semaphore = &callback_result_ready_semaphore;
		}
		for (;;) {
			if (data->cancel_requested) {
				/* Cancelled by the master. */
				result = GNOME_VFS_ERROR_INTERRUPTED;
				break;
			}

			buffer = g_realloc (buffer, total_bytes_read + READ_CHUNK_SIZE);
			/* FIXME bugzilla.eazel.com 5070:
			 * For a better cancellation granularity we should use gnome_vfs_read_cancellable
			 * here, adding a GnomeVFSContext to GnomeVFSXAsyncReadFileData.
			 */
			result = gnome_vfs_read ((GnomeVFSHandle *)data->handle.handle, buffer + total_bytes_read,
				READ_CHUNK_SIZE, &bytes_read);

			total_bytes_read += bytes_read;

			if (data->cancel_requested) {
				/* Cancelled by the master. */
				result = GNOME_VFS_ERROR_INTERRUPTED;
				break;
			}

			if (result != GNOME_VFS_OK) {
				if (result == GNOME_VFS_ERROR_EOF) {
					/* not really an error, just done reading */
					result = GNOME_VFS_OK;
				}
				break;
			}

			if (data->handle.read_more_callback != NULL
				&& !pthread_gnome_vfs_x_read_file_synchronous_callback (data->handle.read_more_callback,
					data->handle.callback_data, total_bytes_read, buffer, 
					&callback_result_ready_semaphore)) {
				/* callback doesn't want any more data */
				break;
			}

		}
		gnome_vfs_close ((GnomeVFSHandle *)data->handle.handle);
	}

	if (result != GNOME_VFS_OK) {
		/* Because of the error or cancellation, nobody will take the data we read, 
		 * delete the buffer here instead.
		 */
		g_free (buffer);
		buffer = NULL;
		total_bytes_read = 0;
	}

	/* Call the final callback. 
	 * If everything is OK, pass in the data read. 
	 * We are handing off the read buffer -- trim it to the actual size we need first
	 * so that it doesn't take up more space than needed.
	 */
	pthread_gnome_vfs_x_read_file_asynchronous_callback(data->handle.callback, 
		data->handle.callback_data, result, total_bytes_read, 
		g_realloc (buffer, total_bytes_read));

	if (data->handle.read_more_callback != NULL) {
		pthread_mutex_destroy (&callback_result_ready_semaphore);
	}

	g_free (data->uri);
	g_free (data);

	return NULL;
}

static GnomeVFSXReadFileHandle *
pthread_gnome_vfs_x_read_file_async(const char *uri, GnomeVFSXReadFileCallback callback, 
	GnomeVFSXReadMoreCallback read_more_callback, gpointer callback_data)
{
	GnomeVFSXAsyncReadFileData *data;
	pthread_attr_t thread_attr;
	pthread_t thread;

	data = g_new0 (GnomeVFSXAsyncReadFileData, 1);

	data->handle.callback = callback;
	data->handle.read_more_callback = read_more_callback;
	data->handle.callback_data = callback_data;
	data->cancel_requested = FALSE;
	data->uri = g_strdup (uri);

	pthread_attr_init (&thread_attr);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create (&thread, &thread_attr, pthread_gnome_vfs_x_read_file_thread_entry, data) != 0) {
		/* FIXME bugzilla.eazel.com 5071:
		 * Would be cleaner to call through an idle callback here.
		 */
		(*callback) (GNOME_VFS_ERROR_INTERNAL, 0, NULL, NULL);
		g_free (data);
		return NULL;
	}

	return (GnomeVFSXReadFileHandle *)data;
}

static void
pthread_gnome_vfs_x_read_file_async_cancel (GnomeVFSXReadFileHandle *handle)
{
	/* Must call this before the final callback kicks in. */
	GnomeVFSXAsyncReadFileData *data;

	data = (GnomeVFSXAsyncReadFileData *)handle;
	data->cancel_requested = TRUE;
	if (data->callback_result_ready_semaphore != NULL) {
		pthread_mutex_unlock (data->callback_result_ready_semaphore);
	}

	/* now the thread will die on it's own and clean up after itself */
}

#endif

/* Set up the read handle and start reading. */
GnomeVFSXReadFileHandle *
gnome_vfs_x_read_file_async (const char *uri,
			     int priority,
			     GnomeVFSXReadFileCallback callback,
			     GnomeVFSXReadMoreCallback read_more_callback,
			     gpointer callback_data)
{
#ifndef PTHREAD_ASYNC_READ
	GnomeVFSXReadFileHandle *handle;

	handle = g_new0 (GnomeVFSXReadFileHandle, 1);

	handle->callback = callback;
	handle->read_more_callback = read_more_callback;
	handle->callback_data = callback_data;

	gnome_vfs_async_open (&handle->handle,
			      uri,
			      GNOME_VFS_OPEN_READ,
			      priority,
			      read_file_open_callback,
			      handle);
	return handle;
#else
	return pthread_gnome_vfs_x_read_file_async(uri, callback, 
		read_more_callback, callback_data);
#endif
}

/* Set up the read handle and start reading. */
GnomeVFSXReadFileHandle *
gnome_vfs_x_read_entire_file_async (const char *uri,
				    int priority,
				    GnomeVFSXReadFileCallback callback,
				    gpointer callback_data)
{
	return gnome_vfs_x_read_file_async (uri, priority, callback, NULL, callback_data);
}

/* Stop the presses! */
void
gnome_vfs_x_read_file_cancel (GnomeVFSXReadFileHandle *handle)
{
#ifndef PTHREAD_ASYNC_READ
	gnome_vfs_async_cancel (handle->handle);
	read_file_close (handle);
	g_free (handle->buffer);
	g_free (handle);
#else

	pthread_gnome_vfs_x_read_file_async_cancel (handle);
#endif
}

gboolean
gnome_vfs_x_uri_is_trash (const char *uri)
{
	return istr_has_prefix (uri, "trash:")
		|| istr_has_prefix (uri, "gnome-trash:");
}

gboolean
gnome_vfs_x_uri_is_trash_folder (const char *uri)
{
	GnomeVFSURI *vfs_uri, *trash_vfs_uri;
	gboolean result;
	
	/* Use a check for the actual trash first so that the trash
	 * itself will be "in trash". There are fancier ways to do
	 * this, but lets start with this.
	 */
 	if (gnome_vfs_x_uri_is_trash (uri)) {
		return TRUE;
	}

        vfs_uri = gnome_vfs_uri_new (uri);
	if (vfs_uri == NULL) {
		return FALSE;
	}

	result = gnome_vfs_find_directory
		(vfs_uri, GNOME_VFS_DIRECTORY_KIND_TRASH,
		 &trash_vfs_uri, FALSE, FALSE, 0777) == GNOME_VFS_OK;

       if (result) {
		result = gnome_vfs_uri_equal (trash_vfs_uri, vfs_uri);			
		gnome_vfs_uri_unref (trash_vfs_uri);
        }
        
        gnome_vfs_uri_unref (vfs_uri);

	return result;
}


gboolean 
gnome_vfs_x_uri_is_in_trash (const char *uri)
{
	GnomeVFSURI *vfs_uri, *trash_vfs_uri;
	gboolean result;

	/* Use a check for the actual trash first so that the trash
	 * itself will be "in trash". There are fancier ways to do
	 * this, but lets start with this.
	 */
 	if (gnome_vfs_x_uri_is_trash (uri)) {
		return TRUE;
	}

        vfs_uri = gnome_vfs_uri_new (uri);
	if (vfs_uri == NULL) {
		return FALSE;
	}

	result = gnome_vfs_find_directory
		(vfs_uri, GNOME_VFS_DIRECTORY_KIND_TRASH,
		 &trash_vfs_uri, FALSE, FALSE, 0777) == GNOME_VFS_OK;

       if (result) {
		result = gnome_vfs_uri_equal (trash_vfs_uri, vfs_uri)
			|| gnome_vfs_uri_is_parent (trash_vfs_uri, vfs_uri, TRUE);
		gnome_vfs_uri_unref (trash_vfs_uri);
        }

        gnome_vfs_uri_unref (vfs_uri);

	return result;
}


/**
 * gnome_vfs_x_format_uri_for_display:
 *
 * Filter, modify, unescape and change URIs to make them appropriate
 * to display to users.
 * 
 * Rules:
 * 	file: URI's without fragments should appear as local paths
 * 	file: URI's with fragments should appear as file: URI's
 * 	All other URI's appear as expected
 *
 * @uri: a URI
 *
 * returns a g_malloc'd string
 **/
char *
gnome_vfs_x_format_uri_for_display (const char *uri) 
{
	char *canonical_uri, *path;

	g_return_val_if_fail (uri != NULL, g_strdup (""));

	canonical_uri = gnome_vfs_x_make_uri_canonical (uri);

	/* If there's no fragment and it's a local path. */
	path = gnome_vfs_get_local_path_from_uri (canonical_uri);
	if (path != NULL) {
		g_free (canonical_uri);
		return path;
	}

	g_free (path);
	return canonical_uri;
}

static gboolean
is_valid_scheme_character (char c)
{
	return g_ascii_isalnum (c) || c == '+' || c == '-' || c == '.';
}

static gboolean
has_valid_scheme (const char *uri)
{
	const char *p;

	p = uri;

	if (!is_valid_scheme_character (*p)) {
		return FALSE;
	}

	do {
		p++;
	} while (is_valid_scheme_character (*p));

	return *p == ':';
}

/**
 * gnome_vfs_x_make_uri_from_input:
 *
 * Takes a user input path/URI and makes a valid URI
 * out of it
 *
 * @location: a possibly mangled "uri"
 *
 * returns a newly allocated uri
 *
 **/
char *
gnome_vfs_x_make_uri_from_input (const char *location)
{
	char *stripped, *path, *uri;

	g_return_val_if_fail (location != NULL, g_strdup (""));

	/* Strip off leading and trailing spaces.
	 * This makes copy/paste of URIs less error-prone.
	 */
	stripped = g_strstrip (g_strdup (location));

	switch (stripped[0]) {
	case '\0':
		uri = g_strdup ("");
		break;
	case '/':
		uri = gnome_vfs_get_uri_from_local_path (stripped);
		break;
	case '~':
		path = gnome_vfs_expand_initial_tilde (stripped);
                /* deliberately falling into default case on fail */
		if (*path == '/') {
			uri = gnome_vfs_get_uri_from_local_path (path);
			g_free (path);
			break;
		}
                g_free (path);
                /* don't insert break here, read above comment */
	default:
		if (has_valid_scheme (stripped)) {
			uri = g_strdup (stripped);
		} else {
			uri = g_strconcat ("http://", stripped, NULL);
		}
	}

	g_free (stripped);

	return uri;
}

/* Note that NULL's and full paths are also handled by this function.
 * A NULL location will return the current working directory
 */
static char *
file_uri_from_local_relative_path (const char *location)
{
	char *current_dir;
	char *base_uri, *base_uri_slash;
	char *location_escaped;
	char *uri;

	current_dir = g_get_current_dir ();
	base_uri = gnome_vfs_get_uri_from_local_path (current_dir);
	/* g_get_current_dir returns w/o trailing / */
	base_uri_slash = g_strconcat (base_uri, "/", NULL);

	location_escaped = gnome_vfs_escape_path_string (location);

	uri = gnome_vfs_uri_make_full_from_relative (base_uri_slash, location_escaped);

	g_free (location_escaped);
	g_free (base_uri_slash);
	g_free (base_uri);
	g_free (current_dir);

	return uri;
}

/**
 * gnome_vfs_x_make_uri_from_shell_arg:
 *
 * Similar to gnome_vfs_x_make_uri_from_input, except that:
 * 
 * 1) guesses relative paths instead of http domains
 * 2) doesn't bother stripping leading/trailing white space
 * 3) doesn't bother with ~ expansion--that's done by the shell
 *
 * @location: a possibly mangled "uri"
 *
 * returns a newly allocated uri
 *
 **/
char *
gnome_vfs_x_make_uri_from_shell_arg (const char *location)
{
	char *uri;

	g_return_val_if_fail (location != NULL, g_strdup (""));

	switch (location[0]) {
	case '\0':
		uri = g_strdup ("");
		break;
	case '/':
		uri = gnome_vfs_get_uri_from_local_path (location);
		break;
	default:
		if (has_valid_scheme (location)) {
			uri = g_strdup (location);
		} else {
			uri = file_uri_from_local_relative_path (location);
		}
	}

	return uri;
}

char *
gnome_vfs_x_uri_get_basename (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	char *name;

	/* Make VFS version of URI. */
	vfs_uri = gnome_vfs_uri_new (uri);
	if (vfs_uri == NULL) {
		return NULL;
	}

	/* Extract name part. */
	name = gnome_vfs_uri_extract_short_name (vfs_uri);
	gnome_vfs_uri_unref (vfs_uri);

	return name;
}

char *
gnome_vfs_x_uri_get_dirname (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	char *name;

	/* Make VFS version of URI. */
	vfs_uri = gnome_vfs_uri_new (uri);
	if (vfs_uri == NULL) {
		return NULL;
	}

	/* Extract name part. */
	name = gnome_vfs_uri_extract_dirname (vfs_uri);
	gnome_vfs_uri_unref (vfs_uri);

	return name;
}


char *
gnome_vfs_x_uri_get_scheme (const char *uri)
{
	char *colon;

	g_return_val_if_fail (uri != NULL, NULL);

	colon = strchr (uri, ':');
	
	if (colon == NULL) {
		return NULL;
	}
	
	return g_strndup (uri, colon - uri);
}

static gboolean
gnome_vfs_x_uri_is_local_scheme (const char *uri)
{
	gboolean is_local_scheme;
	char *temp_scheme;
	int i;
	char *local_schemes[] = {"file:", "help:", "ghelp:", "gnome-help:",
				 "trash:", "man:", "info:", 
				 "hardware:", "search:", "pipe:",
				 "gnome-trash:", NULL};

	is_local_scheme = FALSE;
	for (temp_scheme = *local_schemes, i = 0; temp_scheme != NULL; i++, temp_scheme = local_schemes[i]) {
		is_local_scheme = istr_has_prefix (uri, temp_scheme);
		if (is_local_scheme) {
			break;
		}
	}
	

	return is_local_scheme;
}

static char *
gnome_vfs_x_handle_trailing_slashes (const char *uri)
{
	char *temp, *uri_copy;
	gboolean previous_char_is_column, previous_chars_are_slashes_without_column;
	gboolean previous_chars_are_slashes_with_column;
	gboolean is_local_scheme;

	g_assert (uri != NULL);

	uri_copy = g_strdup (uri);
	if (strlen (uri_copy) <= 2) {
		return uri_copy;
	}

	is_local_scheme = gnome_vfs_x_uri_is_local_scheme (uri);

	previous_char_is_column = FALSE;
	previous_chars_are_slashes_without_column = FALSE;
	previous_chars_are_slashes_with_column = FALSE;

	/* remove multiple trailing slashes */
	for (temp = uri_copy; *temp != '\0'; temp++) {
		if (*temp == '/' && !previous_char_is_column) {
			previous_chars_are_slashes_without_column = TRUE;
		} else if (*temp == '/' && previous_char_is_column) {
			previous_chars_are_slashes_without_column = FALSE;
			previous_char_is_column = TRUE;
			previous_chars_are_slashes_with_column = TRUE;
		} else {
			previous_chars_are_slashes_without_column = FALSE;
			previous_char_is_column = FALSE;
			previous_chars_are_slashes_with_column = FALSE;
		}

		if (*temp == ':') {
			previous_char_is_column = TRUE;
		}
	}

	if (*temp == '\0' && previous_chars_are_slashes_without_column) {
		if (is_local_scheme) {
			/* go back till you remove them all. */
			for (temp--; *(temp) == '/'; temp--) {
				*temp = '\0';
			}
		} else {
			/* go back till you remove them all but one. */
			for (temp--; *(temp - 1) == '/'; temp--) {
				*temp = '\0';
			}			
		}
	}

	if (*temp == '\0' && previous_chars_are_slashes_with_column) {
		/* go back till you remove them all but three. */
		for (temp--; *(temp - 3) != ':' && *(temp - 2) != ':' && *(temp - 1) != ':'; temp--) {
			*temp = '\0';
		}
	}


	return uri_copy;
}

char *
gnome_vfs_x_make_uri_canonical (const char *uri)
{
	char *canonical_uri, *old_uri, *p;
	gboolean relative_uri;

	relative_uri = FALSE;

	if (uri == NULL) {
		return NULL;
	}

	/* Convert "gnome-trash:<anything>" and "trash:<anything>" to
	 * "trash:".
	 */
	if (gnome_vfs_x_uri_is_trash (uri)) {
		return g_strdup (GNOME_VFS_X_TRASH_URI);
	}

	/* FIXME bugzilla.eazel.com 648: 
	 * This currently ignores the issue of two uris that are not identical but point
	 * to the same data except for the specific cases of trailing '/' characters,
	 * file:/ and file:///, and "lack of file:".
	 */

	canonical_uri = gnome_vfs_x_handle_trailing_slashes (uri);

	/* Note: In some cases, a trailing slash means nothing, and can
	 * be considered equivalent to no trailing slash. But this is
	 * not true in every case; specifically not for web addresses passed
	 * to a web-browser. So we don't have the trailing-slash-equivalence
	 * logic here, but we do use that logic in GnomeVFSXDirectory where
	 * the rules are more strict.
	 */

	/* Add file: if there is no scheme. */
	if (strchr (canonical_uri, ':') == NULL) {
		old_uri = canonical_uri;

		if (old_uri[0] != '/') {
			/* FIXME bugzilla.eazel.com 5069: 
			 *  bandaid alert. Is this really the right thing to do?
			 * 
			 * We got what really is a relative path. We do a little bit of
			 * a stretch here and assume it was meant to be a cryptic absolute path,
			 * and convert it to one. Since we can't call gnome_vfs_uri_new and
			 * gnome_vfs_uri_to_string to do the right make-canonical conversion,
			 * we have to do it ourselves.
			 */
			relative_uri = TRUE;
			canonical_uri = gnome_vfs_make_path_name_canonical (old_uri);
			g_free (old_uri);
			old_uri = canonical_uri;
			canonical_uri = g_strconcat ("file:///", old_uri, NULL);
		} else {
			canonical_uri = g_strconcat ("file:", old_uri, NULL);
		}
		g_free (old_uri);
	}

	/* Lower-case the scheme. */
	for (p = canonical_uri; *p != ':'; p++) {
		g_assert (*p != '\0');
		*p = g_ascii_tolower (*p);
	}

	if (!relative_uri) {
		old_uri = canonical_uri;
		canonical_uri = gnome_vfs_make_uri_canonical (canonical_uri);
		if (canonical_uri != NULL) {
			g_free (old_uri);
		} else {
			canonical_uri = old_uri;
		}
	}
	
	/* FIXME bugzilla.eazel.com 2802:
	 * Work around gnome-vfs's desire to convert file:foo into file://foo
	 * by converting to file:///foo here. When you remove this, check that
	 * typing "foo" into location bar does not crash and returns an error
	 * rather than displaying the contents of /
	 */
	if (str_has_prefix (canonical_uri, "file://")
	    && !str_has_prefix (canonical_uri, "file:///")) {
		old_uri = canonical_uri;
		canonical_uri = g_strconcat ("file:/", old_uri + 5, NULL);
		g_free (old_uri);
	}

	return canonical_uri;
}

char *
gnome_vfs_x_make_uri_canonical_strip_fragment (const char *uri)
{
	const char *fragment;
	char *without_fragment, *canonical;

	fragment = strchr (uri, '#');
	if (fragment == NULL) {
		return gnome_vfs_x_make_uri_canonical (uri);
	}

	without_fragment = g_strndup (uri, fragment - uri);
	canonical = gnome_vfs_x_make_uri_canonical (without_fragment);
	g_free (without_fragment);
	return canonical;
}

char *
gnome_vfs_x_make_uri_from_half_baked_uri (const char *half_baked_uri)
{
	/* A "half-baked URI" is a path with a file prefix in front of
	 * it. There are some interfaces where these are the norm,
	 * even though they are not correct URIs. This kind of URI is
	 * what gmc used to make, so it's the defacto standard for
	 * drag and drop.
	 */
	if (istr_has_prefix (half_baked_uri, "file:/")
	    && !istr_has_prefix (half_baked_uri, "file://")) {
		return gnome_vfs_get_uri_from_local_path (half_baked_uri + strlen ("file:"));
	}
	
	return gnome_vfs_x_make_uri_canonical (half_baked_uri);
}

static gboolean
uris_match (const char *uri_1, const char *uri_2, gboolean ignore_fragments)
{
	char *canonical_1, *canonical_2;
	gboolean result;

	if (ignore_fragments) {
		canonical_1 = gnome_vfs_x_make_uri_canonical_strip_fragment (uri_1);
		canonical_2 = gnome_vfs_x_make_uri_canonical_strip_fragment (uri_2);
	} else {
		canonical_1 = gnome_vfs_x_make_uri_canonical (uri_1);
		canonical_2 = gnome_vfs_x_make_uri_canonical (uri_2);
	}

	result = str_is_equal (canonical_1, canonical_2);

	g_free (canonical_1);
	g_free (canonical_2);
	
	return result;
}

gboolean
gnome_vfs_x_uris_match (const char *uri_1, const char *uri_2)
{
	return uris_match (uri_1, uri_2, FALSE);
}

gboolean
gnome_vfs_x_uris_match_ignore_fragments (const char *uri_1, const char *uri_2)
{
	return uris_match (uri_1, uri_2, TRUE);
}

/* convenience routine to use gnome-vfs to test if a string is a remote uri */
gboolean
gnome_vfs_x_is_remote_uri (const char *uri)
{
	gboolean is_local;
	GnomeVFSURI *vfs_uri;
	
	vfs_uri = gnome_vfs_uri_new (uri);
	is_local = gnome_vfs_uri_is_local (vfs_uri);
	gnome_vfs_uri_unref(vfs_uri);
	return !is_local;
}


GnomeVFSResult
gnome_vfs_x_make_directory_and_parents (GnomeVFSURI *uri, guint permissions)
{
	GnomeVFSResult result;
	GnomeVFSURI *parent_uri;

	/* Make the directory, and return right away unless there's
	   a possible problem with the parent.
	*/
	result = gnome_vfs_make_directory_for_uri (uri, permissions);
	if (result != GNOME_VFS_ERROR_NOT_FOUND) {
		return result;
	}

	/* If we can't get a parent, we are done. */
	parent_uri = gnome_vfs_uri_get_parent (uri);
	if (parent_uri == NULL) {
		return result;
	}

	/* If we can get a parent, use a recursive call to create
	   the parent and its parents.
	*/
	result = gnome_vfs_x_make_directory_and_parents (parent_uri, permissions);
	gnome_vfs_uri_unref (parent_uri);
	if (result != GNOME_VFS_OK) {
		return result;
	}

	/* A second try at making the directory after the parents
	   have all been created.
	*/
	result = gnome_vfs_make_directory_for_uri (uri, permissions);
	return result;
}

GnomeVFSResult
gnome_vfs_x_copy_uri_simple ( const char *source_uri, const char *dest_uri)
{
	GnomeVFSResult result;
	GnomeVFSURI *real_source_uri, *real_dest_uri;
	real_source_uri = gnome_vfs_uri_new (source_uri);
	real_dest_uri = gnome_vfs_uri_new (dest_uri);
		
	result = gnome_vfs_xfer_uri (real_source_uri, real_dest_uri,
					GNOME_VFS_XFER_RECURSIVE, GNOME_VFS_XFER_ERROR_MODE_ABORT,
					GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE,
					NULL, NULL);
		
	gnome_vfs_uri_unref (real_source_uri);
	gnome_vfs_uri_unref (real_dest_uri);
		
	return  result;
}

/*#if !defined (EEL_OMIT_SELF_CHECK)*/
/*
  void
  eel_self_check_vfs_extensions (void)
  {
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (""), "");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (" "), "");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (" / "), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (" /"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (" /home\n\n"), "file:///home");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (" \n\t"), "");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("!"), "http://!");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("#"), "http://#");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/ "), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/!"), "file:///!");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/#"), "file:///%23");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/%20"), "file:///%2520");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/%25"), "file:///%2525");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/:"), "file:///%3A");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/home"), "file:///home");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("/home/darin"), "file:///home/darin");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (":"), "http://:");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("::"), "http://::");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input (":://:://:::::::::::::::::"), "http://:://:://:::::::::::::::::");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:"), "file:");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:///%20"), "file:///%20");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:///%3F"), "file:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:///:"), "file:///:");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:///?"), "file:///?");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:///home/joe/some file"), "file:///home/joe/some file");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file://home/joe/some file"), "file://home/joe/some file");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("file:::::////"), "file:::::////");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("foo://foobar.txt"), "foo://foobar.txt");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("home"), "http://home");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("http://null.stanford.edu"), "http://null.stanford.edu");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("http://null.stanford.edu:80"), "http://null.stanford.edu:80");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("http://seth@null.stanford.edu:80"), "http://seth@null.stanford.edu:80");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("http:::::::::"), "http:::::::::");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("www.eazel.com"), "http://www.eazel.com");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_input ("http://null.stanford.edu/some file"), "http://null.stanford.edu/some file");

  EEL_CHECK_STRING_RESULT (eel_uri_get_scheme ("file:///var/tmp"), "file");
  EEL_CHECK_STRING_RESULT (eel_uri_get_scheme (""), NULL);
  EEL_CHECK_STRING_RESULT (eel_uri_get_scheme ("file:///var/tmp::"), "file");
  EEL_CHECK_STRING_RESULT (eel_uri_get_scheme ("man:ls"), "man");
  EEL_CHECK_BOOLEAN_RESULT (eel_uri_is_local_scheme ("file:///var/tmp"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uri_is_local_scheme ("http://www.yahoo.com"), FALSE);

  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("file:///////"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("file://foo/"), "file://foo");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("file://foo"), "file://foo");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("file://"), "file://");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("file:/"), "file:/");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("http://le-hacker.org"), "http://le-hacker.org");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("http://le-hacker.org/dir//////"), "http://le-hacker.org/dir/");
  EEL_CHECK_STRING_RESULT (eel_handle_trailing_slashes ("http://le-hacker.org/////"), "http://le-hacker.org/");
*/
/* eel_make_uri_canonical */

/* FIXME bugzilla.eazel.com 5072: this is a bizarre result from an empty string */
/*
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical (""), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri (""), "file:///");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:/"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///home/mathieu/"), "file:///home/mathieu");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///home/mathieu"), "file:///home/mathieu");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("ftp://mathieu:password@le-hackeur.org"), "ftp://mathieu:password@le-hackeur.org");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("ftp://mathieu:password@le-hackeur.org/"), "ftp://mathieu:password@le-hackeur.org/");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://le-hackeur.org"), "http://le-hackeur.org");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://le-hackeur.org/"), "http://le-hackeur.org/");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://le-hackeur.org/dir"), "http://le-hackeur.org/dir");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://le-hackeur.org/dir/"), "http://le-hackeur.org/dir/");
*/
/* FIXME bugzilla.eazel.com 5068: the "nested" URI loses some characters here. Maybe that's OK because we escape them in practice? */
/*
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("search://[file://]file_name contains stuff"), "search://[file/]file_name contains stuff");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("eazel-services:/~turtle"), "eazel-services:///~turtle");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("eazel-services:///~turtle"), "eazel-services:///~turtle");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/./."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/.//."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/.///."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a"), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/b/.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a///"), "file:///a/");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a"), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("../a"), "file:///../a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("..//a"), "file:///../a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a//."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a/."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical (".//a/."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a//."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a//.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a/.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical (".//a/.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a//.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical (".//a//.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a/b/.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/./a/b/.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/./b/.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/b/./.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/a/b/../."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/../.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("./a/b/../.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("././a/b/../.."), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/c/../.."), "file:///a");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/c/../../d"), "file:///a/d");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/../../d"), "file:///d");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/../../d"), "file:///../d");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/b/.././.././c"), "file:///c");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("a/.././.././b/c"), "file:///../b/c");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.eazel.com"), "http://www.eazel.com");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.eazel.com/"), "http://www.eazel.com/");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.eazel.com/dir"), "http://www.eazel.com/dir");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.eazel.com/dir/"), "http://www.eazel.com/dir/");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://yakk:womble@www.eazel.com:42/blah/"), "http://yakk:womble@www.eazel.com:42/blah/");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("FILE:///"), "file:///");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///trash"), "file:///trash");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///Users/mikef"), "file:///Users/mikef");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/trash"), "file:///trash");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("root"), "file:///root");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("/root"), "file:///root");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("//root"), "file:///root");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("///root"), "file:///root");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("////root"), "file:///root");
*/
/* Test cases related to escaping. */
/*
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///%3F"), "file:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///%78"), "file:///x");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///?"), "file:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:///x"), "file:///x");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("glorb:///%3F"), "glorb:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("glorb:///%78"), "glorb:///x");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("glorb:///?"), "glorb:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("glorb:///x"), "glorb:///x");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http:///%3F"), "http:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http:///%78"), "http:///x");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http:///?"), "http:///?");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http:///x"), "http:///x");

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.Eazel.Com"), "http://www.eazel.com");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.Eazel.Com/xXx"), "http://www.eazel.com/xXx");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("ftp://Darin@www.Eazel.Com/xXx"), "ftp://Darin@www.eazel.com/xXx");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.Eazel.Com:80/xXx"), "http://www.eazel.com:80/xXx");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("ftp://Darin@www.Eazel.Com:80/xXx"), "ftp://Darin@www.eazel.com:80/xXx");
*/
/* FIXME bugzilla.eazel.com 4101: Why append a slash in this case, but not in the http://www.eazel.com case? */
/*
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("http://www.eazel.com:80"), "http://www.eazel.com:80/");
*/
/* Note: these cases behave differently here than in
 * gnome-vfs. In some cases because of bugs in gnome-vfs, but
 * in other cases because we just want them handled
 * differently.
 */
/*
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:trash"), "file:trash");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("//trash"), "file:///trash");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("file:"), "file:");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("trash"), "file:///trash");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("glorp:"), "glorp:");
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("TRASH:XXX"), EEL_TRASH_URI);
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("trash:xxx"), EEL_TRASH_URI);
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("GNOME-TRASH:XXX"), EEL_TRASH_URI);
  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("gnome-trash:xxx"), EEL_TRASH_URI);

  EEL_CHECK_STRING_RESULT (eel_make_uri_canonical ("pipe:gnome-info2html2 as"), "pipe:gnome-info2html2 as");
	
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri ("file:/"), "file:///");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri ("file:/ "), "file:///%20");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri ("file:/%"), "file:///%25");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri ("file:///%3F"), "file:///%3F");
  EEL_CHECK_STRING_RESULT (eel_make_uri_from_half_baked_uri ("file:/%3F"), "file:///%253F");

  EEL_CHECK_STRING_RESULT (eel_uri_make_full_from_relative (NULL, NULL), NULL);
  EEL_CHECK_STRING_RESULT (eel_uri_make_full_from_relative ("http://a/b/c/d;p?q", NULL), "http://a/b/c/d;p?q");
  EEL_CHECK_STRING_RESULT (eel_uri_make_full_from_relative (NULL, "http://a/b/c/d;p?q"), "http://a/b/c/d;p?q");
*/
/* These test cases are from RFC 2396. */
/*
  #define TEST_PARTIAL(partial, result) \
  EEL_CHECK_STRING_RESULT (eel_uri_make_full_from_relative \
  ("http://a/b/c/d;p?q", partial), result)

  TEST_PARTIAL ("g", "http://a/b/c/g");
  TEST_PARTIAL ("./g", "http://a/b/c/g");
  TEST_PARTIAL ("g/", "http://a/b/c/g/");
  TEST_PARTIAL ("/g", "http://a/g");

  TEST_PARTIAL ("//g", "http://g");
	
  TEST_PARTIAL ("?y", "http://a/b/c/?y");
  TEST_PARTIAL ("g?y", "http://a/b/c/g?y");
  TEST_PARTIAL ("#s", "http://a/b/c/d;p#s");
  TEST_PARTIAL ("g#s", "http://a/b/c/g#s");
  TEST_PARTIAL ("g?y#s", "http://a/b/c/g?y#s");
  TEST_PARTIAL (";x", "http://a/b/c/;x");
  TEST_PARTIAL ("g;x", "http://a/b/c/g;x");
  TEST_PARTIAL ("g;x?y#s", "http://a/b/c/g;x?y#s");

  TEST_PARTIAL (".", "http://a/b/c/");
  TEST_PARTIAL ("./", "http://a/b/c/");

  TEST_PARTIAL ("..", "http://a/b/");
  TEST_PARTIAL ("../g", "http://a/b/g");
  TEST_PARTIAL ("../..", "http://a/");
  TEST_PARTIAL ("../../", "http://a/");
  TEST_PARTIAL ("../../g", "http://a/g");
*/
/* Others */
/*
  TEST_PARTIAL ("g/..", "http://a/b/c/");
  TEST_PARTIAL ("g/../", "http://a/b/c/");
  TEST_PARTIAL ("g/../g", "http://a/b/c/g");

  #undef TEST_PARTIAL

  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display (""), "/");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display (":"), ":");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///h/user"), "/h/user");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///%68/user/foo%2ehtml"), "/h/user/foo.html");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///h/user/foo.html#fragment"), "file:///h/user/foo.html#fragment");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("http://www.eazel.com"), "http://www.eazel.com");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("http://www.eazel.com/jobs#Engineering"), "http://www.eazel.com/jobs#Engineering");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file"), "/file");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///#"), "file:///#");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///"), "/");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///%20%23"), "/ #");
  EEL_CHECK_STRING_RESULT (eel_format_uri_for_display ("file:///%20%23#"), "file:///%20%23#");

  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match ("", ""), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match (":", ":"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match ("file:///h/user/file#gunzip:///", "file:///h/user/file#gunzip:///"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match ("file:///h/user/file#gunzip:///", "file:///h/user/file#gzip:///"), FALSE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match ("http://www.Eazel.Com", "http://www.eazel.com"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match ("http://www.Eazel.Com:80", "http://www.eazel.com:80"), TRUE);

  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("", ""), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments (":", ":"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/file#gunzip:///", "file:///h/user/file#gunzip:///"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/file#gunzip:///", "file:///h/user/file#gzip:///"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("http://www.Eazel.Com", "http://www.eazel.com"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("http://www.Eazel.Com:80", "http://www.eazel.com:80"), TRUE);

  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user", "file:///h/user"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user#frag", "file:///h/user"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user#frag", "file:///h/user/"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user#frag", "file:///h/user%23frag"), FALSE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/", "file:///h/user"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/", "http:///h/user"), FALSE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/", "http://www.eazel.com"), FALSE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/file#gunzip:///", "file:///h/user/file"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/file#gunzip:///", "file:///h/user/file"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/file.html.gz#gunzip:///#fragment", "file:///h/user/file.html.gz"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("file:///h/user/#frag", "file:///h/user/"), TRUE);
*/
/* Since it's illegal to have a # in a scheme name, it doesn't really matter what these cases do */
/*
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("fi#le:///h/user/file", "fi"), TRUE);
  EEL_CHECK_BOOLEAN_RESULT (eel_uris_match_ignore_fragments ("fi#le:///h/user/file", "fi#le:"), TRUE);

}

#endif *//* !EEL_OMIT_SELF_CHECK */
