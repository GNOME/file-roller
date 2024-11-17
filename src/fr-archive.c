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
#include "fr-file-data.h"
#include "fr-archive.h"
#include "fr-command.h"
#include "fr-enum-types.h"
#include "fr-error.h"
#include "fr-marshal.h"
#include "fr-process.h"
#include "fr-init.h"

#define FILE_ARRAY_INITIAL_SIZE	256
#define PROGRESS_DELAY          50
#define BYTES_FRACTION(self)    ((double) ((FrArchivePrivate*) fr_archive_get_instance_private (self))->completed_bytes / ((FrArchivePrivate*) fr_archive_get_instance_private (self))->total_bytes)
#define FILES_FRACTION(self)    ((double) ((FrArchivePrivate*) fr_archive_get_instance_private (self))->completed_files + 0.5) / (((FrArchivePrivate*) fr_archive_get_instance_private (self))->total_files + 1)


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
			 "SAVING_REMOTE_ARCHIVE",
			 "RENAMING_FILES",
			 "PASTING_FILES",
			 "UPDATING_FILES" };


typedef struct _DroppedItemsData DroppedItemsData;


typedef struct {
	/* propeties */

	GFile         *file;
	FrArchiveCaps  capabilities;

	/* progress data */

	int            total_files;
	int            completed_files;
	gsize          total_bytes;
	gsize          completed_bytes;
	GMutex         progress_mutex;
	gulong         progress_event;

	/* others */

	gboolean       creating_archive;
	GFile         *extraction_destination;
	gboolean       have_write_permissions;     /* true if we have the
						    * permissions to write the
						    * file. */
	DroppedItemsData *dropped_items_data;
} FrArchivePrivate;


G_DEFINE_TYPE_WITH_PRIVATE (FrArchive, fr_archive, G_TYPE_OBJECT)


enum {
	START,
	PROGRESS,
	MESSAGE,
	STOPPABLE,
	WORKING_ARCHIVE,
	LAST_SIGNAL
};


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


static guint fr_archive_signals[LAST_SIGNAL] = { 0 };


static void
_fr_archive_set_file (FrArchive *self,
		      GFile     *file,
		      gboolean   reset_mime_type)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	if (private->file != NULL) {
		g_object_unref (private->file);
		private->file = NULL;
	}
	if (file != NULL)
		private->file = g_object_ref (file);

	if (reset_mime_type)
		self->mime_type = NULL;

	g_object_notify (G_OBJECT (self), "file");
}


static void
_fr_archive_set_uri (FrArchive  *self,
		     const char *uri)
{
	GFile *file;

	file = (uri != NULL) ? g_file_new_for_uri (uri) : NULL;
	_fr_archive_set_file (self, file, TRUE);

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
        	_fr_archive_set_file (self, g_value_get_object (value), TRUE);
        	break;
	case PROP_MIME_TYPE:
		fr_archive_set_mime_type (self, g_value_get_string (value));
		break;
	case PROP_PASSWORD:
		fr_archive_set_password (self, g_value_get_string (value));
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
	FrArchivePrivate *private = fr_archive_get_instance_private (self);

        switch (property_id) {
        case PROP_FILE:
		g_value_set_object (value, private->file);
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


static void dropped_items_data_free (DroppedItemsData *data);


static void
fr_archive_finalize (GObject *object)
{
	FrArchive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_ARCHIVE (object));

	archive = FR_ARCHIVE (object);
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);

	_fr_archive_set_uri (archive, NULL);
	if (private->progress_event != 0) {
		g_source_remove (private->progress_event);
		private->progress_event = 0;
	}
	g_mutex_clear (&private->progress_mutex);
	g_hash_table_unref (archive->files_hash);
	g_ptr_array_unref (archive->files);
	if (private->dropped_items_data != NULL) {
		dropped_items_data_free (private->dropped_items_data);
		private->dropped_items_data = NULL;
	}

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


static FrArchiveCaps
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

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_archive_finalize;
	gobject_class->set_property = fr_archive_set_property;
	gobject_class->get_property = fr_archive_get_property;

	klass->progress = NULL;
	klass->message = NULL;
	klass->stoppable = NULL;
	klass->working_archive = NULL;

	klass->get_mime_types = fr_archive_base_get_mime_types;
	klass->get_capabilities = fr_archive_base_get_capabilities;
	klass->set_mime_type = fr_archive_base_set_mime_type;
	klass->get_packages = fr_archive_base_get_packages;
	klass->open = NULL;
	klass->list = NULL;
	klass->add_files = NULL;
	klass->extract_files = NULL;
	klass->remove_files = NULL;
	klass->test_integrity = NULL;
	klass->rename = NULL;
	klass->paste_clipboard = NULL;
	klass->add_dropped_files = NULL;
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
			      fr_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1, G_TYPE_BOOLEAN);
	fr_archive_signals[WORKING_ARCHIVE] =
		g_signal_new ("working-archive",
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
	FrArchivePrivate *private = fr_archive_get_instance_private (self);

	self->mime_type = NULL;
	self->files = g_ptr_array_new_full (FILE_ARRAY_INITIAL_SIZE, (GDestroyNotify) fr_file_data_free);
	self->files_hash = g_hash_table_new (g_str_hash, g_str_equal);
	self->n_regular_files = 0;
        self->password = NULL;
        self->encrypt_header = FALSE;
        self->compression = FR_COMPRESSION_NORMAL;
        self->multi_volume = FALSE;
        self->volume_size = 0;
	self->read_only = FALSE;

        self->propAddCanUpdate = FALSE;
        self->propAddCanReplace = FALSE;
        self->propAddCanStoreFolders = FALSE;
        self->propAddCanStoreLinks = FALSE;
        self->propAddCanFollowDirectoryLinksWithoutDereferencing = TRUE;
        self->propExtractCanAvoidOverwrite = FALSE;
        self->propExtractCanSkipOlder = FALSE;
        self->propExtractCanJunkPaths = FALSE;
        self->propPassword = FALSE;
        self->propTest = FALSE;
        self->propCanExtractAll = TRUE;
        self->propCanDeleteNonEmptyFolders = TRUE;
        self->propCanDeleteAllFiles = TRUE;
        self->propCanExtractNonEmptyFolders = TRUE;
        self->propListFromFile = FALSE;

	private->file = NULL;
	private->creating_archive = FALSE;
	private->extraction_destination = NULL;
	private->have_write_permissions = FALSE;
	private->completed_files = 0;
	private->total_files = 0;
	private->completed_bytes = 0;
	private->total_bytes = 0;
	private->dropped_items_data = NULL;
	g_mutex_init (&private->progress_mutex);
}


GFile *
fr_archive_get_file (FrArchive *self)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	return private->file;
}


gboolean
fr_archive_is_capable_of (FrArchive     *self,
			  FrArchiveCaps  requested_capabilities)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	return (((private->capabilities ^ requested_capabilities) & requested_capabilities) == 0);
}


const char **
fr_archive_get_supported_types (FrArchive *self)
{
	return FR_ARCHIVE_GET_CLASS (G_OBJECT (self))->get_mime_types (self);
}


void
fr_archive_update_capabilities (FrArchive *self)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	private->capabilities = fr_archive_get_capabilities (self, self->mime_type, TRUE);
	self->read_only = ! fr_archive_is_capable_of (self, FR_ARCHIVE_CAN_WRITE);
}


FrArchiveCaps
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
fr_archive_get_mime_type (FrArchive  *self)
{
	return self->mime_type;
}


void
fr_archive_set_password (FrArchive  *self,
			 const char *new_password)
{
	if (self->password == new_password)
		return;
	g_free (self->password);
	self->password = g_strdup (new_password);
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
fr_archive_create (GFile      *file,
		   const char *mime_type)
{
	FrArchiveCaps  requested_capabilities;
	GType          archive_type;
	FrArchive     *archive;
	GFile         *parent;

	if (mime_type == NULL)
		mime_type = _g_mime_type_get_from_filename (file);

	/* try with the CAN_READ capability as well, this way we give
	 * priority to the commands that can read and write over commands
	 * that can only create a specific file format. */

	requested_capabilities = FR_ARCHIVE_CAN_READ_WRITE;
	archive_type = fr_get_archive_type_from_mime_type (mime_type, requested_capabilities);

	/* if no command was found, remove the read capability and try again */

	if (archive_type == 0) {
		requested_capabilities ^= FR_ARCHIVE_CAN_READ;
		archive_type = fr_get_archive_type_from_mime_type (mime_type, requested_capabilities);
	}

	archive = create_archive_for_mime_type (archive_type,
						file,
						mime_type,
						FR_ARCHIVE_CAN_WRITE);

	if (archive == NULL)
		return NULL;

	parent = g_file_get_parent (file);
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	private->have_write_permissions = _g_file_check_permissions (parent, W_OK);
	archive->read_only = ! fr_archive_is_capable_of (archive, FR_ARCHIVE_CAN_WRITE) || ! private->have_write_permissions;

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
open_data_free (OpenData *open_data)
{
	g_object_unref (open_data->file);
	_g_object_unref (open_data->cancellable);
	_g_object_unref (open_data->result);
	_g_object_unref (open_data->archive);
	g_free (open_data->buffer);
	g_free (open_data);
}


static void
open_data_complete_with_error (OpenData *load_data,
			       GError   *error)
{
	GSimpleAsyncResult *result;

	result = g_object_ref (load_data->result);
	g_simple_async_result_set_from_error (result, error);
	g_simple_async_result_complete_in_idle (result);

	g_object_unref (result);
}


static FrArchive *
create_archive_to_load_archive (GFile      *file,
			        const char *mime_type)
{
	FrArchiveCaps requested_capabilities;
	GType         archive_type;

	if (mime_type == NULL)
		return FALSE;

	/* try with the CAN_WRITE capability even when loading, this way we give
	 * priority to the commands that can read and write over commands
	 * that can only read a specific file format. */

	requested_capabilities = FR_ARCHIVE_CAN_READ_WRITE;
	archive_type = fr_get_archive_type_from_mime_type (mime_type, requested_capabilities);

	/* if no command was found, remove the write capability and try again */

	if (archive_type == 0) {
		requested_capabilities ^= FR_ARCHIVE_CAN_WRITE;
		archive_type = fr_get_archive_type_from_mime_type (mime_type, requested_capabilities);
	}

	return create_archive_for_mime_type (archive_type,
					     file,
					     mime_type,
					     requested_capabilities);
}


static void
open_archive_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	OpenData *open_data = user_data;
	GError   *error = NULL;

	if (! fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error)) {
		open_data_complete_with_error (open_data, error);
		return;
	}

	g_simple_async_result_complete_in_idle (open_data->result);
}


static void
open_archive_buffer_ready_cb (GObject      *source_object,
			      GAsyncResult *result,
			      gpointer      user_data)
{
	OpenData         *open_data = user_data;
	GError           *error = NULL;
	const char       *mime_type;
	gboolean          result_uncertain;
	FrArchive        *archive;
	char             *uri;
	char             *local_mime_type;

	if (! _g_file_load_buffer_finish (open_data->file, result, &open_data->buffer, &open_data->buffer_size, &error)) {
		open_data_complete_with_error (open_data, error);
		return;
	}

	if (open_data->buffer == NULL) {
		error = g_error_new_literal (FR_ERROR,
			FR_ERROR_UNSUPPORTED_FORMAT,
			_("Archive type not supported."));
		open_data_complete_with_error (open_data, error);
		return;
	}

	archive = NULL;
	uri = g_file_get_uri (open_data->file);
	local_mime_type = g_content_type_guess (uri, (guchar *) open_data->buffer, open_data->buffer_size, &result_uncertain);
	if (! result_uncertain) {
		const char *mime_type_from_filename;

		/* for example: "application/x-lrzip" --> "application/x-lrzip-compressed-tar" */
		mime_type_from_filename = _g_mime_type_get_from_filename (open_data->file);
		if ((mime_type_from_filename != NULL) && g_str_has_prefix (mime_type_from_filename, local_mime_type) && g_str_has_suffix (mime_type_from_filename, "-compressed-tar"))
			mime_type = _g_str_get_static (mime_type_from_filename);
		else
			mime_type = _g_str_get_static (local_mime_type);
		archive = create_archive_to_load_archive (open_data->file, mime_type);
	}
	if (archive == NULL) {
		mime_type = _g_mime_type_get_from_content (open_data->buffer, open_data->buffer_size);
		archive = create_archive_to_load_archive (open_data->file, mime_type);
		if (archive == NULL) {
			mime_type = _g_mime_type_get_from_filename (open_data->file);
			archive = create_archive_to_load_archive (open_data->file, mime_type);
			if (archive == NULL) {
				error = g_error_new_literal (FR_ERROR,
						     	     FR_ERROR_UNSUPPORTED_FORMAT,
						     	     _("Archive type not supported."));
				open_data_complete_with_error (open_data, error);
				return;
			}
		}
	}

	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	private->have_write_permissions = _g_file_check_permissions (fr_archive_get_file (archive), W_OK);
	archive->read_only = ! fr_archive_is_capable_of (archive, FR_ARCHIVE_CAN_WRITE) || ! private->have_write_permissions;
	open_data->archive = archive;

	if (FR_ARCHIVE_GET_CLASS (archive)->open != NULL)
		FR_ARCHIVE_GET_CLASS (archive)->open (archive,
						      open_data->cancellable,
						      open_archive_ready_cb,
						      open_data);
	else
		g_simple_async_result_complete_in_idle (open_data->result);

        g_free (local_mime_type);
        g_free (uri);
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
	open_data->buffer_size = 0;
	open_data->buffer = NULL;
        g_simple_async_result_set_op_res_gpointer (open_data->result,
        					   open_data,
                                                   (GDestroyNotify) open_data_free);

        /* load a few bytes to guess the archive type */

	_g_file_load_buffer_async (open_data->file,
				   BUFFER_SIZE_FOR_PRELOAD,
				   open_data->cancellable,
				   open_archive_buffer_ready_cb,
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


static gboolean
_fr_archive_update_progress_cb (gpointer user_data)
{
	FrArchive *archive = user_data;
	fr_archive_progress (archive, fr_archive_progress_get_fraction (archive));
	return TRUE;
}


static void
_fr_archive_activate_progress_update (FrArchive *archive)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	if (private->progress_event == 0)
		private->progress_event = g_timeout_add (PROGRESS_DELAY, _fr_archive_update_progress_cb, archive);
}


void
fr_archive_list (FrArchive           *archive,
		 const char          *password,
		 GCancellable        *cancellable,
		 GAsyncReadyCallback  callback,
		 gpointer             user_data)
{
	g_return_if_fail (archive != NULL);

	_fr_archive_activate_progress_update (archive);

	if (archive->files != NULL) {
		g_hash_table_remove_all (archive->files_hash);
		g_ptr_array_unref (archive->files);
		archive->files = g_ptr_array_new_full (FILE_ARRAY_INITIAL_SIZE, (GDestroyNotify) fr_file_data_free);
		archive->n_regular_files = 0;
	}

	FR_ARCHIVE_GET_CLASS (archive)->list (archive, password, cancellable, callback, user_data);
}


gboolean
fr_archive_operation_finish (FrArchive     *archive,
			     GAsyncResult  *result,
			     GError       **error)
{
	gboolean success;
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);

	if (private->progress_event != 0) {
		g_source_remove (private->progress_event);
		private->progress_event = 0;
	}

	success = ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);

	if (success && (g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result)) == fr_archive_list)) {
		/* order the list by name to speed up search */
		g_ptr_array_sort (archive->files, fr_file_data_compare_by_path);

		/* update the file_data hash */
		g_hash_table_remove_all (archive->files_hash);
		for (guint i = 0; i < archive->files->len; i++) {
			FrFileData *file_data = g_ptr_array_index (archive->files, i);
			g_hash_table_insert (archive->files_hash, file_data->original_path, file_data);
		}
	}

	archive->files_to_add_size = 0;

	if (! success && (error != NULL) && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (*error);
		*error = g_error_new_literal (FR_ERROR, FR_ERROR_STOPPED, "");
	}

	return success;
}


/* -- fr_archive_add_files -- */


typedef struct {
	FrArchive           *archive;
	GFile               *base_dir;
	char                *dest_dir;
	gboolean             update;
	gboolean             follow_links;
	char                *password;
	gboolean             encrypt_header;
	FrCompression        compression;
	guint                volume_size;
	GCancellable        *cancellable;
	GAsyncReadyCallback  callback;
	gpointer             user_data;
	FileFilter          *include_files_filter;
	FileFilter          *exclude_files_filter;
	FileFilter          *exclude_directories_filter;
} AddData;


static void
add_data_free (AddData *add_data)
{
	file_filter_unref (add_data->include_files_filter);
	file_filter_unref (add_data->exclude_files_filter);
	file_filter_unref (add_data->exclude_directories_filter);
	_g_object_unref (add_data->base_dir);
	g_free (add_data->dest_dir);
	g_free (add_data->password);
	_g_object_unref (add_data->cancellable);
	g_free (add_data);
}


static void
fr_archive_add_files_ready_cb (GList    *file_info_list, /* FileInfo list */
		      	       GError   *error,
		      	       gpointer  user_data)
{
	AddData            *add_data = user_data;
	FrArchive          *archive = add_data->archive;
	GSimpleAsyncResult *result;

	result = g_simple_async_result_new (G_OBJECT (archive),
					    add_data->callback,
					    add_data->user_data,
					    fr_archive_add_files);

	if (error != NULL) {
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);
	}
	else {
		GList *file_list;
		GList *scan;

		archive->files_to_add_size = 0;

		file_list = NULL;
		for (scan = file_info_list; scan; scan = scan->next) {
			FileInfo *data = scan->data;

			switch (g_file_info_get_file_type (data->info)) {
			case G_FILE_TYPE_REGULAR:
			case G_FILE_TYPE_DIRECTORY:
			case G_FILE_TYPE_SYMBOLIC_LINK:
				break;
			default: /* ignore any other type */
				continue;
			}

			if (! archive->propAddCanStoreFolders && (g_file_info_get_file_type (data->info) == G_FILE_TYPE_DIRECTORY))
				continue;

			file_list = g_list_prepend (file_list, g_object_ref (data->file));
			archive->files_to_add_size += g_file_info_get_size (data->info);
		}
		file_list = g_list_reverse (file_list);

		if (file_list != NULL) {
			fr_archive_action_started (archive, FR_ACTION_ADDING_FILES);
			_fr_archive_activate_progress_update (archive);

			FR_ARCHIVE_GET_CLASS (archive)->add_files (add_data->archive,
								   file_list,
								   add_data->base_dir,
								   add_data->dest_dir,
								   add_data->update,
								   add_data->follow_links,
								   add_data->password,
								   add_data->encrypt_header,
								   add_data->compression,
								   add_data->volume_size,
								   add_data->cancellable,
								   add_data->callback,
								   add_data->user_data);
		}
		else
			g_simple_async_result_complete_in_idle (result);
	}

	g_object_unref (result);
	add_data_free (add_data);
}


void
fr_archive_add_files (FrArchive           *archive,
		      GList               *file_list,
		      GFile               *base_dir,
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
	AddData *add_data;

	g_return_if_fail (! archive->read_only);

	add_data = g_new0 (AddData, 1);
	add_data->archive = archive;
	add_data->base_dir = _g_object_ref (base_dir);
	add_data->dest_dir = g_strdup (dest_dir);
	add_data->update = update;
	add_data->follow_links = follow_links;
	add_data->password = g_strdup (password);
	add_data->encrypt_header = encrypt_header;
	add_data->compression = compression;
	add_data->volume_size = volume_size;
	add_data->cancellable = _g_object_ref (cancellable);
	add_data->callback = callback;
	add_data->user_data = user_data;

	fr_archive_action_started (archive, FR_ACTION_GETTING_FILE_LIST);

	_g_file_list_query_info_async (file_list,
				       FILE_LIST_RECURSIVE | FILE_LIST_NO_BACKUP_FILES,
				       (G_FILE_ATTRIBUTE_STANDARD_NAME ","
					G_FILE_ATTRIBUTE_STANDARD_SIZE ","
					G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
					G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP),
				       cancellable,
				       NULL,
				       NULL,
				       fr_archive_add_files_ready_cb,
				       add_data);
}


/* -- fr_archive_add_files_with_filter -- */


static gboolean
directory_filter_cb (GFile     *file,
		     GFileInfo *info,
		     gpointer   user_data)
{
	AddData *add_data = user_data;

	return ! file_filter_empty (add_data->exclude_directories_filter)
			&& file_filter_matches (add_data->exclude_directories_filter, file);
}


static gboolean
file_filter_cb (GFile     *file,
		GFileInfo *info,
		gpointer   user_data)
{
	AddData *add_data = user_data;

	if (file_filter_matches (add_data->include_files_filter, file))
		return FALSE;

	return ! file_filter_empty (add_data->exclude_files_filter)
			&& file_filter_matches (add_data->exclude_files_filter, file);
}


void
fr_archive_add_files_with_filter (FrArchive           *archive,
				  GList               *file_list,
				  GFile               *source_dir,
				  const char          *include_files,
				  const char          *exclude_files,
				  const char          *exclude_directories,
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
	AddData       *add_data;
	FileListFlags  flags;

	g_return_if_fail (! archive->read_only);

	add_data = g_new0 (AddData, 1);
	add_data->archive = archive;
	add_data->base_dir = _g_object_ref (source_dir);
	add_data->dest_dir = g_strdup (dest_dir);
	add_data->update = update;
	add_data->follow_links = follow_links;
	add_data->password = g_strdup (password);
	add_data->encrypt_header = encrypt_header;
	add_data->compression = compression;
	add_data->volume_size = volume_size;
	add_data->cancellable = _g_object_ref (cancellable);
	add_data->callback = callback;
	add_data->user_data = user_data;
	add_data->include_files_filter = file_filter_new (include_files);
	add_data->exclude_files_filter = file_filter_new (exclude_files);
	add_data->exclude_directories_filter = file_filter_new (exclude_directories);

	fr_archive_action_started (archive, FR_ACTION_GETTING_FILE_LIST);

	flags = FILE_LIST_RECURSIVE | FILE_LIST_NO_BACKUP_FILES;
	if (! follow_links)
		flags |= FILE_LIST_NO_FOLLOW_LINKS;
	_g_file_list_query_info_async (file_list,
				       flags,
				       (G_FILE_ATTRIBUTE_STANDARD_NAME ","
					G_FILE_ATTRIBUTE_STANDARD_SIZE ","
					G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
					G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP),
				       cancellable,
				       directory_filter_cb,
				       file_filter_cb,
				       fr_archive_add_files_ready_cb,
				       add_data);
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

	_fr_archive_activate_progress_update (archive);

	FR_ARCHIVE_GET_CLASS (archive)->remove_files (archive,
						      file_list,
						      compression,
						      cancellable,
						      callback,
						      user_data);
}


static gsize
_fr_archive_get_file_list_size (FrArchive *archive,
				GList     *file_list)
{
	gsize     total_size = 0;
	gboolean  local_file_list = FALSE;
	GList    *scan;

	if (file_list == NULL) {
		file_list = g_hash_table_get_keys (archive->files_hash);
		local_file_list = TRUE;
	}

	for (scan = file_list; scan; scan = scan->next) {
		const char *original_path = scan->data;
		FrFileData *file_data;

		file_data = g_hash_table_lookup (archive->files_hash, original_path);
		if (file_data != NULL)
			total_size += file_data->size;
	}

	if (local_file_list)
		g_list_free (file_list);

	return total_size;
}


void
fr_archive_extract (FrArchive           *archive,
		    GList               *file_list,
		    GFile               *destination,
		    const char          *base_dir,
		    gboolean             skip_older,
		    gboolean             overwrite,
		    gboolean             junk_paths,
		    const char          *password,
		    GCancellable        *cancellable,
		    GAsyncReadyCallback  callback,
		    gpointer             user_data)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	_g_object_unref (private->extraction_destination);
	private->extraction_destination = g_object_ref (destination);

	fr_archive_progress_set_total_bytes (archive, _fr_archive_get_file_list_size (archive, file_list));
	_fr_archive_activate_progress_update (archive);

	FR_ARCHIVE_GET_CLASS (archive)->extract_files (archive,
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


/* -- fr_archive_extract_here -- */


typedef struct {
	FrArchive          *archive;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;
} ExtractHereData;


static void
extract_here_data_free (ExtractHereData *e_data)
{
	_g_object_unref (e_data->cancellable);
	g_object_unref (e_data->result);
	g_free (e_data);
}


static void
move_here (FrArchive    *archive,
	   GCancellable *cancellable)
{
	GFile  *extraction_destination;
	GFile  *directory_content;
	GFile  *parent;
	GFile  *parent_parent;
	char   *content_name;
	GFile  *new_directory_content;
	GError *error = NULL;

	extraction_destination = fr_archive_get_last_extraction_destination (archive);
	directory_content = _g_file_get_dir_content_if_unique (extraction_destination);
	if (directory_content == NULL)
		return;

	parent = g_file_get_parent (directory_content);

	if (g_file_equal (parent, extraction_destination)) {
		GFile *new_destination;

		new_destination = _g_file_create_alternative_for_file (extraction_destination);
		if (! g_file_move (extraction_destination, new_destination, 0, cancellable, NULL, NULL, &error)) {
			g_warning ("%s", error->message);
			g_clear_error (&error);
		}

		fr_archive_set_last_extraction_destination (archive, new_destination);

		g_object_unref (directory_content);
		directory_content = _g_file_get_dir_content_if_unique (new_destination);
		g_object_unref (new_destination);

		if (directory_content == NULL)
			return;

		g_object_unref (parent);
		parent = g_file_get_parent (directory_content);
	}

	parent_parent = g_file_get_parent (parent);
	content_name = g_file_get_basename (directory_content);
	new_directory_content = _g_file_create_alternative (parent_parent, content_name);
	g_free (content_name);

	if (! g_file_move (directory_content, new_directory_content, 0, cancellable, NULL, NULL, &error)) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}

	if (! g_file_delete (parent, cancellable, &error)) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}

	fr_archive_set_last_extraction_destination (archive, new_directory_content);

	g_object_unref (new_directory_content);
	g_object_unref (parent_parent);
	g_object_unref (parent);
	g_object_unref (directory_content);
}


static void
extract_here_ready_cb (GObject      *source_object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
	ExtractHereData *e_data = user_data;
	GError          *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	if (error != NULL) {
		_g_file_remove_directory (fr_archive_get_last_extraction_destination (e_data->archive), NULL, NULL);
		g_simple_async_result_set_from_error (e_data->result, error);
		g_error_free (error);
	}
	else
		move_here (e_data->archive, e_data->cancellable);

	g_simple_async_result_complete_in_idle (e_data->result);

	extract_here_data_free (e_data);
}


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
	ext = fr_get_archive_filename_extension (name);
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


static GFile *
get_extract_here_destination (GFile   *file,
			      GError **error)
{
	GFile *directory = NULL;
	char  *desired_destination;
	int    n = 1;

	desired_destination = get_desired_destination_for_archive (file);
	do {
		char *uri;

		*error = NULL;

		_g_object_unref (directory);
		if (n == 1)
			uri = g_strdup (desired_destination);
		else
			uri = g_strdup_printf ("%s%%20(%d)", desired_destination, n);
		directory = g_file_new_for_uri (uri);
		g_file_make_directory (directory, NULL, error);
		n++;

		g_free (uri);
	}
	while (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS));

	if (*error != NULL) {
		g_warning ("could not create destination folder: %s\n", (*error)->message);
		_g_clear_object (&directory);
	}

	g_free (desired_destination);

	return directory;
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
	GSimpleAsyncResult *result;
	GFile              *destination;
	GError             *error = NULL;
	ExtractHereData    *e_data;
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);

	result = g_simple_async_result_new (G_OBJECT (archive),
					    callback,
					    user_data,
					    fr_archive_extract_here);

	destination = get_extract_here_destination (private->file, &error);
	if (error != NULL) {
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_object_unref (result);
		g_error_free (error);
		return FALSE;
	}

	e_data = g_new0 (ExtractHereData, 1);
	e_data->archive = archive;
	e_data->cancellable = _g_object_ref (cancellable);
	e_data->result = result;

	fr_archive_extract (archive,
			    NULL,
			    destination,
			    NULL,
			    skip_older,
			    overwrite,
			    junk_path,
			    password,
			    cancellable,
			    extract_here_ready_cb,
			    e_data);

	g_object_unref (destination);

	return TRUE;
}


void
fr_archive_set_last_extraction_destination (FrArchive *archive,
					    GFile     *folder)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	_g_clear_object (&private->extraction_destination);
	if (folder != NULL)
		private->extraction_destination = g_object_ref (folder);
}


GFile *
fr_archive_get_last_extraction_destination (FrArchive *archive)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);

	return private->extraction_destination;
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
	_fr_archive_activate_progress_update (archive);

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
			    GFile               *file,
			    char                *password,
			    gboolean             encrypt_header,
			    FrCompression        compression,
			    guint                volume_size,
			    FrClipboardOp        op,
			    char                *base_dir,
			    GList               *files,
			    GFile               *tmp_dir,
			    char                *current_dir,
			    GCancellable        *cancellable,
			    GAsyncReadyCallback  callback,
			    gpointer             user_data)
{
	fr_archive_action_started (archive, FR_ACTION_ADDING_FILES);
	_fr_archive_activate_progress_update (archive);

	FR_ARCHIVE_GET_CLASS (archive)->paste_clipboard (archive,
							 file,
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


/* -- add_dropped_files  -- */


struct _DroppedItemsData {
	FrArchive           *archive;
	GList               *item_list;
	char                *dest_dir;
	char                *password;
	gboolean             encrypt_header;
	FrCompression        compression;
	guint                volume_size;
	GCancellable        *cancellable;
	GAsyncReadyCallback  callback;
	gpointer             user_data;
} ;


static DroppedItemsData *
dropped_items_data_new (FrArchive           *archive,
			GList               *item_list,
			const char          *dest_dir,
			const char          *password,
			gboolean             encrypt_header,
			FrCompression        compression,
			guint                volume_size,
			GCancellable        *cancellable,
			GAsyncReadyCallback  callback,
			gpointer             user_data)
{
	DroppedItemsData *data;

	data = g_new0 (DroppedItemsData, 1);
	data->archive = archive;
	data->item_list = _g_object_list_ref (item_list);
	if (dest_dir != NULL)
		data->dest_dir = g_strdup (dest_dir);
	if (password != NULL)
		data->password = g_strdup (password);
	data->encrypt_header = encrypt_header;
	data->compression = compression;
	data->volume_size = volume_size;
	data->cancellable = _g_object_ref (cancellable);
	data->callback = callback;
	data->user_data = user_data;

	return data;
}


static void
dropped_items_data_free (DroppedItemsData *data)
{
	if (data == NULL)
		return;
	_g_object_list_unref (data->item_list);
	g_free (data->dest_dir);
	g_free (data->password);
	_g_object_unref (data->cancellable);
	g_free (data);
}


static gboolean
all_files_in_same_dir (GList *list)
{
	gboolean  same_dir = TRUE;
	GFile    *first_parent;
	GList    *scan;

	if (list == NULL)
		return FALSE;

	first_parent = g_file_get_parent (G_FILE (list->data));
	if (first_parent == NULL)
		return TRUE;

	for (scan = list->next; same_dir && scan; scan = scan->next) {
		GFile *file = G_FILE (scan->data);
		GFile *parent;

		parent = g_file_get_parent (file);
		if (parent == NULL) {
			same_dir = FALSE;
			break;
		}

		if (_g_file_cmp_uris (first_parent, parent) != 0)
			same_dir = FALSE;

		g_object_unref (parent);
	}

	g_object_unref (first_parent);

	return same_dir;
}


static void add_dropped_items (DroppedItemsData *data);


static void
add_dropped_items_ready_cb (GObject      *source_object,
			    GAsyncResult *result,
			    gpointer      user_data)
{
	DroppedItemsData *data = user_data;
	FrArchive        *archive = data->archive;
	GError           *error = NULL;

	fr_archive_operation_finish (FR_ARCHIVE (source_object), result, &error);
	if (error != NULL) {
		FrArchivePrivate *private = fr_archive_get_instance_private (archive);
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (data->archive),
						    data->callback,
						    data->user_data,
						    fr_archive_add_dropped_items);
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_error_free (error);
		dropped_items_data_free (private->dropped_items_data);
		private->dropped_items_data = NULL;
		return;
	}

	/* continue adding the items... */
	add_dropped_items (data);
}


static void
add_dropped_items (DroppedItemsData *data)
{
	FrArchive *archive = data->archive;
	GList     *list = data->item_list;
	GList     *scan;

	if (list == NULL) {
		FrArchivePrivate *private = fr_archive_get_instance_private (archive);
		GSimpleAsyncResult *result;

		result = g_simple_async_result_new (G_OBJECT (data->archive),
						    data->callback,
						    data->user_data,
						    fr_archive_add_dropped_items);
		g_simple_async_result_complete_in_idle (result);

		dropped_items_data_free (private->dropped_items_data);
		private->dropped_items_data = NULL;
		return;
	}

	/* if all files and directories are in the same directory call
	 * fr_archive_add_items... */

	if (all_files_in_same_dir (list)) {
		GFile *first_parent;

		data->item_list = NULL;

		first_parent = g_file_get_parent (G_FILE (list->data));
		fr_archive_add_files (FR_ARCHIVE (archive),
				      list,
				      first_parent,
				      data->dest_dir,
				      FALSE,
				      FALSE,
				      data->password,
				      data->encrypt_header,
				      data->compression,
				      data->volume_size,
				      data->cancellable,
				      add_dropped_items_ready_cb,
				      data);

		g_object_unref (first_parent);
		_g_object_list_unref (list);
		return;
	}

	/* ...else add a directory at a time. */

	for (scan = list; scan; scan = scan->next) {
		GFile *file = G_FILE (scan->data);
		GList *singleton;
		GFile *parent;

		if (! _g_file_query_is_dir (file))
			continue;

		data->item_list = g_list_remove_link (list, scan);

		singleton = g_list_prepend (NULL, file);
		parent = g_file_get_parent (file);
		fr_archive_add_files (FR_ARCHIVE (archive),
				      singleton,
				      parent,
				      data->dest_dir,
				      FALSE,
				      FALSE,
				      data->password,
				      data->encrypt_header,
				      data->compression,
				      data->volume_size,
				      data->cancellable,
				      add_dropped_items_ready_cb,
				      data);

		g_list_free (singleton);
		g_object_unref (parent);
		g_object_unref (file);

		return;
	}

	/* At this point all the directories have been added, only files
	 * remaining.  If all files are in the same directory call
	 * fr_archive_add_files. */

	data->item_list = NULL;

	if (all_files_in_same_dir (list)) {
		GFile *first_parent;

		first_parent = g_file_get_parent (G_FILE (list->data));
		fr_archive_add_files (FR_ARCHIVE (archive),
				      list,
				      first_parent,
				      data->dest_dir,
				      FALSE,
				      FALSE,
				      data->password,
				      data->encrypt_header,
				      data->compression,
				      data->volume_size,
				      data->cancellable,
				      add_dropped_items_ready_cb,
				      data);

		g_object_unref (first_parent);
		_g_object_list_unref (list);
	}
	else
		/* ...else call the archive specific function to add the files in the
		 * current archive directory. */

		FR_ARCHIVE_GET_CLASS (archive)->add_dropped_files (archive,
								   list,
								   data->dest_dir,
								   data->password,
								   data->encrypt_header,
								   data->compression,
								   data->volume_size,
								   data->cancellable,
								   add_dropped_items_ready_cb,
								   data);

	_g_string_list_free (list);
}


void
fr_archive_add_dropped_items (FrArchive           *archive,
			      GList               *item_list,
			      const char          *dest_dir,
			      const char          *password,
			      gboolean             encrypt_header,
			      FrCompression        compression,
			      guint                volume_size,
			      GCancellable        *cancellable,
			      GAsyncReadyCallback  callback,
			      gpointer             user_data)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	GSimpleAsyncResult *result;
	GList              *scan;

	result = g_simple_async_result_new (G_OBJECT (archive),
					    callback,
					    user_data,
					    fr_archive_add_dropped_items);

	if (archive->read_only) {
		GError *error;

		error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, ! private->have_write_permissions ? _("You don’t have the right permissions.") : _("This archive type cannot be modified"));
		g_simple_async_result_set_from_error (result, error);
		g_simple_async_result_complete_in_idle (result);

		g_error_free (error);
		g_object_unref (result);
		return;
	}

	/* FIXME: make this check for all the add actions */
	for (scan = item_list; scan; scan = scan->next) {
		if (_g_file_cmp_uris (G_FILE (scan->data), private->file) == 0) {
			GError *error;

			error = g_error_new_literal (FR_ERROR, FR_ERROR_GENERIC, _("You can’t add an archive to itself."));
			g_simple_async_result_set_from_error (result, error);
			g_simple_async_result_complete_in_idle (result);

			g_error_free (error);
			g_object_unref (result);
			return;
		}
	}

	if (private->dropped_items_data != NULL)
		dropped_items_data_free (private->dropped_items_data);
	private->dropped_items_data = dropped_items_data_new (archive,
								    item_list,
								    dest_dir,
								    password,
								    encrypt_header,
								    compression,
								    volume_size,
								    cancellable,
								    callback,
								    user_data);
	add_dropped_items (private->dropped_items_data);

	g_object_unref (result);
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
	_fr_archive_activate_progress_update (archive);
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
	_fr_archive_set_file (self, file, FALSE);
}


void
fr_archive_change_name (FrArchive  *archive,
		        const char *filename)
{
	const char *name;
	FrArchivePrivate *private = fr_archive_get_instance_private (archive);
	GFile      *parent;
	GFile      *file;

	name = _g_path_get_basename (filename);

	parent = g_file_get_parent (private->file);
	file = g_file_get_child (parent, name);;
	_fr_archive_set_file (archive, file, FALSE);

	g_object_unref (parent);
	g_object_unref (file);
}


void
fr_archive_action_started (FrArchive *archive,
			   FrAction   action)
{
	g_signal_emit (archive,
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
fr_archive_progress_set_total_files (FrArchive *self,
				     int        n_files)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	g_mutex_lock (&private->progress_mutex);
	private->total_files = n_files;
	private->completed_files = 0;
	g_mutex_unlock (&private->progress_mutex);
}


int
fr_archive_progress_get_total_files (FrArchive *self)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	int result;

	g_mutex_lock (&private->progress_mutex);
	result = private->total_files;
	g_mutex_unlock (&private->progress_mutex);

	return result;
}


int
fr_archive_progress_get_completed_files (FrArchive *self)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	int result;

	g_mutex_lock (&private->progress_mutex);
	result = private->completed_files;
	g_mutex_unlock (&private->progress_mutex);

	return result;
}


double
fr_archive_progress_inc_completed_files (FrArchive *self,
		 	 	 	 int        new_completed)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	double fraction;

	g_mutex_lock (&private->progress_mutex);
	private->completed_files += new_completed;
	if (private->total_files > 0)
		fraction = FILES_FRACTION (self);
	else
		fraction = 0.0;
	/*g_print ("%d / %d  : %f\n", private->completed_files, private->total_files + 1, fraction);*/
	g_mutex_unlock (&private->progress_mutex);

	return fraction;

}


void
fr_archive_progress_set_total_bytes (FrArchive *self,
				     gsize      total)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	g_mutex_lock (&private->progress_mutex);
	private->total_bytes = total;
	private->completed_bytes = 0;
	g_mutex_unlock (&private->progress_mutex);
}


static double
_set_completed_bytes (FrArchive *self,
		      gsize      completed_bytes)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	double fraction;

	private->completed_bytes = completed_bytes;
	if (private->total_bytes > 0)
		fraction = BYTES_FRACTION (self);
	else
		fraction = 0.0;
	/*g_print ("%" G_GSIZE_FORMAT " / %" G_GSIZE_FORMAT "  : %f\n", private->completed_bytes, private->total_bytes + 1, fraction);*/

	return fraction;
}


double
fr_archive_progress_set_completed_bytes (FrArchive *self,
					 gsize      completed_bytes)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	double fraction;

	g_mutex_lock (&private->progress_mutex);
	fraction = _set_completed_bytes (self, completed_bytes);
	g_mutex_unlock (&private->progress_mutex);

	return fraction;
}

double
fr_archive_progress_inc_completed_bytes (FrArchive *self,
					 gsize      new_completed)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	double fraction;

	g_mutex_lock (&private->progress_mutex);
	fraction = _set_completed_bytes (self, private->completed_bytes + new_completed);
	g_mutex_unlock (&private->progress_mutex);

	return fraction;
}


double
fr_archive_progress_get_fraction (FrArchive *self)
{
	FrArchivePrivate *private = fr_archive_get_instance_private (self);
	double fraction;

	g_mutex_lock (&private->progress_mutex);
	if ((private->total_bytes > 0) && (private->completed_bytes > 0)) {
		fraction = BYTES_FRACTION (self);
		/*g_print ("%" G_GSIZE_FORMAT " / %" G_GSIZE_FORMAT "  : %f\n", private->completed_bytes, private->total_bytes + 1, fraction);*/
	}
	else if (private->total_files > 0) {
		fraction = FILES_FRACTION (self);
		/*g_print ("%d / %d  : %f\n", private->completed_files, private->total_files + 1, fraction);*/
	}
	else
		fraction = 0.0;
	g_mutex_unlock (&private->progress_mutex);

	return fraction;
}


void
fr_archive_add_file (FrArchive *self,
		     FrFileData *file_data)
{
	fr_file_data_update_content_type (file_data);
	g_ptr_array_add (self->files, file_data);
	if (! file_data->dir)
		self->n_regular_files++;
}


gboolean
_g_file_is_archive (GFile *file)
{
	const char *mime_type = NULL;
	gboolean    is_archive = FALSE;
	char       *buffer;
	int         buffer_size;

	buffer_size = BUFFER_SIZE_FOR_PRELOAD;
	buffer = g_new (char, buffer_size);

	if (g_load_file_in_buffer (file, buffer, buffer_size, NULL)) {
		char     *uri;
		gboolean  result_uncertain;

		uri = g_file_get_uri (file);
		mime_type = g_content_type_guess (uri, (guchar *) buffer, buffer_size, &result_uncertain);
		if (result_uncertain) {
			mime_type = _g_mime_type_get_from_content (buffer, buffer_size);
			if (mime_type == NULL)
				mime_type = _g_mime_type_get_from_filename (file);
		}

		if (mime_type != NULL) {
			int i;

			for (i = 0; mime_type_desc[i].mime_type != NULL; i++) {
				if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0) {
					is_archive = TRUE;
					break;
				}
			}
		}

		g_free (uri);
	}

	g_free (buffer);

	return is_archive;
}
