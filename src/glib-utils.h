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

#define g_signal_handlers_disconnect_by_data(instance, data) \
    g_signal_handlers_disconnect_matched ((instance), G_SIGNAL_MATCH_DATA, \
					  0, 0, NULL, NULL, (data))

#ifndef __GNUC__
#define __FUNCTION__ ""
#endif

#define DEBUG_INFO __FILE__, __LINE__, __FUNCTION__

/* gobject */

gpointer            _g_object_ref                 (gpointer             object);
void                _g_object_unref               (gpointer             object);

/* string */

gboolean            _g_strchrs                    (const char          *str,
						   const char          *chars);
char *              _g_str_substitute             (const char          *str,
						   const char          *from_str,
						   const char          *to_str);
int                 _g_strcmp_null_tolerant       (const char          *s1,
						   const char          *s2);
char*               _g_str_escape_full            (const char          *str,
						   const char          *meta_chars,
						   const char           prefix,
						   const char           postfix);
char*               _g_str_escape                 (const char          *str,
						   const char          *meta_chars);
char *              _g_str_shell_escape           (const char          *filename);
char *              _g_strdup_with_max_size       (const char          *s,
						   int                  max_size);
const char *        _g_str_eat_spaces             (const char          *line);
const char *        _g_str_eat_void_chars         (const char          *line);
char **             _g_str_split_line             (const char          *line,
						   int                  n_fields);
const char *        _g_str_get_last_field         (const char          *line,
						   int                  last_field);
const char *        _g_str_get_static             (const char          *s);

/* string vector */

char **             _g_strv_prepend               (char               **str_array,
						   const char          *str);
gboolean            _g_strv_remove                (char               **str_array,
		  	  	  	  	   const char          *str);

/* string list */

void                _g_string_list_free           (GList               *path_list);
GList *             _g_string_list_dup            (GList               *path_list);

/* GPtrArray */

GPtrArray *         _g_ptr_array_copy             (GPtrArray           *array);
void                _g_ptr_array_free_full        (GPtrArray           *array,
                       				   GFunc                func,
                       				   gpointer             user_data);
void                _g_ptr_array_reverse          (GPtrArray           *array);
int                 _g_ptr_array_binary_search    (GPtrArray           *array,
						   gpointer             value,
						   GCompareFunc         func);

/* GRegex */

gboolean            _g_regexp_matchv              (GRegex             **regexps,
						   const char          *string,
						   GRegexMatchFlags     match_options);
void                _g_regexp_freev               (GRegex             **regexps);
char **             _g_regexp_get_patternv        (const char          *pattern_string);
GRegex **           _g_regexp_split_from_patterns (const char          *pattern_string,
			                           GRegexCompileFlags   compile_options);

/* time */

char *              _g_time_to_string             (time_t               time);

/* uri */

char *              _g_uri_display_basename       (const char          *uri);

/* debug */

void                debug                         (const char          *file,
						   int                  line,
						   const char          *function,
						   const char          *format,
						   ...);

#endif /* _GLIB_UTILS_H */
