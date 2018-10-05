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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FR_COMMAND_ACE_H
#define FR_COMMAND_ACE_H

#include "fr-command.h"

typedef enum {
	FR_ACE_COMMAND_UNKNOWN = 0,
	FR_ACE_COMMAND_PUBLIC,
	FR_ACE_COMMAND_NONFREE
} FrAceCommand;

G_DECLARE_FINAL_TYPE (FrCommandAce, fr_command_ace, FR, COMMAND_ACE, FrCommand)

#endif /* FR_COMMAND_ACE_H */
