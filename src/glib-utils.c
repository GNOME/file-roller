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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include "glib-utils.h"
#include "utf8-fnmatch.h"


#define MAX_PATTERNS 128


gboolean
strchrs (const char *str,
	 const char *chars)
{
	const char *c;
	for (c = chars; *c != '\0'; c++)
		if (strchr (str, *c) != NULL)
			return TRUE;
	return FALSE;
}


char *
str_substitute (const char *str,
		const char *from_str,
		const char *to_str)
{
	char    **tokens;
	int       i;
	GString  *gstr;

	if (str == NULL)
		return NULL;

	if (from_str == NULL)
		return g_strdup (str);

	if (strcmp (str, from_str) == 0)
		return g_strdup (to_str);

	tokens = g_strsplit (str, from_str, -1);

	gstr = g_string_new (NULL);
	for (i = 0; tokens[i] != NULL; i++) {
		gstr = g_string_append (gstr, tokens[i]);
		if ((to_str != NULL) && (tokens[i+1] != NULL))
			gstr = g_string_append (gstr, to_str);
	}

	return g_string_free (gstr, FALSE);
}


int
strcmp_null_tollerant (const char *s1, const char *s2)
{
	if ((s1 == NULL) && (s2 == NULL))
		return 0;
	else if ((s1 != NULL) && (s2 == NULL))
		return 1;
	else if ((s1 == NULL) && (s2 != NULL))
		return -1;
	else
		return strcmp (s1, s2);
}


/* counts how many characters to escape in @str. */
static int
count_chars_to_escape (const char *str, 
		       const char *meta_chars)
{
	int         meta_chars_n = strlen (meta_chars);
	const char *s;
	int         n = 0;

	for (s = str; *s != 0; s++) {
		int i;
		for (i = 0; i < meta_chars_n; i++) 
			if (*s == meta_chars[i]) {
				n++;
				break;
			}
	}
	return n;
}


char*
escape_str_common (const char *str, 
		   const char *meta_chars,
		   const char  prefix,
		   const char  postfix)
{
	int         meta_chars_n = strlen (meta_chars);
	char       *escaped;
	int         i, new_l, extra_chars = 0;
	const char *s;
	char       *t;

	if (str == NULL) 
		return NULL;

	if (prefix)
		extra_chars++;
	if (postfix)
		extra_chars++;

	new_l = strlen (str) + (count_chars_to_escape (str, meta_chars) * extra_chars);
	escaped = g_malloc (new_l + 1);

	s = str;
	t = escaped;
	while (*s) {
		gboolean is_bad = FALSE;
		for (i = 0; (i < meta_chars_n) && !is_bad; i++)
			is_bad = (*s == meta_chars[i]);
		if (is_bad && prefix)
			*t++ = prefix;
		*t++ = *s++;
		if (is_bad && postfix)
			*t++ = postfix;
	}
	*t = 0;

	return escaped;
}


/* escape with backslash the string @str. */
char*
escape_str (const char *str, 
	    const char *meta_chars)
{
	return escape_str_common (str, meta_chars, '\\', 0);
}


/* remove backslashes from a string. */
char*
unescape_str (const char  *str)
{
	char       *new_str;
	const char *s;
	char       *t;

	if (str == NULL)
		return NULL;

	new_str = g_malloc (strlen (str) + 1);

	s = str;
	t = new_str;
	while (*s) {
		if (*s == '\\')
			s++;
		*t++ = *s++;
	}
	*t = 0;

	return new_str;
}


/* escape with backslash the file name. */
char*
shell_escape (const char *filename)
{
	/*return escape_str (filename, "$\'`\"\\!?* ()[]&|:;");*/
	return g_shell_quote (filename);
}


static const char *
g_utf8_strstr (const char *haystack, const char *needle)
{
	const char *s;
	gsize       i;
	gsize       haystack_len = g_utf8_strlen (haystack, -1);
	gsize       needle_len = g_utf8_strlen (needle, -1);
	int         needle_size = strlen (needle);

	s = haystack;
	for (i = 0; i <= haystack_len - needle_len; i++) {
		if (strncmp (s, needle, needle_size) == 0)
			return s;
		s = g_utf8_next_char(s);
	}

	return NULL;
}


static char**
g_utf8_strsplit (const char *string,
		 const char *delimiter,
		 int         max_tokens)
{
	GSList      *string_list = NULL, *slist;
	char       **str_array;
	const char  *s;
	guint        n = 0;
	const char  *remainder;

	g_return_val_if_fail (string != NULL, NULL);
	g_return_val_if_fail (delimiter != NULL, NULL);
	g_return_val_if_fail (delimiter[0] != '\0', NULL);
	
	if (max_tokens < 1)
		max_tokens = G_MAXINT;
	
	remainder = string;
	s = g_utf8_strstr (remainder, delimiter);
	if (s != NULL) {
		gsize delimiter_size = strlen (delimiter);
		
		while (--max_tokens && (s != NULL)) {
			gsize  size = s - remainder;
			char  *new_string;

			new_string = g_new (char, size + 1);
			strncpy (new_string, remainder, size);
			new_string[size] = 0;

			string_list = g_slist_prepend (string_list, new_string);
			n++;
			remainder = s + delimiter_size;
			s = g_utf8_strstr (remainder, delimiter);
		}
	}
	if (*string) {
		n++;
		string_list = g_slist_prepend (string_list, g_strdup (remainder));
	}
	
	str_array = g_new (char*, n + 1);
	
	str_array[n--] = NULL;
	for (slist = string_list; slist; slist = slist->next)
		str_array[n--] = slist->data;
	
	g_slist_free (string_list);
	
	return str_array;
}


static char*
g_utf8_strchug (char *string)
{
	char     *scan;
	gunichar  c;

	g_return_val_if_fail (string != NULL, NULL);
	
	scan = string;
	c = g_utf8_get_char (scan);
	while (g_unichar_isspace (c)) {
		scan = g_utf8_next_char (scan);
		c = g_utf8_get_char (scan);
	}

	g_memmove (string, scan, strlen (scan) + 1);
	
	return string;
}


static char*
g_utf8_strchomp (char *string)
{
	char   *scan;
	gsize   len;
 
	g_return_val_if_fail (string != NULL, NULL);
	
	len = g_utf8_strlen (string, -1);

	if (len == 0)
		return string;

	scan = g_utf8_offset_to_pointer (string, len - 1);

	while (len--) {
		gunichar c = g_utf8_get_char (scan);
		if (g_unichar_isspace (c))
			*scan = '\0';
		else
			break;
		scan = g_utf8_find_prev_char (string, scan);
	}
	
	return string;
}


gboolean
match_patterns (char       **patterns, 
		const char  *string,
		int          flags)
{
	int i;
	int result;
       
	if (patterns[0] == NULL)
		return TRUE;
	
	if (string == NULL)
		return FALSE;
	
	result = FNM_NOMATCH;
	i = 0;
	while ((result != 0) && (patterns[i] != NULL)) {
		result = g_utf8_fnmatch (patterns[i], string, flags);
		i++;
	}

	return (result == 0);
}


#define g_utf8_strstrip(string)    g_utf8_strchomp (g_utf8_strchug (string))


char **
search_util_get_patterns (const char *pattern_string)
{
	char **patterns;
	int    i;
	
	patterns = g_utf8_strsplit (pattern_string, ";", MAX_PATTERNS);
	for (i = 0; patterns[i] != NULL; i++) 
		patterns[i] = g_utf8_strstrip (patterns[i]);
	
	return patterns;
}


char *
_g_strdup_with_max_size (const char *s,
			 int         max_size)
{
	char *result;
	int   l = strlen (s);

	if (l > max_size) {
		char *first_half;
		char *second_half;
		int   offset;
		int   half_max_size = max_size / 2 + 1;

		first_half = g_strndup (s, half_max_size);
		offset = half_max_size + l - max_size;
		second_half = g_strndup (s + offset, half_max_size);

		result = g_strconcat (first_half, "...", second_half, NULL);

		g_free (first_half);
		g_free (second_half);
	} else
		result = g_strdup (s);

	return result;
}


const char *
eat_spaces (const char *line)
{
	if (line == NULL)
		return NULL;
	while ((*line == ' ') && (*line != 0))
		line++;
	return line;
}


char **
split_line (const char *line, 
	    int         n_fields)
{
	char       **fields;
	const char  *scan, *field_end;
	int          i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields] = NULL;

	scan = eat_spaces (line);
	for (i = 0; i < n_fields; i++) {
		if (scan == NULL) {
			fields[i] = NULL;
			continue;
		}
		field_end = strchr (scan, ' ');
		if (field_end != NULL) {
			fields[i] = g_strndup (scan, field_end - scan);
			scan = eat_spaces (field_end);
		}
	}

	return fields;
}


const char *
get_last_field (const char *line,
		int         last_field)
{
	const char *field;
	int         i;

	if (line == NULL)
		return NULL;

	last_field--;
	field = eat_spaces (line);
	for (i = 0; i < last_field; i++) {
		if (field == NULL)
			return NULL;
		field = strchr (field, ' ');
		field = eat_spaces (field);
	}

	return field;
}


void
debug (const char *file,
       int         line,
       const char *function,
       const char *format, ...)
{
#ifdef DEBUG
	va_list  args;
	char    *str;

	g_return_if_fail (format != NULL);
	
	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_fprintf (stderr, "[FR] %s:%d (%s):\n\t%s\n", file, line, function, str);

	g_free (str);
#else /* ! DEBUG */
#endif
}
