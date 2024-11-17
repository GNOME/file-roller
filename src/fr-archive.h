/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2001, 2003, 2012 Free Software Foundation, Inc.
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

#ifndef FR_ARCHIVE_H
#define FR_ARCHIVE_H

#include <glib.h>
#include "fr-file-data.h"
#include "typedefs.h"

typedef enum {
	FR_ACTION_NONE,
	FR_ACTION_CREATING_NEW_ARCHIVE,
	FR_ACTION_LOADING_ARCHIVE,            /* loading the archive from a remote location */
	FR_ACTION_LISTING_CONTENT,            /* listing the content of the archive */
	FR_ACTION_DELETING_FILES,             /* deleting files from the archive */
	FR_ACTION_TESTING_ARCHIVE,            /* testing the archive integrity */
	FR_ACTION_GETTING_FILE_LIST,          /* getting the file list (when fr_archive_add_with_wildcard or
						 fr_archive_add_directory are used, we need to scan a directory
						 and collect the files to add to the archive, this
						 may require some time to complete, so the operation
						 is asynchronous) */
	FR_ACTION_COPYING_FILES_FROM_REMOTE,  /* copying files to be added to the archive from a remote location */
	FR_ACTION_ADDING_FILES,               /* adding files to an archive */
	FR_ACTION_EXTRACTING_FILES,           /* extracting files */
	FR_ACTION_COPYING_FILES_TO_REMOTE,    /* copying extracted files to a remote location */
	FR_ACTION_CREATING_ARCHIVE,           /* creating a local archive */
	FR_ACTION_SAVING_REMOTE_ARCHIVE,      /* copying the archive to a remote location */
	FR_ACTION_RENAMING_FILES,             /* renaming files stored in the archive */
	FR_ACTION_PASTING_FILES,              /* pasting files from the clipboard into the archive */
	FR_ACTION_UPDATING_FILES,             /* updating the files modified with an external application */
	FR_ACTION_ENCRYPTING_ARCHIVE          /* saving the archive with a different password */
} FrAction;

#ifdef DEBUG
extern char *action_names[];
#endif

#define FR_TYPE_ARCHIVE            (fr_archive_get_type ())
#define FR_ARCHIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), FR_TYPE_ARCHIVE, FrArchive))
#define FR_ARCHIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), FR_TYPE_ARCHIVE, FrArchiveClass))
#define FR_IS_ARCHIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FR_TYPE_ARCHIVE))
#define FR_IS_ARCHIVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), FR_TYPE_ARCHIVE))
#define FR_ARCHIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), FR_TYPE_ARCHIVE, FrArchiveClass))

typedef struct _FrArchive         FrArchive;
typedef struct _FrArchiveClass    FrArchiveClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FrArchive, g_object_unref)

struct _FrArchive {
	GObject  __parent;

	/*<public, read only>*/

	const char    *mime_type;
	GPtrArray     *files;                      /* Array of FrFileData */
	GHashTable    *files_hash;                 /* Hash of FrFileData with original_path as key */
	int            n_regular_files;

	/*<public>*/

	char          *password;
	gboolean       encrypt_header;
	FrCompression  compression;
	gboolean       multi_volume;
	guint          volume_size;
	gboolean       read_only;                  /* Whether archive is
						    * read-only for whatever
						    * reason. */

	/*<protected>*/

	gssize         files_to_add_size;

	/* features. */

	/* propAddCanReplace:
	 *
	 * TRUE if the command can overwrite a file in the archive.
	 */
	guint          propAddCanReplace : 1;

	/* propAddCanReplace:
	 *
	 * TRUE if the command can overwrite a file in the archive if older
	 * then the file on disk.
	 */
	guint          propAddCanUpdate : 1;

	/* propAddCanStoreFolders:
	 *
	 * TRUE if the command can store folder entries inside the archive.
	 */
	guint          propAddCanStoreFolders : 1;

	/*
	 * propAddCanStoreLinks
	 *
	 * TRUE if the command can store symbolic links
	 */
	guint          propAddCanStoreLinks : 1;

	/* propAddCanFollowDirectoryLinksWithoutDereferencing:
	 *
	 * is used to overcome an issue with 7zip when adding a file in a
	 * subfolder.  For example if we want to add to an archive
	 *
	 * /home/user/index.html
	 *
	 * in the folder 'doc'
	 *
	 * we create a symbolic link doc -> /home/user
	 *
	 * and use the following command to add the file
	 *
	 * 7z a -bd -y -mx=7 -- /home/user/archive.7z doc/index.html
	 *
	 * this gives an error because 7zip doesn't see the doc/index.html file
	 * for some reason, in this case we have to add the -l option to always
	 * deference the links.
	 *
	 * This means that when adding files to a subfolder in an 7zip archive
	 * we cannot store symbolic links as such, suboptimal but more
	 * acceptable than an error.
	 */
	guint          propAddCanFollowDirectoryLinksWithoutDereferencing : 1;

	/* propExtractCanAvoidOverwrite:
	 *
	 * TRUE if the command can avoid to overwrite the files on disk.
	 */
	guint          propExtractCanAvoidOverwrite : 1;

	/* propExtractCanSkipOlder:
	 *
	 * TRUE if the command can avoid to overwrite a file on disk when it is
	 * newer than the file in the archive.
	 */
	guint          propExtractCanSkipOlder : 1;

	/* propExtractCanJunkPaths:
	 *
	 * TRUE if the command can extract the files in the current folder
	 * without recreating the directory structure.
	 */
	guint          propExtractCanJunkPaths : 1;

	/* propPassword:
	 *
	 * TRUE if the command can use passwords for adding or extracting files.
	 */
	guint          propPassword : 1;

	/* propTest:
	 *
	 * TRUE if the command can test the archive integrity.
	 */
	guint          propTest : 1;

	/* propCanExtractAll:
	 *
	 * TRUE if the command extract all the files when no file is specified.
	 */
	guint          propCanExtractAll : 1;

	/* propCanDeleteNonEmptyFolders:
	 *
	 * is used to overcome an issue with tar, that deletes only the folder
	 * entry in the archive instead of deleting the folder content
	 * recursively.
	 */
	guint          propCanDeleteNonEmptyFolders : 1;

	/* propCanDeleteAllFiles:
	 *
	 * TRUE if the command does not delete the archive itself if all the
	 * files in the archive are deleted.
	 */

	guint          propCanDeleteAllFiles : 1;

	/* propCanExtractNonEmptyFolders:
	 *
	 * is used to overcome an issue with tar.  For example if
	 * the content of a tar archive is
	 *
	 * readme.txt
	 * doc/
	 * doc/page1.html
	 * doc/page2.html
	 *
	 * and we want to extract the content of the doc folder, the command:
	 *
	 * tar -xf archive.tar doc doc/page1.html doc/page2.html
	 *
	 * gives an error.
	 * To fix the issue we have to remove the files inside the doc
	 * folder from the command line, getting the following command:
	 *
	 * tar -xf archive.tar doc
	 */
	guint          propCanExtractNonEmptyFolders : 1;

	/* propListFromFile:
	 *
	 * if TRUE the command has an option to read the file list from a file
	 */
	guint          propListFromFile : 1;
};

struct _FrArchiveClass {
	GObjectClass __parent_class;

	/*< signals >*/

	void          (*start)             (FrArchive           *archive,
					    FrAction             action);
	void          (*progress)          (FrArchive           *archive,
			           	    double               fraction);
	void          (*message)           (FrArchive           *archive,
			           	    const char          *msg);
	void          (*stoppable)         (FrArchive           *archive,
			           	    gboolean             value);
	void          (*working_archive)   (FrArchive           *archive,
			           	    const char          *uri);

	/*< virtual functions >*/

	const char ** (*get_mime_types)    (FrArchive           *archive);
	FrArchiveCaps  (*get_capabilities)  (FrArchive           *archive,
					    const char          *mime_type,
					    gboolean             check_command);
	void          (*set_mime_type)     (FrArchive           *archive,
				            const char          *mime_type);
	const char *  (*get_packages)      (FrArchive           *archive,
					    const char          *mime_type);
	void          (*open)              (FrArchive           *archive,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
	void          (*list)              (FrArchive           *archive,
					    const char          *password,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
	void          (*add_files)         (FrArchive           *archive,
					    GList               *file_list, /* GFile list */
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
					    gpointer             user_data);
	void          (*extract_files)     (FrArchive           *archive,
	    				    GList               *file_list,
	    				    GFile               *destination,
	    				    const char          *base_dir,
	    				    gboolean             skip_older,
	    				    gboolean             overwrite,
	    				    gboolean             junk_paths,
	    				    const char          *password,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
	void          (*remove_files)      (FrArchive           *archive,
				   	    GList               *file_list,
				   	    FrCompression        compression,
				   	    GCancellable        *cancellable,
				   	    GAsyncReadyCallback  callback,
				   	    gpointer             user_data);
	void          (*test_integrity)    (FrArchive           *archive,
			 	   	    const char          *password,
			 	   	    GCancellable        *cancellable,
			 	   	    GAsyncReadyCallback  callback,
			 	   	    gpointer             user_data);
	void          (*rename)            (FrArchive           *archive,
					    GList               *file_list,
				   	    const char          *old_name,
				   	    const char          *new_name,
				   	    const char          *current_dir,
				   	    gboolean             is_dir,
				   	    gboolean             dir_in_archive,
				   	    const char          *original_path,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
	void          (*paste_clipboard)   (FrArchive           *archive,
					    GFile               *archive_file,
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
					    gpointer             user_data);
	void          (*add_dropped_files) (FrArchive           *archive,
				   	    GList               *item_list,
				   	    const char          *dest_dir,
				   	    const char          *password,
				   	    gboolean             encrypt_header,
				   	    FrCompression        compression,
				   	    guint                volume_size,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
	void          (*update_open_files) (FrArchive           *archive,
					    GList               *file_list,
					    GList               *dir_list,
					    const char          *password,
					    gboolean             encrypt_header,
					    FrCompression        compression,
					    guint                volume_size,
					    GCancellable        *cancellable,
					    GAsyncReadyCallback  callback,
					    gpointer             user_data);
};

GType         fr_archive_get_type                (void);

/**
 * fr_archive_get_file:
 * Returns: (transfer none)
 */
GFile *       fr_archive_get_file                (FrArchive           *archive);
gboolean      fr_archive_is_capable_of           (FrArchive           *archive,
						  FrArchiveCaps        capabilities);

/**
 * fr_archive_get_supported_types:
 * Returns: (transfer none)
 */
const char ** fr_archive_get_supported_types     (FrArchive           *archive);
void          fr_archive_update_capabilities     (FrArchive           *archive);
FrArchiveCaps  fr_archive_get_capabilities        (FrArchive           *archive,
						  const char          *mime_type,
						  gboolean             check_command);
void          fr_archive_set_mime_type           (FrArchive           *archive,
						  const char          *mime_type);
const char *  fr_archive_get_mime_type           (FrArchive           *archive);
const char *  fr_archive_get_packages            (FrArchive           *archive,
						  const char          *mime_type);
void          fr_archive_set_password            (FrArchive           *archive,
						  const char          *password);
void          fr_archive_set_stoppable           (FrArchive           *archive,
						  gboolean             stoppable);

/**
 * fr_archive_create:
 * Returns: (transfer full)
 */
FrArchive *   fr_archive_create                  (GFile               *file,
						  const char          *mime_type);
void          fr_archive_open                    (GFile               *file,
		       	       	       	          GCancellable        *cancellable,
		       	       	       	          GAsyncReadyCallback  callback,
		       	       	       	          gpointer             user_data);

/**
 * fr_archive_open_finish:
 * Returns: (transfer full)
 */
FrArchive *   fr_archive_open_finish             (GFile               *file,
						  GAsyncResult        *result,
						  GError             **error);
void          fr_archive_list                    (FrArchive           *archive,
						  const char          *password,
						  GCancellable        *cancellable,
		       	       	       	          GAsyncReadyCallback  callback,
		       	       	       	          gpointer             user_data);
gboolean      fr_archive_operation_finish        (FrArchive           *archive,
						  GAsyncResult        *result,
						  GError             **error);

/**
 * fr_archive_add_files:
 * @file_list: (element-type GFile)
 */
void          fr_archive_add_files               (FrArchive           *archive,
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
						  gpointer             user_data);

/**
 * fr_archive_add_files_with_filter:
 * @file_list: (element-type GFile)
 */
void          fr_archive_add_files_with_filter   (FrArchive           *archive,
						  GList               *file_list,
						  GFile               *base_dir,
						  const char          *include_files,
						  const char          *exclude_files,
						  const char          *exclude_folders,
						  const char          *dest_dir,
						  gboolean             update,
						  gboolean             follow_links,
						  const char          *password,
						  gboolean             encrypt_header,
						  FrCompression        compression,
						  guint                volume_size,
						  GCancellable        *cancellable,
						  GAsyncReadyCallback  callback,
						  gpointer             user_data);

/**
 * fr_archive_remove:
 * @file_list: (element-type GFile)
 */
void          fr_archive_remove                  (FrArchive           *archive,
						  GList               *file_list,
						  FrCompression        compression,
						  GCancellable        *cancellable,
						  GAsyncReadyCallback  callback,
						  gpointer             user_data);

/**
 * fr_archive_extract:
 * @file_list: (element-type GFile)
 */
void          fr_archive_extract                 (FrArchive           *archive,
						  GList               *file_list,
						  GFile               *destination,
						  const char          *base_dir,
						  gboolean             skip_older,
						  gboolean             overwrite,
						  gboolean             junk_path,
						  const char          *password,
		       	       	       	          GCancellable        *cancellable,
		       	       	       	          GAsyncReadyCallback  callback,
		       	       	       	          gpointer             user_data);
gboolean      fr_archive_extract_here            (FrArchive           *archive,
						  gboolean             skip_older,
						  gboolean             overwrite,
						  gboolean             junk_path,
						  const char          *password,
		       	       	       	          GCancellable        *cancellable,
		       	       	       	          GAsyncReadyCallback  callback,
		       	       	       	          gpointer             user_data);
void          fr_archive_set_last_extraction_destination
						 (FrArchive           *archive,
						  GFile               *folder);

/**
 * fr_archive_get_last_extraction_destination:
 * Returns: (transfer none)
 */
GFile *       fr_archive_get_last_extraction_destination
						 (FrArchive           *archive);
void          fr_archive_test                    (FrArchive           *archive,
						  const char          *password,
		       	       	       	          GCancellable        *cancellable,
		       	       	       	          GAsyncReadyCallback  callback,
		       	       	       	          gpointer             user_data);

/**
 * fr_archive_rename:
 * @file_list: (element-type GFile)
 */
void          fr_archive_rename                  (FrArchive           *archive,
						  GList               *file_list,
						  const char          *old_name,
						  const char          *new_name,
						  const char          *current_dir,
						  gboolean             is_dir,
						  gboolean             dir_in_archive,
						  const char          *original_path,
						  GCancellable        *cancellable,
						  GAsyncReadyCallback  callback,
						  gpointer             user_data);

/**
 * fr_archive_paste_clipboard:
 * @files: (element-type GFile)
 */
void          fr_archive_paste_clipboard         (FrArchive           *archive,
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
		       	       	       	          gpointer             user_data);

/**
 * fr_archive_add_dropped_items:
 * @item_list: (element-type GFile)
 */
void          fr_archive_add_dropped_items       (FrArchive           *archive,
						  GList               *item_list,
						  const char          *dest_dir,
						  const char          *password,
						  gboolean             encrypt_header,
						  FrCompression        compression,
						  guint                volume_size,
						  GCancellable        *cancellable,
						  GAsyncReadyCallback  callback,
						  gpointer             user_data);

/**
 * fr_archive_update_open_files:
 * @file_list: (element-type GFile)
 * @dir_list: (element-type GFile)
 */
void          fr_archive_update_open_files       (FrArchive           *archive,
						  GList               *file_list,
						  GList               *dir_list,
						  const char          *password,
						  gboolean             encrypt_header,
						  FrCompression        compression,
						  guint                volume_size,
						  GCancellable        *cancellable,
						  GAsyncReadyCallback  callback,
						  gpointer             user_data);

/* protected */

void          fr_archive_set_multi_volume        (FrArchive           *archive,
					          GFile               *file);
void          fr_archive_change_name             (FrArchive           *archive,
						  const char          *filename);
void          fr_archive_action_started          (FrArchive           *archive,
                                                  FrAction             action);
void          fr_archive_progress                (FrArchive           *archive,
						  double               fraction);
void          fr_archive_message                 (FrArchive           *archive,
						  const char          *msg);
void          fr_archive_working_archive         (FrArchive           *archive,
						  const char          *archive_name);
void          fr_archive_progress_set_total_files(FrArchive           *archive,
						  int                  total);
int           fr_archive_progress_get_total_files(FrArchive           *archive);
int           fr_archive_progress_get_completed_files
						 (FrArchive           *archive);
double        fr_archive_progress_inc_completed_files
						 (FrArchive           *archive,
						  int                  new_completed);
void          fr_archive_progress_set_total_bytes (FrArchive           *archive,
						  gsize                total);
double        fr_archive_progress_set_completed_bytes
						 (FrArchive           *self,
						  gsize                completed_bytes);
double        fr_archive_progress_inc_completed_bytes
						 (FrArchive           *archive,
						  gsize                new_completed);
double        fr_archive_progress_get_fraction   (FrArchive           *archive);
void          fr_archive_add_file                (FrArchive           *archive,
						  FrFileData *file_data);

/* utilities */

gboolean      _g_file_is_archive                 (GFile               *file);

#endif /* FR_ARCHIVE_H */
