/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2021 Free Software Foundation, Inc.
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

#include <glib.h>
#include "fr-thread-count.h"

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
