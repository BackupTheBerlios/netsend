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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>


#include "global.h"

extern struct opts opts;



int
open_input_file(void)
{
	int fd, ret;
	struct stat stat_buf;

	if (!strncmp(opts.infile, "-", 1)) {
		return STDIN_FILENO;
	}

	/* We don't want to read from a regular file
	** rather we want to execute a program and take
	** this output as our data source.
	*/
	if (opts.execstring) {

		pid_t pid;
		int pipefd[2];

		ret = pipe(pipefd);
		if (ret == -1) {
			err_sys("Can't create pipe");
			exit(EXIT_FAILMISC);
		}

		switch (pid = fork()) {
			case -1:
				err_sys("Can't fork");
				exit(EXIT_FAILMISC);
				break;
			case 0:
				close(STDOUT_FILENO);
				close(STDERR_FILENO);
				close(pipefd[0]);
				dup(pipefd[1]);
				dup(pipefd[1]);
				system(opts.execstring);
				exit(0);
				break;
			default:
				close(pipefd[1]);
				return pipefd[0];
				break;
		}

	}

	/* Thats the normal case: we open a regular file and take
	** the content as our source.
	*/
	ret = stat(opts.infile, &stat_buf);
	if (ret == -1) {
		err_sys("Can't fstat file %s", opts.infile);
		exit(EXIT_FAILMISC);
	}

#if 0
	if (!(stat_buf.st_mode & S_IFREG)) {
		err_sys("Not an regular file %s", opts.infile);
		exit(EXIT_FAILOPT);
	}
#endif

	fd = open(opts.infile, O_RDONLY);
	if (fd == -1) {
		err_msg("Can't open input file: %s", opts.infile);
		exit(EXIT_FAILMISC);
	}

	return fd;
}

/* open our outfile */
int
open_output_file(void)
{
	int fd = 0;

	if (!opts.outfile) {
		return STDOUT_FILENO;
	}

	if (!strcmp(opts.outfile, "-")) {
		return STDOUT_FILENO;
	}

	umask(0);

	fd = open(opts.outfile, O_WRONLY | O_CREAT | O_EXCL,
			  S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd == -1) {
		struct stat s;
		if (errno != EEXIST)
			err_sys_die(EXIT_FAILOPT, "Can't create outputfile: %s", opts.outfile);

		fd = open(opts.outfile, O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP);
		if (fd == -1)
			err_sys_die(EXIT_FAILOPT, "Can't open outputfile: %s", opts.outfile);

		if (fstat(fd, &s))
			err_sys_die(EXIT_FAILOPT, "stat() error: %s", opts.outfile);

		if (S_ISREG(s.st_mode)) /* symblic link that pointed to regular file */
			err_sys_die(EXIT_FAILOPT, "Can't create outputfile: %s", opts.outfile);
		/* else file is a named pipe, socket, etc. */
	}

	return fd;
}




/* vim:set ts=4 sw=4 tw=78 noet: */
