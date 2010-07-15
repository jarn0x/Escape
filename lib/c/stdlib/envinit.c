/**
 * $Id: envinit.c 650 2010-05-06 19:05:10Z nasmussen $
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <esc/common.h>
#include <esc/lock.h>
#include <esc/io.h>
#include <stdlib.h>
#include "envintern.h"

/* the fd for the env-driver */
tFD envFd = -1;
static tULock envLock;

s32 initEnv(void) {
	locku(&envLock);
	if(envFd >= 0) {
		unlocku(&envLock);
		return 0;
	}
	envFd = open("/dev/env",IO_READ | IO_WRITE);
	unlocku(&envLock);
	return envFd;
}