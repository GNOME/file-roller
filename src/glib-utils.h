/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2005 Free Software Foundation, Inc.
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

#ifndef _GLIB_UTILS_H
#define _GLIB_UTILS_H

#include <time.h>
#include <glib.h>
#include <gio/gio.h>

#ifndef __GNUC__
#define __FUNCTION__ ""
#endif

#define DEBUG_INFO __FILE__, __LINE__, __FUNCTION__
#define MIME_TYPE_DIRECTORY "folder"
#define MIME_TYPE_ARCHIVE "application/x-archive"
#define DEF_ACTION_CALLBACK(x) void x (GSimpleAction *action, GVariant *parameter, gpointer user_data);

#define get_home_relative_path(x)        \
	g_strconcat (g_get_home_dir (), \
		     "/",               \
		     (x),               \
		     NULL)

#define g_signal_handlers_disconnect_by_data(instance, data) \
    g_signal_handlers_disconnect_matched ((instance), G_SIGNAL_MATCH_DATA, \
					  0, 0, NULL, NULL, (data))

/* gobject */

gpointer            _g_object_ref                  (gpointer             object);
void                _g_object_unref                (gpointer             object);
void                _g_clear_object                (gpointer             p);
GList *             _g_object_list_ref             (GList               *list);
void                _g_object_list_unref           (GList               *list);
void                _g_object_unref_on_weak_notify (gpointer             data,
						    GObject             *where_the_object_was);
/* enum */

GEnumValue *        _g_enum_type_get_value         (GType                enum_type,
						    int                  value);
GEnumValue *        _g_enum_type_get_value_by_nick (GType                enum_type,
						    const char          *nick);

/* error */

void                _g_error_free                  (GError              *error);

/* string */

gboolean            _g_strchrs                     (const char          *str,
						    const char          *chars);
char *              _g_str_substitute              (const char          *str,
						    const char          *from_str,
						    const char          *to_str);
gboolean            _g_str_equal                   (const char          *s1,
						    const char          *s2);
char*               _g_str_escape                  (const char          *str,
						    const char          *meta_chars);
char *              _g_str_shell_escape            (const char          *filename);
char *              _g_strdup_with_max_size        (const char          *s,
						    int                  max_size);
const char *        _g_str_eat_spaces              (const char          *line);
const char *        _g_str_eat_void_chars          (const char          *line);
char **             _g_str_split_line              (const char          *line,
						    int                  n_fields);
const char *        _g_str_get_last_field          (const char          *line,
						    int                  last_field);
const char *        _g_str_get_static              (const char          *s);

/* utf8 */

gboolean            _g_utf8_all_spaces             (const char          *text);

/* string vector */

char **             _g_strv_prepend                (char               **str_array,
						    const char          *str);
gboolean            _g_strv_remove                 (char               **str_array,
		  	  	  	  	    const char          *str);

/* string list */

void                _g_string_list_free            (GList               *path_list);
GList *             _g_string_list_dup             (GList               *path_list);

/* GRegex */

gboolean            _g_regexp_matchv               (GRegex             **regexps,
						    const char          *string,
						    GRegexMatchFlags     match_options);
void                _g_regexp_freev                (GRegex             **regexps);
char **             _g_regexp_get_patternv         (const char          *pattern_string);
GRegex **           _g_regexp_split_from_patterns  (const char          *pattern_string,
			                            GRegexCompileFlags   compile_options);

/* uri/path/filename */

const char *        _g_uri_get_home                (void);
char *              _g_uri_get_home_relative       (const char          *partial_uri);
const char *        _g_uri_remove_host             (const char          *uri);
char *              _g_uri_get_host                (const char          *uri);
char *              _g_uri_get_root                (const char          *uri);
int                 _g_uri_cmp                     (const char          *uri1,
						    const char          *uri2);
const char *        _g_path_get_basename           (const char          *path);
char *              _g_path_get_dir_name           (const char          *path);
char *              _g_path_remove_level           (const char          *path);
char *              _g_path_remove_ending_separator(const char          *path);
char *              _g_path_remove_extension       (const char          *path);
char *		    _g_path_remove_first_extension (const gchar		*path);
gboolean            _g_path_is_parent_of           (const char          *dirname,
						    const char          *filename);
const char *        _g_path_get_relative_basename  (const char          *path,
						    const char          *base_dir,
						    gboolean             junk_paths);
const char *        _g_path_get_relative_basename_safe
						   (const char          *path,
						    const char          *base_dir,
						    gboolean             junk_paths);
gboolean            _g_filename_is_hidden          (const char          *name);
const char *        _g_filename_get_extension      (const char          *filename);
gboolean            _g_filename_has_extension      (const char          *filename,
						    const char          *ext);
char *              _g_filename_get_random         (int                  random_part_len,
						    const char          *suffix);
gboolean            _g_mime_type_matches           (const char          *type,
						    const char          *pattern);
const char *        _g_mime_type_get_from_content  (char                *buffer,
		  	  	  	  	    gsize                buffer_size);
gboolean            _g_basename_is_valid           (const char          *new_name,
						    const char          *old_name,
						    char               **reason);

/* GFile */

int                 _g_file_cmp_uris               (GFile               *a,
						    GFile               *b);
gboolean            _g_file_is_local               (GFile               *file);
GFile *             _g_file_get_home               (void);
char *              _g_file_get_display_basename   (GFile               *file);
GFile *             _g_file_new_home_relative      (const char          *partial_uri);
GList *             _g_file_list_dup               (GList               *l);
void                _g_file_list_free              (GList               *l);
GList *             _g_file_list_new_from_uri_list (GList               *uris);
GFile *             _g_file_append_path            (GFile               *file,
                                                    ...);

/* GKeyFile */

GList *             _g_key_file_get_string_list    (GKeyFile            *key_file,
						    const char          *group_name,
						    const char          *key,
						    GError             **error);

/* GSettings utils */

GSettings *         _g_settings_new_if_schema_installed
						   (const char          *schema_id);

/* functions used to parse a command output lines. */

gboolean            _g_line_matches_pattern        (const char          *line,
						    const char          *pattern);
int                 _g_line_get_index_from_pattern (const char          *line,
						    const char          *pattern);
char*               _g_line_get_next_field         (const char          *line,
						    int                  start_from,
						    int                  field_n);
char*               _g_line_get_prev_field         (const char          *line,
						    int                  start_from,
						    int                  field_n);

/* threading */

gchar * 	   fr_get_thread_count 		   (void);

/* debug */

void                debug                          (const char          *file,
						    int                  line,
						    const char          *function,
						    const char          *format,
						    ...);

#endif /* _GLIB_UTILS_H */
