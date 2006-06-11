/*
** $Id$
**
** netsend - a high performance filetransfer and diagnostic tool
** http://netsend.berlios.de
**
**
** Copyright (C) 2006 - Hagen Paul Pfeifer <hagen@jauu.net>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "global.h"

/* Simple malloc wrapper - prevent error checking */
void *
alloc(size_t size) {

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	return ptr;
}

void *
salloc(int c, size_t size){

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	memset(ptr, c, size);

	return ptr;
}


/* vim:set ts=4 sw=4 tw=78 noet: */
