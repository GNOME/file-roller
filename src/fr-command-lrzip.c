/*
 * fr-command-lrzip.c
 *
 *  Created on: 10.04.2010
 *      Author: Alexander Saprykin
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-lrzip.h"


struct _FrCommandLrzip
{
	FrCommand  parent_instance;
};


G_DEFINE_TYPE (FrCommandLrzip, fr_command_lrzip, fr_command_get_type ())


/* -- list -- */


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FrFileData *fdata;
	FrCommand *comm = FR_COMMAND (data);

	g_return_if_fail (line != NULL);

	if (strlen (line) == 0)
		return;

	if (! g_str_has_prefix (line, "Decompressed file size:"))
		return;

	fdata = fr_file_data_new ();
	fdata->size = g_ascii_strtoull (_g_str_get_last_field (line, 4), NULL, 10);

	struct stat st;
	if (stat (comm->filename, &st) == 0)
#ifdef __APPLE__
		fdata->modified = st.st_mtime;
#else
		fdata->modified = st.st_mtim.tv_sec;
#endif
	else
		time(&(fdata->modified));

	fdata->encrypted = FALSE;

	char *new_fname = g_strdup (_g_path_get_basename (comm->filename));
	if (g_str_has_suffix (new_fname, ".lrz"))
		new_fname[strlen (new_fname) - 4] = '\0';

	if (*new_fname == '/') {
		fdata->full_path = g_strdup (new_fname);
		fdata->original_path = fdata->full_path;
	}
	else {
		fdata->full_path = g_strconcat ("/", new_fname, NULL);
		fdata->original_path = fdata->full_path + 1;
	}
	fdata->path = _g_path_remove_level (fdata->full_path);
	fdata->name = new_fname;
	fdata->dir = FALSE;
	fdata->link = NULL;

	if (*fdata->name == '\0')
		fr_file_data_free (fdata);
	else
		fr_archive_add_file (FR_ARCHIVE (comm), fdata);
}


static gboolean
fr_command_lrzip_list (FrCommand  *comm)
{
	fr_process_set_err_line_func (comm->process, list__process_line, comm);

	fr_process_begin_command (comm->process, "lrzip");
	fr_process_add_arg (comm->process, "-i");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);

	return TRUE;
}


static void
fr_command_lrzip_add (FrCommand  *comm,
		      const char *from_file,
		      GList      *file_list,
		      const char *base_dir,
		      gboolean    update,
		      gboolean    follow_links)
{
	fr_process_begin_command (comm->process, "lrzip");

	if (base_dir != NULL)
		fr_process_set_working_dir (comm->process, base_dir);

	/* preserve links. */

	switch (FR_ARCHIVE (comm)->compression) {
	case FR_COMPRESSION_VERY_FAST:
		fr_process_add_arg (comm->process, "-l"); break;
	case FR_COMPRESSION_FAST:
		fr_process_add_arg (comm->process, "-g"); break;
	case FR_COMPRESSION_NORMAL:
		fr_process_add_arg (comm->process, "-b"); break;
	case FR_COMPRESSION_MAXIMUM:
		fr_process_add_arg (comm->process, "-z"); break;
	}

	fr_process_add_arg (comm->process, "-o");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, (char *) file_list->data);

	fr_process_end_command (comm->process);
}

static void
fr_command_lrzip_extract (FrCommand  *comm,
			  const char *from_file,
			  GList      *file_list,
			  const char *dest_dir,
			  gboolean    overwrite,
			  gboolean    skip_older,
			  gboolean    junk_paths)
{
	fr_process_begin_command (comm->process, "lrzip");
	fr_process_add_arg (comm->process, "-d");

	if (dest_dir != NULL) {
		fr_process_add_arg (comm->process, "-O");
		fr_process_add_arg (comm->process, dest_dir);
	}
	if (overwrite)
		fr_process_add_arg (comm->process, "-f");

	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
}


/*
static void
fr_command_lrzip_test (FrCommand   *comm)
{
	fr_process_begin_command (comm->process, "lrzip");
	fr_process_add_arg (comm->process, "-t");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_end_command (comm->process);
}
*/


const char *lrzip_mime_type[] = { "application/x-lrzip", NULL };


static const char **
fr_command_lrzip_get_mime_types (FrArchive *archive)
{
	return lrzip_mime_type;
}


static FrArchiveCaps
fr_command_lrzip_get_capabilities (FrArchive  *archive,
				   const char *mime_type,
				   gboolean    check_command)
{
	FrArchiveCaps capabilities = FR_ARCHIVE_CAN_DO_NOTHING;

	if (_g_program_is_available ("lrzip", check_command))
		capabilities |= FR_ARCHIVE_CAN_READ_WRITE;

	return capabilities;
}


static const char *
fr_command_lrzip_get_packages (FrArchive  *archive,
			       const char *mime_type)
{
	return FR_PACKAGES ("lrzip");
}


static void
fr_command_lrzip_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (FR_IS_COMMAND_LRZIP (object));

	if (G_OBJECT_CLASS (fr_command_lrzip_parent_class)->finalize)
		G_OBJECT_CLASS (fr_command_lrzip_parent_class)->finalize (object);
}


static void
fr_command_lrzip_class_init (FrCommandLrzipClass *klass)
{
	GObjectClass   *gobject_class;
	FrArchiveClass *archive_class;
	FrCommandClass *command_class;

	fr_command_lrzip_parent_class = g_type_class_peek_parent (klass);

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = fr_command_lrzip_finalize;

	archive_class = FR_ARCHIVE_CLASS (klass);
	archive_class->get_mime_types   = fr_command_lrzip_get_mime_types;
	archive_class->get_capabilities = fr_command_lrzip_get_capabilities;
	archive_class->get_packages     = fr_command_lrzip_get_packages;

	command_class = FR_COMMAND_CLASS (klass);
	command_class->list             = fr_command_lrzip_list;
	command_class->add              = fr_command_lrzip_add;
	command_class->extract          = fr_command_lrzip_extract;
}


static void
fr_command_lrzip_init (FrCommandLrzip *self)
{
	FrArchive *base = FR_ARCHIVE (self);

	base->propAddCanUpdate             = FALSE;
	base->propAddCanReplace            = FALSE;
	base->propAddCanStoreFolders       = FALSE;
	base->propAddCanStoreLinks         = FALSE;
	base->propExtractCanAvoidOverwrite = TRUE;
	base->propExtractCanSkipOlder      = FALSE;
	base->propExtractCanJunkPaths      = FALSE;
	base->propPassword                 = FALSE;
	base->propTest                     = FALSE;
}
