/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2005-2018 Free Software Foundation, Inc.
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
#include <glib.h>
#include "glib-utils.h"


static void
test_g_path_get_relative_basename_safe (const char *path,
					const char *base_dir,
					gboolean    junk_paths,
					const char *expected)
{
	const char *res;

	res = _g_path_get_relative_basename_safe (path, base_dir, FALSE);
	g_print ("%s + %s -> %s\n", base_dir, path, res);
	g_assert_cmpstr (res, == , expected);
}


static void
test_base_dir (void) {
	test_g_path_get_relative_basename_safe ("/home/user/a/file.txt", "/home/user", FALSE, "a/file.txt");
	test_g_path_get_relative_basename_safe ("/home/user/a/b/file.txt", "/home/user", FALSE, "a/b/file.txt");
	test_g_path_get_relative_basename_safe ("/home/user/../file.txt", "/home/user", FALSE, NULL);
	test_g_path_get_relative_basename_safe ("/home/user/a/../file.txt", "/home/user", FALSE, NULL);
	test_g_path_get_relative_basename_safe ("/home/user/a/../../file.txt", "/home/user", FALSE, NULL);
	test_g_path_get_relative_basename_safe ("/home/user/a/../../file.txt", "/home/user", FALSE, NULL);
	test_g_path_get_relative_basename_safe ("/home/user/a/b/../file.txt", "/home/user", FALSE, NULL);
}


static void
test_g_path_get_relative_basename (const char *path,
				   const char *base_dir,
				   gboolean    junk_paths,
				   const char *expected)
{
	const char *res;

	res = _g_path_get_relative_basename (path, base_dir, FALSE);
	g_print ("%s + %s -> %s\n", base_dir, path, res);
	g_assert_cmpstr (res, == , expected);
}


static void
test_relative_basename (void) {
	test_g_path_get_relative_basename ("/home/user1/file.txt", "/home/user1", FALSE, "/file.txt");
	test_g_path_get_relative_basename ("/home/user1/a/file.txt", "/home/user1", FALSE, "/a/file.txt");
	test_g_path_get_relative_basename ("/home/user1/../file.txt", "/home/user1", FALSE, "/../file.txt");
}


int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/_g_path_get_relative_basename", test_relative_basename);
	g_test_add_func ("/_g_path_get_relative_basename_safe/correct_base_dir", test_base_dir);

	return g_test_run ();
}
