/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2006 The Free Software Foundation, Inc.
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
 
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include "java-utils.h"


/* 
 * The following code conforms to the JVM specification.(Java 2 Platform)
 * For further changes to the classfile structure, please update the 
 * following macros.
 */


/* Tags that identify structures */

#define CONST_CLASS				 7
#define CONST_FIELDREF				 9
#define CONST_METHODREF				10
#define CONST_INTERFACEMETHODREF		11
#define CONST_STRING				 8
#define CONST_INTEGER				 3
#define CONST_FLOAT				 4
#define CONST_LONG				 5
#define CONST_DOUBLE				 6
#define CONST_NAMEANDTYPE			12
#define CONST_UTF8				 1

/* Sizes of structures */

#define CONST_CLASS_INFO			 2
#define CONST_FIELDREF_INFO			 4
#define CONST_METHODREF_INFO	 		 4
#define CONST_INTERFACEMETHODREF_INFO		 4
#define CONST_STRING_INFO			 2
#define CONST_INTEGER_INFO			 4
#define CONST_FLOAT_INFO			 4
#define CONST_LONG_INFO				 8
#define CONST_DOUBLE_INFO			 8
#define CONST_NAMEANDTYPE_INFO			 4


/* represents the utf8 strings in class file */
struct utf_string	
{
	guint16 index;
	guint16 length;
	gchar *str;
};

/* structure that holds class information in a class file */
struct class_info
{
	guint16 index;
	guint16 name_index; /* index into the utf_strings */
};

struct classfile
{
	guint32 magic_no;		/* 0xCAFEBABE (JVM Specification) :) */

	guint16 major;			/* versions */
	guint16 minor;

	guint16 const_pool_count;
	GSList *const_pool_class;	/* (const_pool_count - 1) elements of tye 'CONST_class_info' */
	GSList *const_pool_utf;		/* (const_pool_count - 1) elements of type 'utf_strings' */

	guint16 access_flags;
	guint16 this_class;		/* the index of the class the file is named after. */
	
	/* not needed */
#if 0	
	guint16         super_class;
	guint16         interfaces_count;
	guint16        *interfaces;
	guint16         fields_count;
	field_info     *fields;
	guint16         methods_count;
	method_info    *methods;
	guint16         attributes_count;
	attribute_info *attributes;
#endif
};


static struct classfile cfile;
static int    fdesc = -1;


/* The following function loads the utf8 strings and class structures from the 
 * class file. */
static void
load_constant_pool_utfs (void)
{
	guint8 tag;
	guint16 i = 0;		// should be comparable with const_pool_count
	while( i < (cfile.const_pool_count - 1) && read( fdesc, &tag, 1 ) != -1 )	{
		struct utf_string *txt = NULL;
		struct class_info *class = NULL;
		switch(tag)	{				//	identify the structure's tag
			case CONST_CLASS:				//	new class structure found in class file
				class = g_new( struct class_info, 1 );
				class->index = i + 1;
				if( read( fdesc, &class->name_index, 2 ) != 2 )
					return;			//	error reading
				class->name_index = GUINT16_FROM_BE(class->name_index);
				cfile.const_pool_class = g_slist_append( cfile.const_pool_class, class );
				break;
			case CONST_FIELDREF:
				lseek( fdesc, CONST_FIELDREF_INFO, SEEK_CUR);
				break;
			case CONST_METHODREF:
				lseek( fdesc, CONST_METHODREF_INFO, SEEK_CUR);
				break;
			case CONST_INTERFACEMETHODREF:
				lseek( fdesc, CONST_INTERFACEMETHODREF_INFO, SEEK_CUR);
				break;
			case CONST_STRING:
				lseek( fdesc, CONST_STRING_INFO, SEEK_CUR);
				break;
			case CONST_INTEGER:
				lseek( fdesc, CONST_INTEGER_INFO, SEEK_CUR);
				break;
			case CONST_FLOAT:
				lseek( fdesc, CONST_FLOAT_INFO, SEEK_CUR);
				break;
			case CONST_LONG:
				lseek( fdesc, CONST_LONG_INFO, SEEK_CUR);
				break;
			case CONST_DOUBLE:
				lseek( fdesc, CONST_DOUBLE_INFO, SEEK_CUR);
				break;
			case CONST_NAMEANDTYPE:
				lseek( fdesc, CONST_NAMEANDTYPE_INFO, SEEK_CUR);
				break;
			case CONST_UTF8:						//	new utf8 string found in class file
				txt = g_new( struct utf_string, 1);
				txt->index = i + 1;
				if( read( fdesc, &(txt->length), 2 ) == -1 )
					return;			//	error while reading
				txt->length = GUINT16_FROM_BE(txt->length);
				txt->str = g_new( gchar, txt->length );
				if( read( fdesc, txt->str, txt->length ) == -1 )
					return;			//	error while reading
				cfile.const_pool_utf = g_slist_append( cfile.const_pool_utf, txt );
				break;
			default:
					return;			//	error - unknown tag in class file
				break;
		}
		i++;
	}
	if( i != (cfile.const_pool_count - 1) )
		return;		//	error - all entries not read
#ifdef DEBUG
	else
		g_print( "Number of Entries: %d", i );
#endif
}


/*	This function frees up the space used by the cfile.(very important)	*/
static void
free_classfile_structure()
{
	GSList *scan;

	g_slist_foreach( cfile.const_pool_class, (GFunc)g_free, NULL );
	g_slist_free(cfile.const_pool_class);

	for( scan = cfile.const_pool_utf; scan ; scan = scan->next )	{
		struct utf_string *string= scan->data;
		g_free(string->str);
	}
	g_slist_foreach( cfile.const_pool_utf, (GFunc)g_free, NULL );
	g_slist_free(cfile.const_pool_utf);

	memset( &cfile, 0, sizeof(struct classfile) );			//	set memory to 0
}


/*	This function extracts the package name from a class file	*/
gchar*
get_package_name_from_class_file( gchar *fname )
{
	gchar *package = NULL;
	guint16 length = 0, end = 0, utf_index = 0;
	int i = 0;
	if(g_file_test( fname, G_FILE_TEST_EXISTS))	{			//	check if file exists
		fdesc = open( fname, O_RDONLY );
		guint32 magic;
		guint16 major, minor, count;

		if( fdesc == -1 )
			return NULL;		//	cannot open file

		if( (i = read( fdesc, &magic, 4 )) != 4 )
			return NULL;				//	unexpected EOF
		cfile.magic_no = GUINT32_FROM_BE(magic);

		if( read( fdesc, &major, 2 ) != 2 )
			return NULL;				//	unexpected EOF
		cfile.major = GUINT16_FROM_BE(major);

		if( read( fdesc, &minor, 2 ) != 2 )
			return NULL;				//	unexpected EOF
		cfile.minor = GUINT16_FROM_BE(minor);

		if( read( fdesc, &count, 2 ) != 2 )
			return NULL;				//	unexpected EOF
		cfile.const_pool_count = GUINT16_FROM_BE(count);
		load_constant_pool_utfs();

		if( read( fdesc, &cfile.access_flags, 2 ) != 2 )
			return NULL;				//	unexpected EOF
		cfile.access_flags = GUINT16_FROM_BE(cfile.access_flags);

		if( read( fdesc, &cfile.this_class, 2 ) != 2 )
			return NULL;				//	unexpected EOF
		cfile.this_class = GUINT16_FROM_BE(cfile.this_class);
	}
	else
		return NULL;				//	unexpected EOF


	//	now search for the class structure with index = cfile.this_class
	for( i = 0; (i < g_slist_length(cfile.const_pool_class)) && (utf_index == 0); i++ )	{
		struct class_info *class = g_slist_nth_data( cfile.const_pool_class, i );
		if( class->index == cfile.this_class )
			utf_index = class->name_index;			//	terminates loop
	}

	//	now search for the utf8 string with index = utf_index
	for( i = 0; i < g_slist_length(cfile.const_pool_utf); i++ )	{
		struct utf_string *data = g_slist_nth_data( cfile.const_pool_utf, i );
		if( data->index == utf_index )	{
			package = g_strndup( data->str, data->length );
			length = data->length;
			break;
		}
	}

	if(package)	{
		for( i = length; (i >= 0) && (end == 0); i-- )	{
			if( package[i] == '/' )
				end = i;	//	terminates loop
		}
		package = g_strndup( package, end );		//	ensure null terminated string
	}

	free_classfile_structure();			//	free the memory held by the cfile structure.
	return package;
}


/*
	This function consumes a comment from the java file
	multiline = TRUE implies that comment is multiline
*/
static void
consume_comment( gboolean multiline )
{
	//	this function consumes a comment - it implements a DFA (Deterministic finite automata)
	gchar ch;
	gboolean escaped = FALSE;
	gboolean star = FALSE;
	while( read( fdesc, &ch, 1 ) == 1 )	{
		switch(ch)	{
			case '/':
				if( escaped )
					break;
				else if(star == TRUE)
					return;
				break;
			case '\n':
				if(multiline == FALSE)
					return;
				break;
			case '*':
				escaped = FALSE;
				star = TRUE;
				break;
			case '\\':
				escaped = (escaped == TRUE) ? FALSE : TRUE;
				break;
			default:
				escaped = FALSE;
				star = FALSE;
				break;
		}
	}
}


/*	This function extracts package name from a java file	*/
gchar*
get_package_name_from_java_file( gchar *fname )
{
	gchar *package = NULL, *first_valid_word, ch;
	gboolean prev_char_is_bslash = FALSE;
	gboolean valid_char_found = FALSE;

	if(g_file_test( fname, G_FILE_TEST_EXISTS))	{
		fdesc = open( fname, O_RDONLY );
		while( (valid_char_found == FALSE) && (read( fdesc, &ch, 1 ) == 1) )	{
			switch(ch)	{
				case '/':
					if(prev_char_is_bslash == TRUE)	{		//	single line comment found
						consume_comment(FALSE);
						prev_char_is_bslash = FALSE;
					}
					else
						prev_char_is_bslash = TRUE;			//	indicate '/' occured
					break;
				case '*':
					if(prev_char_is_bslash == TRUE)			//	multiline comment
						consume_comment(TRUE);
					prev_char_is_bslash = FALSE;
					break;
				case ' ':case '\t':case '\r':case '\n':
					prev_char_is_bslash = FALSE;			//	white space
					break;
				default:
					prev_char_is_bslash = FALSE;
					valid_char_found = TRUE;
					break;
			}
		}
		if( ch == 'p' )	{				//	probable - package statement
			first_valid_word = g_malloc(8);
			first_valid_word[0] = 'p';
			if( read( fdesc, &first_valid_word[1], 6 ) != 6 )
				return NULL;			//	unexpected EOF
			first_valid_word[7] = 0;
			if( !g_ascii_strcasecmp( first_valid_word, "package" ) )	{		//	package statement found
				//	code to read package name here
				gint index = 0;
				gchar *buffer = g_malloc(100);
				while( read( fdesc, &ch, 1 ) == 1 )	{
					if( ch == ';' )			//	read upto the nearest ';'
						break;
					if( ch == '.' )			//	convert '.' to '/'
						buffer[index++] = '/';
					else if( (ch >=48 && ch <= 57) || (ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122) )
						buffer[index++] = ch;	//	valid character
				}
				buffer[index] = 0;
				package = g_strdup( buffer );
			//	g_free( buffer );
			}
		}
	}
	else
		return NULL;	//	error - unable to open file.

	return package;
}
