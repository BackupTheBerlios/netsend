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
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

 /* signale stuff */
 #define __GNU_SOURCE
 #include <string.h>
 #include <signal.h>

#include "global.h"

extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];

/* This is our inner receive function.
** It reads from a connected socket descriptor
** and write to the file descriptor
*/
static ssize_t
cs_read(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t rc;
	char *buf;

	/* user option or default(DEFAULT_BUFSIZE) */
	buflen = (opts.buffer_size == 0) ? DEFAULT_BUFSIZE : opts.buffer_size;

	/* allocate read buffer */
	buf = malloc(buflen);
	if (!buf) {
		fprintf(stderr, "ERROR: Can't allocate %d bytes: %s!\n",
				buflen, strerror(errno));
		exit(EXIT_FAILMEM);
	}

	/* main client loop */
	while ((rc = read(connected_fd, buf, buflen)) > 0) {
		net_stat.read_call_cnt++;
		net_stat.read_call_bytes += rc;
		write(file_fd, buf, rc); /* FIXME: to late and to drunken ... */
	}

	return rc;
}


/* Creates our client socket and initialize
** options
**
** XXX: at the moment we can't release ourself from the mulicast channel
** because struct ip_mreq and struct ipv6_mreq is function local. But at
** the moment this doesn't really matter because netsend deliever the file
** and exit - it isn't a uptime daemon.
*/
static int
instigate_cs(int *ret_fd)
{
	int on = 1;
	char *hostname = NULL;
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;
	struct ip_mreq mreq;
	struct ipv6_mreq mreq6;


	memset(&hosthints, 0, sizeof(struct addrinfo));

	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
	hosthints.ai_flags    = AI_PASSIVE | AI_ADDRCONFIG;


	/* Check if the user want to bind to a
	** multicast channel. We must implement this check
	** here because if something fail we set hostname to
	** NULL and initialize a standard udp socket
	*/
	if (opts.hostname && opts.protocol == IPPROTO_UDP) {

		hostname = opts.hostname;

		if (inet_pton(AF_INET, hostname, &mreq.imr_multiaddr) <= 0) {
			if (inet_pton(AF_INET6, hostname, &mreq6.ipv6mr_multiaddr) <= 0) {
				err_msg("You didn't specify an valid multicast address (%s)!",
						hostname);
				exit(EXIT_FAILNET);
			}
			/* IPv6 */
			if (!IN6_IS_ADDR_MULTICAST(&mreq6.ipv6mr_multiaddr)) {
				err_msg("You didn't specify an valid IPv6 multicast address (%s)!",
						hostname);
				exit(EXIT_FAILNET);
			}
			hosthints.ai_family = AF_INET6;
			mreq6.ipv6mr_interface = 0;
			use_multicast = true;

		} else { /* IPv4 */
			if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr))) {
				err_msg("You didn't specify an valid IPv4 multicast address (%s)!",
						hostname);
				exit(EXIT_FAILNET);
			}
			hosthints.ai_family = AF_INET;
			mreq.imr_interface.s_addr = INADDR_ANY;
			use_multicast = true;

			/* no look if our user specify strict ipv6 address (-6) but
			** deliver us with a (valid) ipv4 multicast address
			*/
			if (opts.family == AF_INET6) {
				err_msg("You specify strict ipv6 support (-6) add a "
						"IPv4 multicast address!");
				exit(EXIT_FAILOPT);
			}
		}
		hosthints.ai_flags = AI_NUMERICHOST | AI_ADDRCONFIG;
	}

	/* probe our values */
	xgetaddrinfo(hostname, opts.port, &hosthints, &hostres);

	for (addrtmp = hostres; addrtmp != NULL ; addrtmp = addrtmp->ai_next) {

		if (opts.family != AF_UNSPEC &&
			addrtmp->ai_family != opts.family) { /* user fixed family! */
			continue;
		}

		fd = socket(addrtmp->ai_family, addrtmp->ai_socktype,
				addrtmp->ai_protocol);

		if (fd < 0) {
			err_sys("socket");
			continue;
		}

		/* For multicast sockets it is maybe necessary to set
		** socketoption SO_REUSEADDR, cause multiple receiver on
		** the same host will bind to this local socket.
		** In all other cases: there is no penalty - hopefully! ;-)
		*/
		ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		if (ret == -1) {
			err_sys("setsockopt (SO_REUSEADDR)");
			exit(EXIT_FAILNET);
		}

		ret = bind(fd, addrtmp->ai_addr, addrtmp->ai_addrlen);
		if (ret == 0) {   /* bind call success */

			if (use_multicast) {

				switch (addrtmp->ai_family) {
					case AF_INET6:
						ret = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
								&on, sizeof(int));
						if (ret == -1) {
							err_sys("setsockopt (IPV6_MULTICAST_LOOP) failed");
							exit(EXIT_FAILNET);
						}
						msg(STRESSFUL, "set IPV6_MULTICAST_LOOP option");

						ret = setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
								         &mreq6, sizeof(mreq6));
						if (ret == -1) {
							err_sys("setsockopt (IPV6_JOIN_GROUP) failed");
							exit(EXIT_FAILNET);
						}
						msg(GENTLE, "join IPv6 multicast group");

						break;
					case AF_INET:
						ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
								&on, sizeof(int));
						if (ret == -1) {
							err_sys("setsockopt (IP_MULTICAST_LOOP) failed");
							exit(EXIT_FAILNET);
						}
						msg(STRESSFUL, "set IP_MULTICAST_LOOP option");

						ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
								         &mreq, sizeof(mreq));
						if (ret == -1) {
							err_sys("setsockopt (IP_ADD_MEMBERSHIP) failed");
							exit(EXIT_FAILNET);
						}
						msg(GENTLE, "add membership to IPv4 multicast group");
						break;
					default:
						err_msg("Programmed Error");
						exit(EXIT_FAILINT);
						break;
				}
			}
			/* Fine: we find a valid socket, bind to it and probably set
			** socketoptions for the multicast mode.
			** Now break out an do the really interesting stuff ....
			*/
			break;
		}
	}

	if (opts.protocol == IPPROTO_TCP) {
		ret = listen(fd, BACKLOG);
		if (ret < 0) {
			err_sys("listen(fd: %d, backlog: %d) failed", fd, BACKLOG);
			exit(EXIT_FAILNET);
		}
	}

	freeaddrinfo(hostres);

	*ret_fd = fd;

	return 0;
}

static void
signal_hndl(int signo)
{
	(void) signo;

	fprintf(stderr, "received signal\n");
	return;
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
	int ret, file_fd, connected_fd = -1, server_fd;
	struct sockaddr_storage sa;
	socklen_t sa_len = sizeof sa;
	struct sigaction sigterm_sa;



	sigterm_sa.sa_handler = signal_hndl;
	sigemptyset(&sigterm_sa.sa_mask);
	sigterm_sa.sa_flags = 0;
	sigaction(SIGPIPE, &sigterm_sa, NULL);

	msg(GENTLE, "receiver mode");

	file_fd = open_output_file();

	instigate_cs(&server_fd);


	if (opts.protocol == IPPROTO_TCP) {

		char peer[1024];

		connected_fd = accept(server_fd, (struct sockaddr *) &sa, &sa_len);
		if (connected_fd == -1) {
			err_sys("accept error");
			exit(EXIT_FAILNET);
		}

		ret = getnameinfo((struct sockaddr *)&sa, sa_len, peer,
				sizeof(peer), NULL, 0, 0);
		if (ret != 0) {
			err_msg("getnameinfo error: %s",  gai_strerror(ret));
			exit(EXIT_FAILNET);
		}
		msg(GENTLE, "accept from %s", peer);
	}


	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	msg(LOUDISH, "block in read");

	cs_read(file_fd, opts.protocol == IPPROTO_TCP ? connected_fd : server_fd);

	msg(LOUDISH, "done");

	gettimeofday(&opts.endtime, NULL);

	/* FIXME: print statistic here */

	/* We sync the file descriptor here because in a worst
	** case this call block and sophisticate the time
	** measurement.
	*/
	fsync(file_fd);

}

/* vim:set ts=4 sw=4 tw=78 noet: */
