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

extern struct opts opts;
extern struct conf_map_t congestion_map[];
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
	int fd = 0, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;


	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;


	xgetaddrinfo(opts.hostname, opts.port, &hosthints, &hostres);

	addrtmp = hostres;

	do {
		/* We do not want unix domain socket's */
		if (addrtmp->ai_family == PF_LOCAL) {
			continue;
		}

		/* TODO: add some sanity checks here ... */

		/* We have found for what we are looking for */
		if (addrtmp->ai_family == opts.family) {
			break;
		}

	} while ((addrtmp = addrtmp->ai_next));

	fd = socket(hostres->ai_family, hostres->ai_socktype,
			hostres->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Can't create server socket: %s\n",
				strerror(errno));
		exit(EXIT_FAILNET);
	}


	if (opts.change_congestion) {

		if (VL_LOUDISH(opts.verbose)) {
			fprintf(stderr, "Congestion Avoidance: %s\n",
					congestion_map[opts.congestion].conf_string);
		}

		struct protoent *pptr = getprotobyname("tcp");
		if (!pptr) {
			fprintf(stderr, "getprotobyname() return uncleanly!\n");
			exit(EXIT_FAILNET);
		}

		ret = setsockopt(fd, pptr->p_proto, TCP_CONGESTION,
				congestion_map[opts.congestion].conf_string,
				strlen(congestion_map[opts.congestion].conf_string) + 1);
		if (ret < 0 && VL_GENTLE(opts.verbose)) {
			fprintf(stderr, "Can't set congestion avoidance algorithm(%s): %s!\n"
					"Did you build a kernel with proper ca support?\n",
					congestion_map[opts.congestion].conf_string,
					strerror(errno));
		}
	}

	ret = connect(fd, hostres->ai_addr, hostres->ai_addrlen);
	if (ret == -1) {
		fprintf(stderr,"ERROR: Can't connect to %s: %s!\n",
				opts.hostname, strerror(errno));
		exit(EXIT_FAILNET);
	}

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
client_mode(void)
{
	int file_fd, connected_fd = 0;

	if (VL_GENTLE(opts.verbose))
		fprintf(stdout, "Client Mode (Hostname: %s)\n", opts.hostname);

	file_fd = open_output_file();

	connected_fd = instigate_cs();

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	cs_read(file_fd, connected_fd);

	gettimeofday(&opts.endtime, NULL);
}

/* vim:set ts=4 sw=4 tw=78 noet: */
