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


#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
/* TODO: extend our configure script to loop for declaration */
#include <execinfo.h>
#include <time.h>

#include "global.h"

#define	MAXERRMSG 1024

extern struct opts opts;


void
msg(const int level, const char *format, ...)
{
	va_list ap;
	struct timeval tv;

	 if (opts.verbose < level)
		 return;

	 if (opts.verbose > LOUDISH) {
		 gettimeofday(&tv, NULL);
		 fprintf(stderr, "[%ld.%ld] ", tv.tv_sec, tv.tv_usec);
	 }

	 va_start(ap, format);
	 vfprintf(stderr, format, ap);
	 va_end(ap);

	 fputs("\n", stderr);
}


static void
err_doit(int sys_error, const char *file, const int line_no,
		 const char *fmt, va_list ap)
{
	int	errno_save;
	char buf[MAXERRMSG];

	errno_save = errno;

	vsnprintf(buf, sizeof buf -1, fmt, ap);
	if (sys_error) {
		size_t len = strlen(buf);
		snprintf(buf + len,  sizeof buf - len, " (%s)", strerror(errno_save));
	}

#ifdef DEBUG
	fprintf(stderr, "ERROR [%s:%d]: %s\n", file, line_no, buf);
#else
	fprintf(stderr, "ERROR: %s\n", buf);
	/* shut-up gcc warnings ... */
	(void) file;
	(void) line_no;
#endif
	fflush(NULL);
}

void
x_err_ret(const char *file, int line_no, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_doit(0, file, line_no, fmt, ap);
	va_end(ap);
	return;
}


void
x_err_sys(const char *file, int line_no, const char *fmt, ...)
{
	va_list		ap;

	va_start(ap, fmt);
	err_doit(1, file, line_no, fmt, ap);
	va_end(ap);
}

void print_bt(void)
{
	void *bt[128];
	int bt_size;
	char **bt_syms;
	int i;

	bt_size = backtrace(bt, 128);
	bt_syms = backtrace_symbols(bt, bt_size);
	fputs("BACKTRACE:\n", stderr);
	for(i = 1; i < bt_size; i++) {
		fprintf(stderr, "#%2d  %s\n", i - 1, bt_syms[i]);
	}
	fputs("\n", stderr);
}


/* vim:set ts=4 sw=4 tw=78 noet: */
