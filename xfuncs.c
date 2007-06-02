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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/utsname.h>

#include <limits.h>

#ifndef ULLONG_MAX
# define ULLONG_MAX 18446744073709551615ULL
#endif

#include "global.h"
#include "xfuncs.h"


/* Simple malloc wrapper - prevent error checking */
void *
xmalloc(size_t size)
{

	void *ptr = malloc(size);

	if (!ptr)
		err_msg_die(EXIT_FAILMEM, "Out of mem: %s!\n", strerror(errno));
	return ptr;
}


void xgetaddrinfo(const char *node, const char *service,
		struct addrinfo *hints, struct addrinfo **res)
{
	int ret;

	ret = getaddrinfo(node, service, hints, res);
	if (ret != 0) {
		err_msg_die(EXIT_FAILNET, "Call to getaddrinfo() failed: %s!\n",
				(ret == EAI_SYSTEM) ?  strerror(errno) : gai_strerror(ret));
	}
}


void xsetsockopt(int s, int level, int optname, const void *optval, socklen_t optlen, const char *str)
{
	int ret = setsockopt(s, level, optname, optval, optlen);
	if (ret)
		err_sys_die(EXIT_FAILNET, "Can't set socketoption %s", str);
}


int xsnprintf(char *str, size_t size, const char *format, ...)
{
	va_list ap;
	int len;

	va_start(ap, format);
	len = vsnprintf(str, size, format, ap);
	va_end(ap);
        if (len < 0 || ((size_t)len) >= size)
		err_msg_die(EXIT_FAILINT, "buflen %u not sufficient (ret %d)",
								size, len);
	return len;
}


char *xstrdup(const char *src)
{
	size_t len = strlen(src) + 1;
	char *duplicate;
	if (!len) /* integer overflow */
		err_msg_die(EXIT_FAILINT, "xstrdup: string execeeds size_t range");
	duplicate = xmalloc(len);
	memcpy(duplicate, src, len);
	return duplicate;
}


/* vim:set ts=4 sw=4 tw=78 noet: */
