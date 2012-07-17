/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2007, 2008 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include "glib-utils.h"
#include "file-utils.h"
#include "gio-utils.h"
#include "file-data.h"
#include "fr-archive.h"
#include "fr-command.h"
#include "fr-enum-types.h"
#include "fr-error.h"
#include "fr-marshal.h"
#include "fr-process.h"
#include "fr-init.h"

#if ENABLE_MAGIC
#include <magic.h>
#endif


#define FILES_ARRAY_INITIAL_SIZE 256


char *action_names[] = { "NONE",
			 "CREATING_NEW_ARCHIVE",
			 "LOADING_ARCHIVE",
			 "LISTING_CONTENT",
			 "DELETING_FILES",
			 "TESTING_ARCHIVE",
			 "GETTING_FILE_LIST",
			 "COPYING_FILES_FROM_REMOTE",
			 "ADDING_FILES",
			 "EXTRACTING_FILES",
			 "COPYING_FILES_TO_REMOTE",
			 "CREATING_ARCHIVE",
			 "SAVING_REMOTE_ARCHIVE" };


struct _FrArchivePrivate {
	/* propeties */

	GFile         *file;
	FrArchiveCaps  capabilities;

	/* internal */

	GCancellable  *cancellable;
	gboolean       creating_archive;
	char          *extraction_destination;
	gboolean       have_write_permissions;     /* true if we have the
						    * permissions to write the
						    * file. */
};


#define MAX_CHUNK_LEN (NCARGS * 2 / 3) /* Max command line length */
#define UNKNOWN_TYPE "application/octet-stream"
#define SAME_FS (FALSE)
#define NO_BACKUP_FILES (TRUE)
#define NO_DOT_FILES (FALSE)
#define IGNORE_CASE (FALSE)
#define LIST_LENGTH_TO_USE_FILE 10 /* FIXME: find a good value */
#define FILE_ARRAY_INITIAL_SIZE 256


enum {
	START,
	PROGRESS,
	MESSAGE,
	STOPPABLE,
	WORKING_ARCHIVE,
	LAST_SIGNAL
};


static guint fr_archive_signals[LAST_SIGNAL] = { 0 };


G_DEFINE_TYPE (FrArchive, fr_archive, G_TYPE_OBJECT)


/* Properties */
enum {
        PROP_0,
        PROP_FILE,
        PROP_MIME_TYPE,
        PROP_PASSWORD,
        PROP_ENCRYPT_HEADER,
        PROP_COMPRESSION,
        PROP_VOLUME_SIZE
};


static void
_fr_archive_set_file (FrArchive *self,
		      GFile     *file)
{
	if (self->priv->file != NULL) {
		g_object_unref (self->priv->file);
		self->priv->file = NULL;
	}
	if (file != NULL)
		self->priv->file = g_object_ref (file);

	self->mime_type = NULL;

	g_object_notify (G_OBJECT (self), "file");
}


static void
_fr_archive_set_uri (FrArchive  *self,
		     const char *uri)
{
	GFile *file;

	file = (uri != NULL) ? g_file_new_for_uri (uri) : NULL;
	_fr_archive_set_file (self, file);

	_g_object_unref (file);
}


static void
fr_archive_set_property (GObject      *object,
			 guint         property_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
        FrArchive *self;

        self = FR_ARCHIVE (object);

        switch (property_id) {
        case PROP_FILE:
        	_fr_archive_set_file (self, g_value_get_object (value));
        	break;
	case PROP_MIME_TYPE:
		fr_archive_set_mime_type (self, g_value_get_string (value));
		break;
	case PROP_PASSWORD:
		g_free (self->password);
		self->password = g_strdup (g_value_get_string (value));
		break;
	case PROP_ENCRYPT_HEADER:
		self->encrypt_header = g_value_get_boolean (value);
		break;
	case PROP_COMPRESSION:
		self->compression = g_value_get_enum (value);
		break;
	case PROP_VOLUME_SIZE:
		self->volume_size = g_value_get_uint (value);
		break;
        default:
                break;
        }
}


static void
fr_archive_get_property (GObject    *object,
			 guint       property_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
        FrArchive *self;

        self = FR_ARCHIVE (object);

        switch (property_id) {
        case PROP_FILE:
                g_value_set_object (value, self->priv->file);
                break;
	case PROP_MIME_TYPE:
		g_value_set_static_string (value, self->mime_type);
		break;
	case PROP_PASSWORD:
		g_value_set_string (value, self->password);
		break;
	case PROP_ENCRYPT_HEADER:
		g_value_set_boolean (value, self->encrypt_header);
		break;
	case PROP_COMPRESSION:
		g_value_set_enum (value, self->compression);
		break;
	case PROP_VOLUME_SIZE:
		g_value_set_uint (value, self->volume_size);
		break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}


static void
fr_archive_finalize (GObject *object)
{
	FrArchive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_ARCHIVE (object));

	archive = FR_ARCHIVE (object);

	_fr_archive_set_uri (archive, NULL);
	g_object_unref (archive->priv->cancellable);

	/* Chain up */

	if (G_OBJECT_CLASS (fr_archive_parent_class)->finalize)
		G_OBJECT_CLASS (fr_archive_parent_class)->finalize (object);
}


const char **void_mime_types = { NULL };


static const char **
fr_archive_base_get_mime_types (FrArchive *self)
{
	return void_mime_types;
}


static FrArchiveCap
fr_archive_base_get_capabilities (FrArchive  *self,
			          const char *mime_type,
			          gboolean    check_command)
{
	return FR_ARCHIVE_CAN_DO_NOTHING;
}


static void
fr_archive_base_set_mime_type (FrArchive  *self,
			       const char *mime_type)
{
	self->mime_type = _g_str_get_static (mime_type);
	fr_archive_update_capabilities (self);
}


static const char *
fr_archive_base_get_packages (FrArchive  *self,
			      const char *mime_type)
{
	return NULL;
}


static void
fr_archive_class_init (FrArchiveClass *klass)
{
	GObjectClass *gobject_class;

	fr_archive_parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (FrArchivePrivate));

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_archive_finalize;
	gobject_class->set_property = fr_archive_set_property;
	gobject_class->get_property = fr_archive_get_property;

	klass->start = NULL;
	klass->progress = NULL;
	klass->message = NULL;
	klass->stoppable = NULL;
	klass->working_archive = NULL;

	klass->get_mime_types = fr_archive_base_get_mime_types;
	klass->get_capabilities = fr_archive_base_get_capabilities;
	klass->set_mime_type = fr_archive_base_set_mime_type;
	klass->get_packages = fr_archive_base_get_packages;
	klass->load = NULL;
	klass->add_files = NULL;
	klass->extract_files = NULL;
	klass->remove_files = NULL;
	klass->test_integrity = NULL;
	klass->rename = NULL;
	klass->paste_clipboard = NULL;
	klass->add_dropped_items = NULL;
	klass->update_open_files = NULL;

	/* properties */

	g_object_class_install_property (gobject_class,
					 PROP_FILE,
					 g_param_spec_object ("file",
							      "File",
							      "The archive file",
							      G_TYPE_FILE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_MIME_TYPE,
					 g_param_spec_string ("mime-type",
							      "Content type",
							      "A mime-type that describes the content type",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_PASSWORD,
					 g_param_spec_string ("password",
							      "Password",
							      "The archive password",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_ENCRYPT_HEADER,
					 g_param_spec_boolean ("encrypt-header",
							       "Encrypt header",
							       "Whether to encrypt the archive header when creating the archive",
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_COMPRESSION,
					 g_param_spec_enum ("compression",
							    "Compression type",
							    "The compression type to use when creating the archive",
							    FR_TYPE_COMPRESSION,
							    FR_COMPRESSION_NORMAL,
							    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_VOLUME_SIZE,
					 g_param_spec_uint ("volume-size",
							    "Volume size",
							    "The size of each volume or 0 to not use volumes",
							    0L,
							    G_MAXUINT,
							    0,
							    G_PARAM_READWRITE));

	/* signals */

	fr_archive_signals[START] =
		g_signal_new ("start",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, start),
			      NULL, NULL,
			      fr_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1, G_TYPE_INT);
	fr_archive_signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, progress),
			      NULL, NULL,
			      fr_marshal_VOID__DOUBLE,
			      G_TYPE_NONE, 1,
			      G_TYPE_DOUBLE);
	fr_archive_signals[MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, message),
			      NULL, NULL,
			      fr_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	fr_archive_signals[STOPPABLE] =
		g_signal_new ("stoppable",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, stoppable),
			      NULL, NULL,
			      fr_marshal_VOID__BOOL,
			      G_TYPE_NONE,
			      1, G_TYPE_BOOLEAN);
	fr_archive_signals[WORKING_ARCHIVE] =
		g_signal_new ("working_archive",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FrArchiveClass, working_archive),
			      NULL, NULL,
			      fr_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
}


static void
fr_archive_init (FrArchive *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, FR_TYPE_ARCHIVE, FrArchivePrivate);

	self->mime_type = NULL;
	self->files = g_ptr_array_sized_new (FILES_ARRAY_INITIAL_SIZE);
	self->n_regular_files = 0;
        self->password = NULL;
        self->encrypt_header = FALSE;
        self->compression = FR_COMPRESSION_NORMAL;
        self->multi_volume = FALSE;
        self->volume_size = 0;
	self->read_only = FALSE;
	self->action = FR_ACTION_NONE;
        self->extract_here = FALSE;
        self->n_file = 0;
        self->n_files = 0;

        self->propAddCanUpdate = FALSE;
        self->propAddCanReplace = FALSE;
        self->propAddCanStoreFolders = FALSE;
        self->propExtractCanAvoidOverwrite = FALSE;
        self->propExtractCanSkipOlder = FALSE;
        self->propExtractCanJunkPaths = FALSE;
        self->propPassword = FALSE;
        self->propTest = FALSE;
        self->propCanExtractAll = TRUE;
        self->propCanDeleteNonEmptyFolders = TRUE;
        self->propCanExtractNonEmptyFolders = TRUE;
        self->propListFromFile = FALSE;

	self->priv->file = NULL;
	self->priv->cancellable = g_cancellable_new ();
	self->priv->creating_archive = FALSE;
	self->priv->extraction_destination = NULL;
	self->priv->have_write_permissions = FALSE;
}


GFile *
fr_archive_get_file (FrArchive *self)
{
	return self->priv->file;
}


gboolean
fr_archive_is_capable_of (FrArchive     *self,
			  FrArchiveCaps  requested_capabilities)
{
	return (((self->priv->capabilities ^ requested_capabilities) & requested_capabilities) == 0);
}


const char **
fr_archive_get_mime_types (FrArchive *self)
{
	return FR_ARCHIVE_GET_CLASS (G_OBJECT (self))->get_mime_types (self);
}


void
fr_archive_update_capabilities (FrArchive *self)
{
	self->priv->capabilities = fr_archive_get_capabilities (self, self->mime_type, TRUE);
	/* FIXME: check if this is correct */
	self->read_only = ! fr_archive_is_capable_of (self, FR_ARCHIVE_CAN_WRITE);
}


FrArchiveCap
fr_archive_get_capabilities (FrArchive  *self,
			     const char *mime_type,
			     gboolean    check_command)
{
	return FR_ARCHIVE_GET_CLASS (G_OBJECT (self))->get_capabilities (self, mime_type, check_command);
}


void
fr_archive_set_mime_type (FrArchive  *self,
			  const char *mime_type)
{
	FR_ARCHIVE_GET_CLASS (G_OBJECT (self))->set_mime_type (self, mime_type);
}


const char *
fr_archive_get_packages (FrArchive  *self,
			 const char *mime_type)
{
	return FR_ARCHIVE_GET_CLASS (G_OBJECT (self))->get_packages (self, mime_type);
}


void
fr_archive_set_stoppable (FrArchive *archive,
			  gboolean   stoppable)
{
	g_signal_emit (G_OBJECT (archive),
		       fr_archive_signals[STOPPABLE],
		       0,
		       stoppable);
}


/* -- fr_archive_new_for_creating -- */


static const char *
get_mime_type_from_filename (GFile *file)
{
	const char *mime_type = NULL;
	char       *uri;

	if (file == NULL)
		return NULL;

	uri = g_file_get_uri (file);
	mime_type = get_mime_type_from_extension (_g_filename_get_extension (uri));

	g_free (uri);

	return mime_type;
}


static FrArchive *
create_archive_for_mime_type (GType          archive_type,
			      GFile         *file,
			      const char    *mime_type,
			      FrArchiveCaps  requested_capabilities)
{
	FrArchive *archive;

	if (archive_type == 0)
		return NULL;

	archive = g_object_new (archive_type,
				"file", file,
				"mime-type", mime_type,
				NULL);

	if (! fr_archive_is_capable_of (archive, requested_capabilities)) {
		g_object_unref (archive);
		return NULL;
	}

	return archive;
}


FrArchive *
fr_archive_create (GFile *file)
{
	const char *mime_type;
	GType       archive_type;
	FrArchive  *archive;
	GFile      *parent;

	mime_type = get_mime_type_from_filename (file);
	archive_type = get_archive_type_from_mime_type (mime_type, FR_ARCHIVE_CAN_WRITE);

	archive = create_archive_for_mime_type (archive_type,
						file,
						mime_type,
						FR_ARCHIVE_CAN_WRITE);

	parent = g_file_get_parent (file);
	archive->priv->have_write_permissions = _g_file_check_permissions (parent, W_OK);
	archive->read_only = ! fr_archive_is_capable_of (archive, FR_ARCHIVE_CAN_WRITE) || ! archive->priv->have_write_permissions;

	g_object_unref (parent);

	return archive;
}


/* -- fr_archive_open -- */


#define BUFFER_SIZE_FOR_PRELOAD 32


typedef struct {
	GFile              *file;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
	char               *buffer;
        gsize               buffer_size;
	FrArchive          *archive;
} OpenData;


static void
load_data_free (OpenData *open_data)
{
	g_object_unref (open_data->file);
	_g_object_unref (open_data->cancellable);
	_g_object_unref (open_data->result);
	_g_object_unref (open_data->archive);
	g_free (open_data->buffer);
	g_free (open_data);
}


static void
load_data_complete_with_error (OpenData *load_data,
			       GError   *error)
{
	GSimpleAsyncResult *result;

	result = g_object_ref (load_data->result);
	g_simple_async_result_set_from_error (result, error);
	g_simple_async_result_complete_in_idle (result);

	g_object_unref (result);
}


static const char *
get_mime_type_from_magic_numbers (char  *buffer,
				  gsize  buffer_size)
{
	static const struct magic {
		const unsigned int off;
		const unsigned int len;
		const char * const id;
		const char * const mime_type;
	}
	magic_ids [] = {
		/* magic ids taken from magic/Magdir/archive from the file-4.21 tarball */
		{ 0,  6, "7z\274\257\047\034",                   "application/x-7z-compressed" },
		{ 7,  7, "**ACE**",                              "application/x-ace"           },
		{ 0,  2, "\x60\xea",                             "application/x-arj"           },
		{ 0,  3, "BZh",                                  "application/x-bzip2"         },
		{ 0,  2, "\037\213",                             "application/x-gzip"          },
		{ 0,  4, "LZIP",                                 "application/x-lzip"          },
		{ 0,  9, "\x89\x4c\x5a\x4f\x00\x0d\x0a\x1a\x0a", "application/x-lzop",         },
		{ 0,  4, "Rar!",                                 "application/x-rar"           },
		{ 0,  4, "RZIP",                                 "application/x-rzip"          },
		{ 0,  6, "\3757zXZ\000",                         "application/x-xz"            },
		{ 20, 4, "\xdc\xa7\xc4\xfd",                     "application/x-zoo",          },
		{ 0,  4, "PK\003\004",                           "application/zip"             },
		{ 0,  8, "PK00PK\003\004",                       "application/zip"             },
		{ 0,  4, "LRZI",                                 "application/x-lrzip"         },
	};

	int  i;

	for (i = 0; i < G_N_ELEMENTS (magic_ids); i++) {
		const struct magic * const magic = &magic_ids[i];

		if ((magic->off + magic->len) > buffer_size)
			g_warning ("buffer underrun for mime-type '%s' magic", magic->mime_type);
		else if (! memcmp (buffer + magic->off, magic->id, magic->len))
			return magic->mime_type;
	}

	return NULL;
}


static FrArchive *
create_archive_to_load_archive (GFile      *file,
			        const char *mime_type)
{
	FrArchiveCaps requested_capabilities = FR_ARCHIVE_CAN_DO_NOTHING;
	GType         archive_type;

	if (mime_type == NULL)
		return FALSE;

	/* try with the WRITE capability even when loading, this way we give
	 * priority to the commands that can read and write over commands
	 * that can only read a specific file format. */

	requested_capabilities |= FR_ARCHIVE_CAN_READ_WRITE;
	archive_type = get_archive_type_from_mime_type (mime_type, requested_capabilities);

	/* if no command was found, remove the write capability and try again */

	if (archive_type == 0) {
		requested_capabilities ^= FR_ARCHIVE_CAN_WRITE;
		archive_type = get_archive_type_from_mime_type (mime_type, requested_capabilities);
	}

	return create_archive_for_mime_type (archive_type,
					     file,
					     mime_type,
					     requested_capabilities);
}


static void
open_archive_stream_ready_cb (GObject      *source_object,
			      GAsyncResult *result,
			      gpointer      user_data)
{
	OpenData         *open_data = user_data;
	GFileInputStream *istream = G_FILE_INPUT_STREAM (source_object);
	GError           *error = NULL;
	gssize            bytes_read;
	const char       *mime_type;
	gboolean          result_uncertain;
	FrArchive        *archive;
	char             *local_mime_type, *uri;

	bytes_read = g_input_stream_read_finish (G_INPUT_STREAM (istream), result, &error);
	if (bytes_read == -1) {
		g_object_unref (istream);
		load_data_complete_with_error (open_data, error);
		return;
	}

	open_data->buffer_size = bytes_read;
	g_object_unref (istream);

	archive = NULL;
	uri = g_file_get_uri (open_data->file);
	local_mime_type = g_content_type_guess (uri, (guchar *) open_data->buffer, open_data->buffer_size, &result_uncertain);
	if (! result_uncertain) {
		mime_type = _g_str_get_static (local_mime_type);
		archive = create_archive_to_load_archive (open_data->file, mime_type);
	}
	if (archive == NULL) {
		mime_type = get_mime_type_from_magic_numbers (open_data->buffer, open_data->buffer_size);
		archive = create_archive_to_load_archive (open_data->file, mime_type);
		if (archive == NULL) {
			mime_type = get_mime_type_from_filename (open_data->file);
			archive = create_archive_to_load_archive (open_data->file, mime_type);
			if (archive == NULL) {
				error = g_error_new (FR_ERROR,
						     FR_ERROR_UNSUPPORTED_FORMAT,
						     "%s",
						     _("Archive type not supported."));
				load_data_complete_with_error (open_data, error);
				return;
			}
		}
	}

	archive->priv->have_write_permissions = _g_file_check_permissions (fr_archive_get_file (archive), W_OK);
	archive->read_only = ! fr_archive_is_capable_of (archive, FR_ARCHIVE_CAN_WRITE) || ! archive->priv->have_write_permissions;
	open_data->archive = archive;

        g_simple_async_result_complete_in_idle (open_data->result);

        g_free (local_mime_type);
        g_free (uri);
}


static void
open_archive_read_ready_cb (GObject      *source_object,
			    GAsyncResult *result,
			    gpointer      user_data)
{
	OpenData         *open_data = user_data;
	GFileInputStream *istream;
	GError           *error = NULL;

	istream = g_file_read_finish (G_FILE (source_object), result, &error);
	if (istream == NULL) {
		load_data_complete_with_error (open_data, error);
		return;
	}

	g_input_stream_read_async (G_INPUT_STREAM (istream),
				   open_data->buffer,
				   open_data->buffer_size,
				   G_PRIORITY_DEFAULT,
				   open_data->cancellable,
				   open_archive_stream_ready_cb,
				   open_data);
}


void
fr_archive_open (GFile               *file,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	OpenData *open_data;

	g_return_if_fail (file != NULL);

	open_data = g_new0 (OpenData, 1);
	open_data->file = g_object_ref (file);
	open_data->cancellable = _g_object_ref (cancellable);
	open_data->result = g_simple_async_result_new (G_OBJECT (file),
						       callback,
						       user_data,
						       fr_archive_open);
	open_data->buffer_size = BUFFER_SIZE_FOR_PRELOAD;
	open_data->buffer = g_new (char, open_data->buffer_size);
        g_simple_async_result_set_op_res_gpointer (open_data->result,
        					   open_data,
                                                   (GDestroyNotify) load_data_free);

        /* FIXME: libarchive
         fr_archive_action_started (archive, FR_ACTION_LOADING_ARCHIVE); */

        /* load a few bytes to guess the archive type */

	g_file_read_async (open_data->file,
			   G_PRIORITY_DEFAULT,
			   open_data->cancellable,
			   open_archive_read_ready_cb,
			   open_data);
}


FrArchive *
fr_archive_open_finish (GFile         *file,
			GAsyncResult  *result,
			GError       **error)
{
	GSimpleAsyncResult *simple;
	OpenData           *open_data;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (file), fr_archive_open), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	open_data = g_simple_async_result_get_op_res_gpointer (simple);
	return _g_object_ref (open_data->archive);
}


void
fr_archive_load (FrArchive           *archive,
		 const char          *password,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	g_return_if_fail (archive != NULL);

	if (archive->files != NULL) {
		_g_ptr_array_free_full (archive->files, (GFunc) file_data_free, NULL);
		archive->files = g_ptr_array_sized_new (FILE_ARRAY_INITIAL_SIZE);
	}

	fr_archive_action_started (archive, FR_ACTION_LISTING_CONTENT);
	FR_ARCHIVE_GET_CLASS (archive)->load (archive, password, cancellable, callback, user_data);
}


gboolean
fr_archive_operation_finish (FrArchive     *archive,
			     GAsyncResult  *result,
			     GError       **error)
{
	return ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}


void
fr_archive_add_files (FrArchive           *archive,
		      GList               *file_list,
		      const char          *base_dir,
		      const char          *dest_dir,
		      gboolean             update,
		      gboolean             recursive,
		      const char          *password,
		      gboolean             encrypt_header,
		      FrCompression        compression,
		      guint                volume_size,
		      GCancellable        *cancellable,
		      GAsyncReadyCallback  callback,
		      gpointer             user_data)
{
	g_return_if_fail (! archive->read_only);

	fr_archive_action_started (archive, FR_ACTION_ADDING_FILES);

	FR_ARCHIVE_GET_CLASS (archive)->add_files (archive,
						   file_list,
						   base_dir,
						   dest_dir,
						   update,
						   recursive,
						   password,
						   encrypt_header,
						   compression,
						   volume_size,
						   cancellable,
						   callback,
						   user_data);
}


/* -- add with wildcard -- */


typedef struct {
	FrArchive           *archive;
	char                *source_dir;
	char                *dest_dir;
	gboolean             update;
	char                *password;
	gboolean             encrypt_header;
	FrCompression        compression;
	guint                volume_size;
	GCancellable        *cancellable;
	GAsyncReadyCallback  callback;
	gpointer             user_data;
} AddWithWildcardData;


static void
add_with_wildcard_data_free (AddWithWildcardData *aww_data)
{
	g_free (aww_data->source_dir);
	g_free (aww_data->dest_dir);
	g_free (aww_data->password);
	_g_object_unref (aww_data->cancellable);
	g_free (aww_data);
}


static void
add_with_wildcard__step2 (GList    *file_list,
			  GList    *dirs_list,
			  GError   *error,
			  gpointer  data)
{
	AddWithWildcardData *aww_data = data;
	FrArchive           *archive = aww_data->archive;
	GSimpleAsyncResult  *result;

	result = g_simple_async_result_new (G_OBJECT (archive),
					    aww_data->callback,
					    aww_data->user_data,
					    fr_archive_add_with_wildcard);

	if (error != NULL) {
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);
	}
	else {
		if (file_list == NULL)
			g_simple_async_result_complete_in_idle (result);
		else
			fr_archive_add_files (aww_data->archive,
					      file_list,
					      aww_data->source_dir,
					      aww_data->dest_dir,
					      aww_data->update,
					      FALSE,
					      aww_data->password,
					      aww_data->encrypt_header,
					      aww_data->compression,
					      aww_data->volume_size,
					      aww_data->cancellable,
					      aww_data->callback,
					      aww_data->user_data);
	}

	g_object_unref (result);
	_g_string_list_free (file_list);
	_g_string_list_free (dirs_list);
	add_with_wildcard_data_free (aww_data);
}


void
fr_archive_add_with_wildcard (FrArchive           *archive,
			      const char          *include_files,
			      const char          *exclude_files,
			      const char          *exclude_folders,
			      const char          *source_dir,
			      const char          *dest_dir,
			      gboolean             update,
			      gboolean             follow_links,
			      const char          *password,
			      gboolean             encrypt_header,
			      FrCompression        compression,
			      guint                volume_size,
			      GCancellable        *cancellable,
			      GAsyncReadyCallback  callback,
			      gpointer             user_data)
{
	AddWithWildcardData *aww_data;

	g_return_if_fail (! archive->read_only);

	aww_data = g_new0 (AddWithWildcardData, 1);
	aww_data->archive = archive;
	aww_data->source_dir = g_strdup (source_dir);
	aww_data->dest_dir = g_strdup (dest_dir);
	aww_data->update = update;
	aww_data->password = g_strdup (password);
	aww_data->encrypt_header = encrypt_header;
	aww_data->compression = compression;
	aww_data->volume_size = volume_size;
	aww_data->cancellable = _g_object_ref (cancellable);
	aww_data->callback = callback;
	aww_data->user_data = user_data;

	fr_archive_action_started (archive, FR_ACTION_GETTING_FILE_LIST);

	g_directory_list_async (source_dir,
				source_dir,
				TRUE,
				follow_links,
				NO_BACKUP_FILES,
				NO_DOT_FILES,
				include_files,
				exclude_files,
				exclude_folders,
				IGNORE_CASE,
				aww_data->cancellable,
				add_with_wildcard__step2,
				aww_data);
}


/* -- fr_archive_add_directory -- */


typedef struct {
	FrArchive           *archive;
	char                *base_dir;
	char                *dest_dir;
	gboolean             update;
	char                *password;
	gboolean             encrypt_header;
	FrCompression        compression;
	guint                volume_size;
	GCancellable        *cancellable;
	GAsyncReadyCallback  callback;
	gpointer             user_data;
} AddDirectoryData;


static void
add_directory_data_free (AddDirectoryData *ad_data)
{
	g_free (ad_data->base_dir);
	g_free (ad_data->dest_dir);
	g_free (ad_data->password);
	_g_object_unref (ad_data->cancellable);
	g_free (ad_data);
}


static void
add_directory__step2 (GList    *file_list,
		      GList    *dir_list,
		      GError   *error,
		      gpointer  user_data)
{
	AddDirectoryData   *ad_data = user_data;
	FrArchive          *archive = ad_data->archive;
	GSimpleAsyncResult *result;

	result = g_simple_async_result_new (G_OBJECT (archive),
					    ad_data->callback,
					    ad_data->user_data,
					    fr_archive_add_directory);

	if (error != NULL) {
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);
	}
	else {
		if (archive->propAddCanStoreFolders)
			file_list = g_list_concat (file_list, dir_list);
		else
			_g_string_list_free (dir_list);

		if (file_list == NULL)
			g_simple_async_result_complete_in_idle (result);
		else
			fr_archive_add_files (ad_data->archive,
					      file_list,
					      ad_data->base_dir,
					      ad_data->dest_dir,
					      ad_data->update,
					      FALSE,
					      ad_data->password,
					      ad_data->encrypt_header,
					      ad_data->compression,
					      ad_data->volume_size,
					      ad_data->cancellable,
					      ad_data->callback,
					      ad_data->user_data);
	}

	g_object_unref (result);
	_g_string_list_free (file_list);
	_g_string_list_free (dir_list);
	add_directory_data_free (ad_data);
}


void
fr_archive_add_directory (FrArchive           *archive,
			  const char          *directory,
			  const char          *base_dir,
			  const char          *dest_dir,
			  gboolean             update,
			  const char          *password,
			  gboolean             encrypt_header,
			  FrCompression        compression,
			  guint                volume_size,
			  GCancellable        *cancellable,
			  GAsyncReadyCallback  callback,
			  gpointer             user_data)

{
	AddDirectoryData *ad_data;

	g_return_if_fail (! archive->read_only);

	ad_data = g_new0 (AddDirectoryData, 1);
	ad_data->archive = archive;
	ad_data->base_dir = g_strdup (base_dir);
	ad_data->dest_dir = g_strdup (dest_dir);
	ad_data->update = update;
	ad_data->password = g_strdup (password);
	ad_data->encrypt_header = encrypt_header;
	ad_data->compression = compression;
	ad_data->volume_size = volume_size;
	ad_data->cancellable = _g_object_ref (cancellable);
	ad_data->callback = callback;
	ad_data->user_data = user_data;

	fr_archive_action_started (archive, FR_ACTION_GETTING_FILE_LIST);

	g_directory_list_all_async (directory,
				    base_dir,
				    TRUE,
				    cancellable,
				    add_directory__step2,
				    ad_data);
}


void
fr_archive_add_items (FrArchive           *archive,
		      GList               *item_list,
		      const char          *base_dir,
		      const char          *dest_dir,
		      gboolean             update,
		      const char          *password,
		      gboolean             encrypt_header,
		      FrCompression        compression,
		      guint                volume_size,
		      GCancellable        *cancellable,
		      GAsyncReadyCallback  callback,
		      gpointer             user_data)

{
	AddDirectoryData *ad_data;

	g_return_if_fail (! archive->read_only);

	ad_data = g_new0 (AddDirectoryData, 1);
	ad_data->archive = archive;
	ad_data->base_dir = g_strdup (base_dir);
	ad_data->dest_dir = g_strdup (dest_dir);
	ad_data->update = update;
	ad_data->password = g_strdup (password);
	ad_data->encrypt_header = encrypt_header;
	ad_data->compression = compression;
	ad_data->volume_size = volume_size;
	ad_data->cancellable = _g_object_ref (cancellable);
	ad_data->callback = callback;
	ad_data->user_data = user_data;

	fr_archive_action_started (archive, FR_ACTION_GETTING_FILE_LIST);

	g_list_items_async (item_list,
			    base_dir,
			    cancellable,
			    add_directory__step2,
			    ad_data);
}


void
fr_archive_remove (FrArchive           *archive,
		   GList               *file_list,
		   FrCompression        compression,
		   GCancellable        *cancellable,
		   GAsyncReadyCallback  callback,
		   gpointer             user_data)
{
	g_return_if_fail (! archive->read_only);

	FR_ARCHIVE_GET_CLASS (archive)->remove_files (archive,
						      file_list,
						      compression,
						      cancellable,
						      callback,
						      user_data);
}


void
fr_archive_extract (FrArchive           *self,
		    GList               *file_list,
		    const char          *destination,
		    const char          *base_dir,
		    gboolean             skip_older,
		    gboolean             overwrite,
		    gboolean             junk_paths,
		    const char          *password,
		    GCancellable        *cancellable,
		    GAsyncReadyCallback  callback,
		    gpointer             user_data)
{
	g_free (self->priv->extraction_destination);
	self->priv->extraction_destination = g_strdup (destination);

	FR_ARCHIVE_GET_CLASS (self)->extract_files (self,
						    file_list,
						    destination,
						    base_dir,
						    skip_older,
						    overwrite,
						    junk_paths,
						    password,
						    cancellable,
						    callback,
						    user_data);
}


void
fr_archive_extract_to_local (FrArchive           *self,
			     GList               *file_list,
			     const char          *destination_path,
			     const char          *base_dir,
			     gboolean             skip_older,
			     gboolean             overwrite,
			     gboolean             junk_paths,
			     const char          *password,
			     GCancellable        *cancellable,
			     GAsyncReadyCallback  callback,
			     gpointer             user_data)
{
	char *destination_uri;

	destination_uri = g_filename_to_uri (destination_path, NULL, NULL);
	if (destination_uri == NULL)
		return;

	fr_archive_extract (self,
			    file_list,
			    destination_uri,
			    base_dir,
			    skip_older,
			    overwrite,
			    junk_paths,
			    password,
			    cancellable,
			    callback,
			    user_data);

	g_free (destination_uri);
}


/* -- fr_archive_extract_here -- */


static char *
get_desired_destination_for_archive (GFile *file)
{
	GFile      *directory;
	char       *directory_uri;
	char       *name;
	const char *ext;
	char       *new_name;
	char       *new_name_escaped;
	char       *desired_destination = NULL;

	directory = g_file_get_parent (file);
	directory_uri = g_file_get_uri (directory);

	name = g_file_get_basename (file);
	ext = get_archive_filename_extension (name);
	if (ext == NULL)
		/* if no extension is present add a suffix to the name... */
		new_name = g_strconcat (name, "_FILES", NULL);
	else
		/* ...else use the name without the extension */
		new_name = g_strndup (name, strlen (name) - strlen (ext));
	new_name_escaped = g_uri_escape_string (new_name, "", FALSE);

	desired_destination = g_strconcat (directory_uri, "/", new_name_escaped, NULL);

	g_free (new_name_escaped);
	g_free (new_name);
	g_free (name);
	g_free (directory_uri);
	g_object_unref (directory);

	return desired_destination;
}


static char *
get_extract_here_destination (GFile   *file,
			      GError **error)
{
	char  *desired_destination;
	char  *destination = NULL;
	int    n = 1;
	GFile *directory;

	desired_destination = get_desired_destination_for_archive (file);
	do {
		*error = NULL;

		g_free (destination);
		if (n == 1)
			destination = g_strdup (desired_destination);
		else
			destination = g_strdup_printf ("%s%%20(%d)", desired_destination, n);

		directory = g_file_new_for_uri (destination);
		g_file_make_directory (directory, NULL, error);
		g_object_unref (directory);

		n++;
	}
	while (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS));

	g_free (desired_destination);

	if (*error != NULL) {
		g_warning ("could not create destination folder: %s\n", (*error)->message);
		g_free (destination);
		destination = NULL;
	}

	return destination;
}


gboolean
fr_archive_extract_here (FrArchive           *archive,
			 gboolean             skip_older,
			 gboolean             overwrite,
			 gboolean             junk_path,
			 const char          *password,
			 GCancellable        *cancellable,
			 GAsyncReadyCallback  callback,
			 gpointer             user_data)
{
	char   *destination;
	GError *error = NULL;

	destination = get_extract_here_destination (archive->priv->file, &error);
	if (error != NULL) {
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (archive),
						    callback,
						    user_data,
						    fr_archive_extract_here);
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_error_free (error);

		return FALSE;
	}

	archive->extract_here = TRUE;
	fr_archive_extract (archive,
			    NULL,
			    destination,
			    NULL,
			    skip_older,
			    overwrite,
			    junk_path,
			    password,
			    cancellable,
			    callback,
			    user_data);

	g_free (destination);

	return TRUE;
}


void
fr_archive_set_last_extraction_destination (FrArchive  *archive,
					    const char *uri)
{
	if (uri == archive->priv->extraction_destination)
		return;

	g_free (archive->priv->extraction_destination);
	archive->priv->extraction_destination = NULL;
	if (uri != NULL)
		archive->priv->extraction_destination = g_strdup (uri);
}


const char *
fr_archive_get_last_extraction_destination (FrArchive *archive)
{
	return archive->priv->extraction_destination;
}


void
fr_archive_test (FrArchive           *archive,
		 const char          *password,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	FR_ARCHIVE_GET_CLASS (archive)->test_integrity (archive,
							password,
							cancellable,
							callback,
							user_data);
}


void
fr_archive_rename (FrArchive           *archive,
		   GList               *file_list,
		   const char          *old_name,
		   const char          *new_name,
		   const char          *current_dir,
		   gboolean             is_dir,
		   gboolean             dir_in_archive,
		   const char          *original_path,
		   GCancellable        *cancellable,
		   GAsyncReadyCallback  callback,
		   gpointer             user_data)
{
	FR_ARCHIVE_GET_CLASS (archive)->rename (archive,
						file_list,
						old_name,
						new_name,
						current_dir,
						is_dir,
						dir_in_archive,
						original_path,
						cancellable,
						callback,
						user_data);
}


void
fr_archive_paste_clipboard (FrArchive           *archive,
			    char                *archive_uri,
			    char                *password,
			    gboolean             encrypt_header,
			    FrCompression        compression,
			    guint                volume_size,
			    FrClipboardOp        op,
			    char                *base_dir,
			    GList               *files,
			    char                *tmp_dir,
			    char                *current_dir,
			    GCancellable        *cancellable,
			    GAsyncReadyCallback  callback,
			    gpointer             user_data)
{
	FR_ARCHIVE_GET_CLASS (archive)->paste_clipboard (archive,
							 archive_uri,
							 password,
							 encrypt_header,
							 compression,
							 volume_size,
							 op,
							 base_dir,
							 files,
							 tmp_dir,
							 current_dir,
							 cancellable,
							 callback,
							 user_data);
}


void
fr_archive_add_dropped_items (FrArchive           *archive,
			      GList               *item_list,
			      const char          *base_dir,
			      const char          *dest_dir,
			      gboolean             update,
			      const char          *password,
			      gboolean             encrypt_header,
			      FrCompression        compression,
			      guint                volume_size,
			      GCancellable        *cancellable,
			      GAsyncReadyCallback  callback,
			      gpointer             user_data)
{
	GSimpleAsyncResult *result;
	GList              *scan;
	char               *archive_uri;

	result = g_simple_async_result_new (G_OBJECT (archive),
					    callback,
					    user_data,
					    fr_archive_add_dropped_items);

	if (archive->read_only) {
		GError *error;

		error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, ! archive->priv->have_write_permissions ? _("You don't have the right permissions.") : _("This archive type cannot be modified"));
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_error_free (error);
		return;
	}

	/* FIXME: make this check for all the add actions */
	archive_uri = g_file_get_uri (archive->priv->file);
	for (scan = item_list; scan; scan = scan->next) {
		if (_g_uri_cmp (scan->data, archive_uri) == 0) {
			GError *error;

			error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, _("You can't add an archive to itself."));
			g_simple_async_result_set_from_error (result, error);
			g_simple_async_result_complete_in_idle (result);

			g_error_free (error);
			g_free (archive_uri);
			return;
		}
	}
	g_free (archive_uri);

	FR_ARCHIVE_GET_CLASS (archive)->add_dropped_items (archive,
							   item_list,
							   base_dir,
							   dest_dir,
							   update,
							   password,
							   encrypt_header,
							   compression,
							   volume_size,
							   cancellable,
							   callback,
							   user_data);
}


void
fr_archive_update_open_files (FrArchive           *archive,
			      GList               *file_list,
			      GList               *dir_list,
			      const char          *password,
			      gboolean             encrypt_header,
			      FrCompression        compression,
			      guint                volume_size,
			      GCancellable        *cancellable,
			      GAsyncReadyCallback  callback,
			      gpointer             user_data)
{
	FR_ARCHIVE_GET_CLASS (archive)->update_open_files (archive,
							   file_list,
							   dir_list,
							   password,
							   encrypt_header,
							   compression,
							   volume_size,
							   cancellable,
							   callback,
							   user_data);
}


void
fr_archive_set_multi_volume (FrArchive *self,
			     GFile     *file)
{
	self->multi_volume = TRUE;
	_fr_archive_set_file (self, file);
}


void
fr_archive_change_name (FrArchive  *archive,
		        const char *filename)
{
	const char *name;
	GFile      *parent;
	GFile      *file;

	name = _g_path_get_file_name (filename);

	parent = g_file_get_parent (archive->priv->file);
	file = g_file_get_child (parent, name);;
	_fr_archive_set_file (archive, file);

	g_object_unref (parent);
	g_object_unref (file);
}


GCancellable *
fr_archive_get_cancellable (FrArchive *self)
{
	return self->priv->cancellable;
}


void
fr_archive_action_started (FrArchive *self,
			   FrAction   action)
{
	g_signal_emit (self,
		       fr_archive_signals[START],
		       0,
		       action);
}


/* fraction == -1 means : I don't known how much time the current operation
 *                        will take, the dialog will display this info pulsing
 *                        the progress bar.
 * fraction in [0.0, 1.0] means the amount of work, in percentage,
 *                        accomplished.
 */
void
fr_archive_progress (FrArchive *self,
		     double     fraction)
{
	g_signal_emit (self,
		       fr_archive_signals[PROGRESS],
		       0,
		       fraction);
}


void
fr_archive_message (FrArchive  *self,
		    const char *msg)
{
	g_signal_emit (self,
		       fr_archive_signals[MESSAGE],
		       0,
		       msg);
}


void
fr_archive_working_archive (FrArchive  *self,
		            const char *archive_name)
{
	g_signal_emit (self,
		       fr_archive_signals[WORKING_ARCHIVE],
		       0,
		       archive_name);
}


void
fr_archive_set_n_files (FrArchive *self,
			int        n_files)
{
	self->n_files = n_files;
	self->n_file = 0;
}


void
fr_archive_add_file (FrArchive *self,
		     FileData  *file_data)
{
	file_data_update_content_type (file_data);
	g_ptr_array_add (self->files, file_data);
	if (! file_data->dir)
		self->n_regular_files++;
}


/* FIXME: use sniffer as in fr_archive_open */
gboolean
_g_uri_is_archive (const char *uri)
{
	GFile      *file;
	const char *mime_type = NULL;
	gboolean    is_archive = FALSE;

	file = g_file_new_for_uri (uri);
	/* FIXME: libarchive
	mime_type = get_mime_type_from_magic_numbers (file); */
	if (mime_type == NULL)
		mime_type = get_mime_type_from_filename (file);

	if (mime_type != NULL) {
		int i;

		for (i = 0; mime_type_desc[i].mime_type != NULL; i++) {
			if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0) {
				is_archive = TRUE;
				break;
			}
		}
	}
	g_object_unref (file);

	return is_archive;
}
