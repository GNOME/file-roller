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

#include <config.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib-object.h>
#include "glib-utils.h"


#define MAX_PATTERNS 128


/* gobject utils*/


gpointer
_g_object_ref (gpointer object)
{
	return (object != NULL) ? g_object_ref (object) : NULL;
}


void
_g_object_unref (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}


void
_g_clear_object (gpointer p)
{
	g_clear_object ((GObject **) p);
}


GList *
_g_object_list_ref (GList *list)
{
	GList *new_list;

	if (list == NULL)
		return NULL;

	new_list = g_list_copy (list);
	g_list_foreach (new_list, (GFunc) g_object_ref, NULL);

	return new_list;
}


void
_g_object_list_unref (GList *list)
{
	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}


void
_g_object_unref_on_weak_notify (gpointer  data,
				GObject  *where_the_object_was)
{
	g_object_unref (data);
}


/* enum */


GEnumValue *
_g_enum_type_get_value (GType enum_type,
			int   value)
{
	GEnumClass *class;
	GEnumValue *enum_value;

	class = G_ENUM_CLASS (g_type_class_ref (enum_type));
	enum_value = g_enum_get_value (class, value);
	g_type_class_unref (class);

	return enum_value;
}


GEnumValue *
_g_enum_type_get_value_by_nick (GType       enum_type,
				const char *nick)
{
	GEnumClass *class;
	GEnumValue *enum_value;

	class = G_ENUM_CLASS (g_type_class_ref (enum_type));
	enum_value = g_enum_get_value_by_nick (class, nick);
	g_type_class_unref (class);

	return enum_value;
}


/* error */


void
_g_error_free (GError *error)
{
	if (error != NULL)
		g_error_free (error);
}


/* string */


gboolean
_g_strchrs (const char *str,
	    const char *chars)
{
	const char *c;
	for (c = chars; *c != '\0'; c++)
		if (strchr (str, *c) != NULL)
			return TRUE;
	return FALSE;
}


char *
_g_str_substitute (const char *str,
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


gboolean
_g_str_equal (const char *s1,
	      const char *s2)
{
	return g_strcmp0 (s1, s2) == 0;
}


/* -- _g_str_escape_full -- */


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


static char *
_g_str_escape_full (const char *str,
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
char *
_g_str_escape (const char *str,
	       const char *meta_chars)
{
	return _g_str_escape_full (str, meta_chars, '\\', 0);
}


/* escape with backslash the file name. */
char *
_g_str_shell_escape (const char *filename)
{
	return _g_str_escape (filename, "$'`\"\\!?* ()[]&|:;<>#");
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
_g_str_eat_spaces (const char *line)
{
	if (line == NULL)
		return NULL;
	while ((*line == ' ') && (*line != 0))
		line++;
	return line;
}


const char *
_g_str_eat_void_chars (const char *line)
{
	if (line == NULL)
		return NULL;
	while (((*line == ' ') || (*line == '\t')) && (*line != 0))
		line++;
	return line;
}


char **
_g_str_split_line (const char *line,
		   int         n_fields)
{
	char       **fields;
	const char  *scan, *field_end;
	int          i;

	fields = g_new0 (char *, n_fields + 1);
	fields[n_fields] = NULL;

	scan = _g_str_eat_spaces (line);
	for (i = 0; i < n_fields; i++) {
		if (scan == NULL) {
			fields[i] = NULL;
			continue;
		}
		field_end = strchr (scan, ' ');
		if (field_end != NULL) {
			fields[i] = g_strndup (scan, field_end - scan);
			scan = _g_str_eat_spaces (field_end);
		}
	}

	return fields;
}


const char *
_g_str_get_last_field (const char *line,
		       int         last_field)
{
	const char *field;
	int         i;

	if (line == NULL)
		return NULL;

	last_field--;
	field = _g_str_eat_spaces (line);
	for (i = 0; i < last_field; i++) {
		if (field == NULL)
			return NULL;
		field = strchr (field, ' ');
		field = _g_str_eat_spaces (field);
	}

	return field;
}


GHashTable *static_strings = NULL;


const char *
_g_str_get_static (const char *s)
{
        const char *result;

        if (s == NULL)
                return NULL;

        if (static_strings == NULL)
                static_strings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        if (! g_hash_table_lookup_extended (static_strings, s, (gpointer*) &result, NULL)) {
                result = g_strdup (s);
                g_hash_table_insert (static_strings,
                                     (gpointer) result,
                                     GINT_TO_POINTER (1));
        }

        return result;
}


/* utf8 */


gboolean
_g_utf8_all_spaces (const char *text)
{
	const char *scan;

	if (text == NULL)
		return TRUE;

	for (scan = text; *scan != 0; scan = g_utf8_next_char (scan)) {
		gunichar c = g_utf8_get_char (scan);
		if (! g_unichar_isspace (c))
			return FALSE;
	}

	return TRUE;
}


/* string vector */


char **
_g_strv_prepend (char       **str_array,
		 const char  *str)
{
	char **result;
	int    i;
	int    j;

	result = g_new (char *, g_strv_length (str_array) + 1);
	i = 0;
	result[i++] = g_strdup (str);
	for (j = 0; str_array[j] != NULL; j++)
		result[i++] = g_strdup (str_array[j]);
	result[i] = NULL;

	return result;
}


gboolean
_g_strv_remove (char       **str_array,
		const char  *str)
{
	int i;
	int j;

	if (str == NULL)
		return FALSE;

	for (i = 0; str_array[i] != NULL; i++)
		if (strcmp (str_array[i], str) == 0)
			break;

	if (str_array[i] == NULL)
		return FALSE;

	for (j = i; str_array[j] != NULL; j++)
		str_array[j] = str_array[j + 1];

	return TRUE;
}


/* string list */


void
_g_string_list_free (GList *path_list)
{
	if (path_list == NULL)
		return;
	g_list_free_full (path_list, g_free);
}


GList *
_g_string_list_dup (GList *path_list)
{
	GList *new_list = NULL;
	GList *scan;

	for (scan = path_list; scan; scan = scan->next)
		new_list = g_list_prepend (new_list, g_strdup (scan->data));

	return g_list_reverse (new_list);
}


/* GRegex */


gboolean
_g_regexp_matchv (GRegex           **regexps,
	          const char        *string,
	          GRegexMatchFlags   match_options)
{
	gboolean matched;
	int      i;

	if ((regexps == NULL) || (regexps[0] == NULL))
		return TRUE;

	if (string == NULL)
		return FALSE;

	matched = FALSE;
	for (i = 0; regexps[i] != NULL; i++)
		if (g_regex_match (regexps[i], string, match_options, NULL)) {
			matched = TRUE;
			break;
		}

	return matched;
}


void
_g_regexp_freev (GRegex **regexps)
{
	int i;

	if (regexps == NULL)
		return;

	for (i = 0; regexps[i] != NULL; i++)
		g_regex_unref (regexps[i]);
	g_free (regexps);
}


/* -- _g_regexp_get_patternv -- */


static const char *
_g_utf8_strstr (const char *haystack,
		const char *needle)
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


static char **
_g_utf8_strsplit (const char *string,
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
	s = _g_utf8_strstr (remainder, delimiter);
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
			s = _g_utf8_strstr (remainder, delimiter);
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

	memmove (string, scan, strlen (scan) + 1);

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


#define g_utf8_strstrip(string) g_utf8_strchomp (g_utf8_strchug (string))


char **
_g_regexp_get_patternv (const char *pattern_string)
{
	char **patterns;
	int    i;

	if (pattern_string == NULL)
		return NULL;

	patterns = _g_utf8_strsplit (pattern_string, ";", MAX_PATTERNS);
	for (i = 0; patterns[i] != NULL; i++) {
		char *p1, *p2;

		p1 = g_utf8_strstrip (patterns[i]);
		p2 = _g_str_substitute (p1, ".", "\\.");
		patterns[i] = _g_str_substitute (p2, "*", ".*");

		g_free (p2);
		g_free (p1);
	}

	return patterns;
}


GRegex **
_g_regexp_split_from_patterns (const char         *pattern_string,
			       GRegexCompileFlags  compile_options)
{
	char   **patterns;
	GRegex **regexps;
	int      i;

	patterns = _g_regexp_get_patternv (pattern_string);
	if (patterns == NULL)
		return NULL;

	regexps = g_new0 (GRegex*, g_strv_length (patterns) + 1);
	for (i = 0; patterns[i] != NULL; i++)
		regexps[i] = g_regex_new (patterns[i],
					  G_REGEX_OPTIMIZE | compile_options,
					  G_REGEX_MATCH_NOTEMPTY,
					  NULL);
	g_strfreev (patterns);

	return regexps;
}


/* uri/path/filename */


const char *
_g_uri_get_home (void)
{
	static char *home_uri = NULL;
	if (home_uri == NULL)
		home_uri = g_filename_to_uri (g_get_home_dir (), NULL, NULL);
	return home_uri;
}


char *
_g_uri_get_home_relative (const char *partial_uri)
{
	return g_strconcat (_g_uri_get_home (),
			    "/",
			    partial_uri,
			    NULL);
}


const char *
_g_uri_remove_host (const char *uri)
{
        const char *idx, *sep;

        if (uri == NULL)
                return NULL;

        idx = strstr (uri, "://");
        if (idx == NULL)
                return uri;
        idx += 3;
        if (*idx == '\0')
                return "/";
        sep = strstr (idx, "/");
        if (sep == NULL)
                return idx;
        return sep;
}


char *
_g_uri_get_host (const char *uri)
{
	const char *idx;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return NULL;
	idx = strstr (idx + 3, "/");
	if (idx == NULL)
		return NULL;
	return g_strndup (uri, (idx - uri));
}


char *
_g_uri_get_root (const char *uri)
{
	char *host;
	char *root;

	host = _g_uri_get_host (uri);
	if (host == NULL)
		return NULL;
	root = g_strconcat (host, "/", NULL);
	g_free (host);

	return root;
}


int
_g_uri_cmp (const char *uri1,
	    const char *uri2)
{
	return g_strcmp0 (uri1, uri2);
}


/* like g_path_get_basename but does not warn about NULL and does not
 * alloc a new string. */
const gchar *
_g_path_get_basename (const gchar *file_name)
{
	register char   *base;
	register gssize  last_char;

	if (file_name == NULL)
		return NULL;

	if (file_name[0] == '\0')
		return "";

	last_char = strlen (file_name) - 1;

	if (file_name [last_char] == G_DIR_SEPARATOR)
		return "";

	base = g_utf8_strrchr (file_name, -1, G_DIR_SEPARATOR);
	if (! base)
		return file_name;

	return base + 1;
}


char *
_g_path_get_dir_name (const gchar *path)
{
	register gssize base;
	register gssize last_char;

	if (path == NULL)
		return NULL;

	if (path[0] == '\0')
		return g_strdup ("");

	last_char = strlen (path) - 1;
	if (path[last_char] == G_DIR_SEPARATOR)
		last_char--;

	base = last_char;
	while ((base >= 0) && (path[base] != G_DIR_SEPARATOR))
		base--;

	return g_strndup (path + base + 1, last_char - base);
}


gchar *
_g_path_remove_level (const gchar *path)
{
	int         p;
	const char *ptr = path;
	char       *new_path;

	if (path == NULL)
		return NULL;

	p = strlen (path) - 1;
	if (p < 0)
		return NULL;

	/* ignore the first slash if it's the last character,
	 * this way /a/b/ is treated as /a/b */

	if ((ptr[p] == '/') && (p > 0))
		p--;

	while ((p > 0) && (ptr[p] != '/'))
		p--;
	if ((p == 0) && (ptr[p] == '/'))
		p++;
	new_path = g_strndup (path, (guint)p);

	return new_path;
}


char *
_g_path_remove_ending_separator (const char *path)
{
	gint len, copy_len;

	if (path == NULL)
		return NULL;

	copy_len = len = strlen (path);
	if ((len > 1) && (path[len - 1] == '/'))
		copy_len--;

	return g_strndup (path, copy_len);
}


char *
_g_path_remove_extension (const gchar *path)
{
	const char *ext;

	if (path == NULL)
		return NULL;

	ext = _g_filename_get_extension (path);
	if (ext == NULL)
		return g_strdup (path);
	else
		return g_strndup (path, strlen (path) - strlen (ext));
}


char *
_g_path_remove_first_extension (const gchar *path)
{
	const char *ext;

	if (path == NULL)
		return NULL;

	ext = strrchr (path, '.');
	if (ext == NULL)
		return g_strdup (path);
	else
		return g_strndup (path, strlen (path) - strlen (ext));
}


/* Check whether the dirname is contained in filename */
gboolean
_g_path_is_parent_of (const char *dirname,
		      const char *filename)
{
	int dirname_l, filename_l, separator_position;

	if ((dirname == NULL) || (filename == NULL))
		return FALSE;

	dirname_l = strlen (dirname);
	filename_l = strlen (filename);

	if ((dirname_l == filename_l + 1)
	     && (dirname[dirname_l - 1] == '/'))
		return FALSE;

	if ((filename_l == dirname_l + 1)
	     && (filename[filename_l - 1] == '/'))
		return FALSE;

	if (dirname[dirname_l - 1] == '/')
		separator_position = dirname_l - 1;
	else
		separator_position = dirname_l;

	return ((filename_l > dirname_l)
		&& (strncmp (dirname, filename, dirname_l) == 0)
		&& (filename[separator_position] == '/'));
}


const char *
_g_path_get_relative_basename (const char *path,
			       const char *base_dir,
			       gboolean    junk_paths)
{
	size_t      base_dir_len;
	const char *base_path;

	if (junk_paths)
		return _g_path_get_basename (path);

	if (base_dir == NULL)
		return (path[0] == '/') ? path + 1 : path;

	base_dir_len = strlen (base_dir);
	if (strlen (path) < base_dir_len)
		return NULL;

	base_path = path + base_dir_len;
	if (path[0] != '/')
		base_path -= 1;

	return base_path;
}


#define ISDOT(c) ((c) == '.')
#define ISSLASH(c) ((c) == '/')


static const char *
sanitize_filename (const char *file_name)
{
	size_t      prefix_len;
	char const *p;

	if (file_name == NULL)
		return NULL;

	prefix_len = 0;
	for (p = file_name; *p; ) {
		if (ISDOT (p[0]) && ISDOT (p[1]) && (ISSLASH (p[2]) || !p[2]))
			return NULL;

		do {
			char c = *p++;
			if (ISSLASH (c))
				break;
		}
		while (*p);
	}

	p = file_name + prefix_len;
	while (ISSLASH (*p))
		p++;

	return p;
}


const char *
_g_path_get_relative_basename_safe (const char *path,
				    const char *base_dir,
				    gboolean    junk_paths)
{
	return sanitize_filename (_g_path_get_relative_basename (path, base_dir, junk_paths));
}


gboolean
_g_filename_is_hidden (const gchar *name)
{
	if (name[0] != '.') return FALSE;
	if (name[1] == '\0') return FALSE;
	if ((name[1] == '.') && (name[2] == '\0')) return FALSE;

	return TRUE;
}


const char *
_g_filename_get_extension (const char *filename)
{
	const char *ptr = filename;
	int         len;
	int         p;
	const char *ext;

	if (filename == NULL)
		return NULL;

	len = strlen (filename);
	if (len <= 1)
		return NULL;

	p = len - 1;
	while ((p >= 0) && (ptr[p] != '.'))
		p--;
	if ((p < 0) || (ptr[p] != '.'))
		return NULL;

	ext = filename + p;
	if (ext - 4 >= filename) {
		const char *test = ext - 4;
		/* .tar.rz cannot be uncompressed in one step */
		if ((strncmp (test, ".tar", 4) == 0) && (strncmp (ext, ".rz", 2) != 0))
			ext = ext - 4;
	}
	return ext;
}


gboolean
_g_filename_has_extension (const char *filename,
		   const char *ext)
{
	size_t filename_l, ext_l;

	filename_l = strlen (filename);
	ext_l = strlen (ext);

	if (filename_l < ext_l)
		return FALSE;
	return strcasecmp (filename + filename_l - ext_l, ext) == 0;
}


char *
_g_filename_get_random (int         random_part_len,
		        const char *suffix)
{
	const char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	const int   n_letters = strlen (letters);
	int         suffix_len;
	char       *result, *c;
	GRand      *rand;
	int         i;

	suffix_len = suffix != NULL ? strlen (suffix) + 1 : 0;
	result = g_new (char, suffix_len + random_part_len + 1);

	rand = g_rand_new ();
	for (i = 0, c = result; i < random_part_len; i++, c++)
		*c = letters[g_rand_int_range (rand, 0, n_letters)];
	*c = '\0';
	g_rand_free (rand);

	if (suffix != NULL) {
		strcpy (c, ".");
		strcpy (c + 1, suffix);
	}

	return result;
}


gboolean
_g_mime_type_matches (const char *mime_type,
		      const char *pattern)
{
	return (strcasecmp (mime_type, pattern) == 0);
}


const char *
_g_mime_type_get_from_content (char  *buffer,
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
		{ 0,  4, "Rar!",                                 "application/vnd.rar"         },
		{ 0,  4, "RZIP",                                 "application/x-rzip"          },
		{ 0,  6, "\3757zXZ\000",                         "application/x-xz"            },
		{ 20, 4, "\xdc\xa7\xc4\xfd",                     "application/x-zoo",          },
		{ 0,  4, "PK\003\004",                           "application/zip"             },
		{ 0,  8, "PK00PK\003\004",                       "application/zip"             },
		{ 0,  4, "LRZI",                                 "application/x-lrzip"         },
		{ 0,  4, "\x28\xB5\x2F\xFD",                     "application/zstd"            },
	};

	for (size_t i = 0; i < G_N_ELEMENTS (magic_ids); i++) {
		const struct magic * const magic = &magic_ids[i];

		if ((magic->off + magic->len) > buffer_size)
			g_warning ("buffer underrun for mime-type '%s' magic", magic->mime_type);
		else if (! memcmp (buffer + magic->off, magic->id, magic->len))
			return magic->mime_type;
	}

	return NULL;
}


#define BAD_CHARS "/\\*"


gboolean
_g_basename_is_valid (const char          *new_name,
		      const char          *old_name,
		      char               **reason)
{
	char     *utf8_new_name;
	gboolean  retval = TRUE;

	new_name = _g_str_eat_spaces (new_name);
	utf8_new_name = g_filename_display_name (new_name);

	if (*new_name == '\0') {
		/* Translators: the name references to a filename.  This message can appear when renaming a file. */
		*reason = g_strdup (_("New name is void, please type a name."));
		retval = FALSE;
	}
	else if ((old_name != NULL) && (strcmp (new_name, old_name) == 0)) {
		/* Translators: the name references to a filename.  This message can appear when renaming a file. */
		*reason = g_strdup (_("New name is the same as old one, please type other name."));
		retval = FALSE;
	}
	else if (_g_strchrs (new_name, BAD_CHARS)) {
		/* Translators: the %s references to a filename.  This message can appear when renaming a file. */
		*reason = g_strdup_printf (_("Name “%s” is not valid because it contains at least one of the following characters: %s, please type other name."), utf8_new_name, BAD_CHARS);
		retval = FALSE;
	}

	g_free (utf8_new_name);

	return retval;
}


/* GFile */


int
_g_file_cmp_uris (GFile *a,
                  GFile *b)
{
	char *uri_a;
	char *uri_b;
	int   result;

	uri_a = g_file_get_uri (a);
	uri_b = g_file_get_uri (b);
	result = g_strcmp0 (uri_a, uri_b);

	g_free (uri_b);
	g_free (uri_a);

	return result;
}


gboolean
_g_file_is_local (GFile *file)
{
	char     *scheme;
	gboolean  is_local;

	scheme = g_file_get_uri_scheme (file);
	is_local = strcmp (scheme, "file") == 0;

	g_free (scheme);

	return is_local;
}


GFile *
_g_file_get_home (void)
{
	static GFile *file = NULL;

	if (file != NULL)
		return file;

	file = g_file_new_for_path (g_get_home_dir ());

	return file;
}


char *
_g_file_get_display_basename (GFile *file)
{
	char *uri, *e_name, *name;

	uri = g_file_get_uri (file);
	e_name = g_filename_display_basename (uri);
	name = g_uri_unescape_string (e_name, "");

	g_free (e_name);
	g_free (uri);

	return name;
}


GFile *
_g_file_new_home_relative (const char *partial_uri)
{
	GFile *file;
	char  *uri;

	uri = g_strconcat (_g_uri_get_home (), "/", partial_uri, NULL);
	file = g_file_new_for_uri (uri);
	g_free (uri);

	return file;
}


GList *
_g_file_list_dup (GList *l)
{
	GList *r = NULL, *scan;
	for (scan = l; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_dup ((GFile*) scan->data));
	return g_list_reverse (r);
}


void
_g_file_list_free (GList *l)
{
	GList *scan;
	for (scan = l; scan; scan = scan->next)
		g_object_unref (scan->data);
	g_list_free (l);
}


GList *
_g_file_list_new_from_uri_list (GList *uris)
{
	GList *r = NULL, *scan;
	for (scan = uris; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_new_for_uri ((char*)scan->data));
	return g_list_reverse (r);
}


GFile *
_g_file_append_path (GFile  *file,
		     ...)
{
	char       *uri;
	const char *path;
	va_list     args;
	GFile      *new_file;

	uri = g_file_get_uri (file);

	va_start (args, file);
	while ((path = va_arg (args, const char *)) != NULL) {
		char *escaped;
		char *new_uri;

		escaped = g_uri_escape_string (path, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
		new_uri = g_build_filename (uri, escaped, NULL);
		g_free (uri);
		uri = new_uri;

		g_free (escaped);
	}
	va_end (args);
	new_file = g_file_new_for_uri (uri);

	g_free (uri);

	return new_file;
}


/* GKeyFile */


GList *
_g_key_file_get_string_list (GKeyFile    *key_file,
			     const char  *group_name,
			     const char  *key,
			     GError    **error)
{
	char  **strv;
	GList  *list;
	int     i;

	strv = g_key_file_get_string_list (key_file, group_name, key, NULL, error);
	if (strv == NULL)
		return NULL;

	list = NULL;
	for (i = 0; strv[i] != NULL; i++)
		list = g_list_prepend (list, strv[i]);

	g_free (strv);

	return g_list_reverse (list);
}


/* GSettings utils */


GSettings *
_g_settings_new_if_schema_installed (const char *schema_id)
{
	GSettingsSchema *schema;

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
						  schema_id,
						  TRUE);
	if (schema == NULL)
		return NULL;

	g_settings_schema_unref (schema);

	return g_settings_new (schema_id);
}


/* line parser */


gboolean
_g_line_matches_pattern (const char *line,
			 const char *pattern)
{
	const char *l = line, *p = pattern;

	for (/* void */; (*p != 0) && (*l != 0); p++, l++) {
		if (*p != '%') {
			if (*p != *l)
				return FALSE;
		}
		else {
			p++;
			switch (*p) {
			case 'a':
				break;
			case 'n':
				if (!isdigit (*l))
					return FALSE;
				break;
			case 'c':
				if (!isalpha (*l))
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}

	return (*p == 0);
}


int
_g_line_get_index_from_pattern (const char *line,
				const char *pattern)
{
	size_t      line_l, pattern_l;
	const char *l;

	line_l = strlen (line);
	pattern_l = strlen (pattern);

	if ((pattern_l == 0) || (line_l == 0))
		return -1;

	for (l = line; *l != 0; l++)
		if (_g_line_matches_pattern (l, pattern))
			return (l - line);

	return -1;
}


char*
_g_line_get_next_field (const char *line,
			int         start_from,
			int         field_n)
{
	const char *f_start, *f_end;

	line = line + start_from;

	f_start = line;
	while ((*f_start == ' ') && (*f_start != *line))
		f_start++;
	f_end = f_start;

	while ((field_n > 0) && (*f_end != 0)) {
		if (*f_end == ' ') {
			field_n--;
			if (field_n != 0) {
				while ((*f_end == ' ') && (*f_end != *line))
					f_end++;
				f_start = f_end;
			}
		}
		else
			f_end++;
	}

	return g_strndup (f_start, f_end - f_start);
}


char*
_g_line_get_prev_field (const char *line,
			int         start_from,
			int         field_n)
{
	const char *f_start, *f_end;

	f_start = line + start_from - 1;
	while ((*f_start == ' ') && (*f_start != *line))
		f_start--;
	f_end = f_start;

	while ((field_n > 0) && (*f_start != *line)) {
		if (*f_start == ' ') {
			field_n--;
			if (field_n != 0) {
				while ((*f_start == ' ') && (*f_start != *line))
					f_start--;
				f_end = f_start;
			}
		}
		else
			f_start--;
	}

	return g_strndup (f_start + 1, f_end - f_start);
}

/* threading */

gchar *
fr_get_thread_count (void)
{
	gchar *cpus;
	if (g_get_num_processors() >= 8)
		cpus = g_strdup_printf("%u", g_get_num_processors() - 2);
	else if (g_get_num_processors() >= 4)
		cpus = g_strdup_printf("%u", g_get_num_processors() - 1);
	else
		cpus = g_strdup_printf("%u", g_get_num_processors());
	return cpus;
}

/* debug */

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
#endif
}
