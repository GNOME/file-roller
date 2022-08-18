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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include "fr-file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-unsquashfs.h"


struct _FrCommandUnsquashfs
{
	FrCommand  parent_instance;
};


G_DEFINE_TYPE (FrCommandUnsquashfs, fr_command_unsquashfs, fr_command_get_type ())

typedef struct {
        FrCommand *command;
        gboolean read_header;
} UnsquashfsData;


static void
process_data_line (char     *line,
                   gpointer  data)
{
        FrFileData *fdata;
        UnsquashfsData *d = data;
        char          **fields;
        char          **tmfields;
        struct tm       tm = {0, };
        const char     *name;

        g_return_if_fail (line != NULL);

        /* Skip header */
        if (!d->read_header) {
                if (line[0] == '\0')
                        d->read_header = TRUE;
                return;
        }

        fdata = fr_file_data_new ();

        fields = _g_str_split_line (line, 5);
        fdata->size = g_ascii_strtoull (fields[2], NULL, 10);
        tmfields = g_strsplit(fields[3], "-", 3);
        if (tmfields[2]) {
                tm.tm_year = atoi (tmfields[0]) - 1900;
                tm.tm_mon = atoi (tmfields[1]) - 1;
                tm.tm_mday = atoi (tmfields[2]);
        }
        g_strfreev (tmfields);
        tmfields = g_strsplit (fields[4], ":", 2);
        if (tmfields[1]) {
                tm.tm_hour = atoi (tmfields[0]);
                tm.tm_min = atoi (tmfields[1]);
        }
        g_strfreev (tmfields);
        fdata->modified = mktime (&tm);
        g_strfreev (fields);

        name = _g_str_get_last_field (line, 6);
        fields = g_strsplit (name, " -> ", 2);

        fdata->dir = line[0] == 'd';
        name = fields[0];
        if (g_str_has_prefix (name, "squashfs-root/")) { /* Should generally be the case */
                fdata->full_path = g_strdup (name + 13);
                fdata->original_path = fdata->full_path;
        } else if (strcmp (name, "squashfs-root") == 0) {
                fdata->full_path = g_strdup ("/");
                fdata->original_path = fdata->full_path;
        } else {
                fdata->full_path = g_strdup (name);
                fdata->original_path = fdata->full_path;
        }

        if (fields[1] != NULL)
                fdata->link = g_strdup (fields[1]);
        g_strfreev (fields);

        if (fdata->dir)
                fdata->name = _g_path_get_dir_name (fdata->full_path);
        else
                fdata->name = g_strdup (_g_path_get_basename (fdata->full_path));
        fdata->path = _g_path_remove_level (fdata->full_path);

        if (*fdata->name == 0)
                fr_file_data_free (fdata);
        else
                fr_archive_add_file (FR_ARCHIVE (d->command), fdata);
}


static gboolean
fr_command_unsquashfs_list (FrCommand *command)
{
        UnsquashfsData *data;

        data = g_new0 (UnsquashfsData, 1);
        data->command = command;

        fr_process_begin_command (command->process, "unsquashfs");
        fr_process_add_arg (command->process, "-lls");
        fr_process_add_arg (command->process, command->filename);
        fr_process_set_out_line_func (command->process, process_data_line, data);
        fr_process_set_end_func (command->process, g_free, data);
        fr_process_end_command (command->process);

        return TRUE;
}


static void
fr_command_unsquashfs_extract (FrCommand  *command,
                               const char *from_file,
                               GList      *file_list,
                               const char *dest_dir,
                               gboolean    overwrite,
                               gboolean    skip_older,
                               gboolean    junk_paths)
{
        GList *scan;

        fr_process_begin_command (command->process, "unsquashfs");
        fr_process_add_arg (command->process, "-dest");
        if (dest_dir != NULL) {
                fr_process_add_arg (command->process, dest_dir);
        } else {
                fr_process_add_arg (command->process, ".");
        }
        if (overwrite) {
                fr_process_add_arg (command->process, "-force");
        }
        fr_process_add_arg (command->process, command->filename);

        for (scan = file_list; scan; scan = scan->next)
                fr_process_add_arg (command->process, scan->data);

        fr_process_end_command (command->process);
}


const char *unsquashfs_mime_type[] = { "application/vnd.squashfs",
                                       "application/vnd.snap",
                                       NULL };


static const char **
fr_command_unsquashfs_get_mime_types (FrArchive *archive)
{
        return unsquashfs_mime_type;
}


static FrArchiveCaps
fr_command_unsquashfs_get_capabilities (FrArchive  *archive,
                                        const char *mime_type,
                                        gboolean    check_command)
{
        FrArchiveCaps capabilities;

        capabilities = FR_ARCHIVE_CAN_STORE_MANY_FILES;
        if (_g_program_is_available ("unsquashfs", check_command))
                capabilities |= FR_ARCHIVE_CAN_READ;

        return capabilities;
}


static const char *
fr_command_unsquashfs_get_packages (FrArchive  *archive,
                                    const char *mime_type)
{
        return FR_PACKAGES ("unsquashfs");
}


static void
fr_command_unsquashfs_finalize (GObject *object)
{
        g_return_if_fail (object != NULL);
        g_return_if_fail (FR_IS_COMMAND_UNSQUASHFS (object));

        if (G_OBJECT_CLASS (fr_command_unsquashfs_parent_class)->finalize)
                G_OBJECT_CLASS (fr_command_unsquashfs_parent_class)->finalize (object);
}


static void
fr_command_unsquashfs_class_init (FrCommandUnsquashfsClass *klass)
{
        GObjectClass   *gobject_class;
        FrArchiveClass *archive_class;
        FrCommandClass *command_class;

        fr_command_unsquashfs_parent_class = g_type_class_peek_parent (klass);

        gobject_class = G_OBJECT_CLASS (klass);
        gobject_class->finalize = fr_command_unsquashfs_finalize;

        archive_class = FR_ARCHIVE_CLASS (klass);
        archive_class->get_mime_types   = fr_command_unsquashfs_get_mime_types;
        archive_class->get_capabilities = fr_command_unsquashfs_get_capabilities;
        archive_class->get_packages     = fr_command_unsquashfs_get_packages;

        command_class = FR_COMMAND_CLASS (klass);
        command_class->list             = fr_command_unsquashfs_list;
        command_class->extract          = fr_command_unsquashfs_extract;
}


static void
fr_command_unsquashfs_init (FrCommandUnsquashfs *self)
{
        FrArchive *base = FR_ARCHIVE (self);

        base->propAddCanUpdate             = FALSE;
        base->propAddCanReplace            = FALSE;
        base->propExtractCanAvoidOverwrite = FALSE;
        base->propExtractCanSkipOlder      = FALSE;
        base->propExtractCanJunkPaths      = FALSE;
        base->propPassword                 = FALSE;
        base->propTest                     = FALSE;
}
