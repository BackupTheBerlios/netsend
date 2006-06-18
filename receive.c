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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "global.h"

extern struct opts opts;
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];


static ssize_t
cs_read(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t rc;
	char *buf;

	/* user option or default(DEFAULT_BUFSIZE) */
	buflen = opts.buffer_size;

	/* allocate read buffer */
	buf = malloc(buflen);
	if (!buf) {
		fprintf(stderr, "ERROR: Can't allocate %d bytes: %s!\n",
				buflen, strerror(errno));
		exit(EXIT_FAILMEM);
	}

	/* main client loop */
	while ((rc = read(connected_fd, buf, buflen)) > 0) {
		write(file_fd, buf, rc); /* FIXME: to late and to drunken ... */
	}

	return rc;
}

/* Creates our client socket and initialize
** options
*/
static int
instigate_cs(void)
{
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;

	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
	hosthints.ai_flags    = AI_PASSIVE;

	xgetaddrinfo(NULL, opts.port, &hosthints, &hostres);

	for (addrtmp = hostres; addrtmp != NULL ; addrtmp = addrtmp->ai_next) {

		if (addrtmp->ai_family != opts.family)
			continue;

		fd = socket(addrtmp->ai_family, addrtmp->ai_socktype,
				addrtmp->ai_protocol);

		if (fd < 0) {
			err_sys("socket");
			continue;
		}

		ret = bind(fd, addrtmp->ai_addr, addrtmp->ai_addrlen);
		if (ret == 0)
			break;  /* success */
	}

	if (opts.protocol == IPPROTO_TCP) {
		ret = listen(fd, BACKLOG);
		if (ret < 0) {
			err_sys("listen(%d, %d) failed", fd, BACKLOG);
			exit(EXIT_FAILNET);
		}
	}

	freeaddrinfo(hostres);

	return fd;
}

/* *** Main Client Routine ***
**
** o initialize client socket
** o allocate receive buffer
** o receive routine
** o write file, print diagnostic info and exit
*/
void
receive_mode(void)
{
	int file_fd, connected_fd, server_fd;

	msg(GENTLE, "receiver mode");

	file_fd = open_output_file();

	server_fd = instigate_cs();

	do {
		struct sockaddr sa;
		socklen_t sa_len = sizeof sa;

		msg(LOUDISH, "accept");

		if (opts.protocol == IPPROTO_TCP) {
			connected_fd = accept(server_fd, &sa, &sa_len);
			if (connected_fd == -1) {
				err_sys("accept error");
				exit(EXIT_FAILNET);
			}
		}


		/* take the transmit start time for diff */
		gettimeofday(&opts.starttime, NULL);

		msg(LOUDISH, "read");
		cs_read(file_fd, opts.protocol == IPPROTO_TCP ? connected_fd : server_fd);
		msg(LOUDISH, "done");

		gettimeofday(&opts.endtime, NULL);

	} while(0); /* XXX: Further improvement: iterating server ;-) */
}

/* vim:set ts=4 sw=4 tw=78 noet: */
