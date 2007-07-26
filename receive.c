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
#include "xfuncs.h"
#include "proto_tipc.h"

extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];
extern struct sock_callbacks sock_callbacks;

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

	buf = xmalloc(buflen);

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	/* main client loop */
	while ((rc = read(connected_fd, buf, buflen)) > 0) {
		ssize_t ret;
		net_stat.total_rx_calls++;
		net_stat.total_rx_bytes += rc;
		do
			ret = write(file_fd, buf, rc);
		while (ret == -1 && errno == EINTR);

		if (ret != rc) {
			err_sys("write failed");
			break;
		}
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);
	free(buf);
	return rc;
}


static void set_muticast4(int fd, struct ip_mreq *mreq)
{
	int on = 1;

	xsetsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &on, sizeof(int), "IP_MULTICAST_LOOP");
	msg(STRESSFUL, "set IP_MULTICAST_LOOP option");

	xsetsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, mreq, sizeof(*mreq), "IP_ADD_MEMBERSHIP");
	msg(GENTLE, "add membership to IPv4 multicast group");
}


static void set_muticast6(int fd, struct ipv6_mreq *mreq6)
{
	int on = 1;

	xsetsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
		&on, sizeof(int), "IPV6_MULTICAST_LOOP");
	msg(STRESSFUL, "set IPV6_MULTICAST_LOOP option");

	xsetsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
	         mreq6, sizeof(*mreq6), "IPV6_JOIN_GROUP");
	msg(GENTLE, "join IPv6 multicast group");
}


static int socket_bind(struct addrinfo *a)
{
	int ret, on = 1;
	int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
	if (fd < 0)
		return -1;

	/* For multicast sockets it is maybe necessary to set
	 * socketoption SO_REUSEADDR, cause multiple receiver on
	 * the same host will bind to this local socket.
	 * In all other cases: there is no penalty - hopefully! ;-)
	 */
	xsetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on), "SO_REUSEADDR");

	ret = bind(fd, a->ai_addr, a->ai_addrlen);
	if (ret) {
		err_msg("bind failed");
		close(fd);
		return -1;
	}
	return fd;
}


#ifdef HAVE_AF_TIPC
static int instigate_cs_tipc(void)
{
	int fd = tipc_socket_bind();
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "tipc_socket_bind");
	if (sock_callbacks.cb_listen(fd, BACKLOG))
		err_sys_die(EXIT_FAILNET, "listen(fd: %d, backlog: %d) failed", fd, BACKLOG);
	return fd;
}
#endif


/* Creates our client socket and initialize
** options
**
** XXX: at the moment we can't release ourself from the mulicast channel
** because struct ip_mreq and struct ipv6_mreq is function local. But at
** the moment this doesn't really matter because netsend deliever the file
** and exit - it isn't a uptime daemon.
*/
static int
instigate_cs(void)
{
	char *hostname = NULL;
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo hosthints, *hostres, *addrtmp;
	struct ip_mreq mreq;
	struct ipv6_mreq mreq6;
#ifdef HAVE_AF_TIPC
	if (opts.family == AF_TIPC)
		return instigate_cs_tipc();
#endif
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
			if (inet_pton(AF_INET6, hostname, &mreq6.ipv6mr_multiaddr) <= 0)
				err_msg_die(EXIT_FAILNET, "You didn't specify an valid multicast address (%s)!",
						hostname);
			/* IPv6 */
			if (!IN6_IS_ADDR_MULTICAST(&mreq6.ipv6mr_multiaddr))
				err_msg_die(EXIT_FAILNET, "You didn't specify an valid IPv6 multicast address (%s)!",
						hostname);

			hosthints.ai_family = AF_INET6;
			mreq6.ipv6mr_interface = 0;
		} else { /* IPv4 */
			if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr)))
				err_msg_die(EXIT_FAILNET, "You didn't specify an valid IPv4 multicast address (%s)!",
						hostname);
			hosthints.ai_family = AF_INET;
			mreq.imr_interface.s_addr = INADDR_ANY;

			/* no look if our user specify strict ipv6 address (-6) but
			** deliver us with a (valid) ipv4 multicast address
			*/
			if (opts.family == AF_INET6)
				err_msg_die(EXIT_FAILOPT, "You specify strict ipv6 support (-6) add a "
						"IPv4 multicast address!");
		}
		use_multicast = true;
		hosthints.ai_flags = AI_NUMERICHOST | AI_ADDRCONFIG;
	}

	/* probe our values */
	xgetaddrinfo(hostname, opts.port, &hosthints, &hostres);

	for (addrtmp = hostres; addrtmp != NULL ; addrtmp = addrtmp->ai_next) {
		if (opts.family != AF_UNSPEC &&
			addrtmp->ai_family != opts.family) { /* user fixed family! */
			continue;
		}

		fd = socket_bind(addrtmp);
		if (fd < 0)
			continue;

		if (use_multicast) {
			switch (addrtmp->ai_family) {
			case AF_INET6:
				set_muticast6(fd, &mreq6);
				break;
			case AF_INET:
				set_muticast4(fd, &mreq);
				break;
			default:
				err_msg_die(EXIT_FAILINT, "Programmed Failure");
			}
		}
		break;
	}

	if (fd < 0)
		err_msg_die(EXIT_FAILNET, "Don't found a suitable address for binding, giving up "
				"(TIP: start program with strace(2) to find the problen\n");

	ret = sock_callbacks.cb_listen(fd, BACKLOG);
	if (ret < 0)
		err_sys_die(EXIT_FAILNET, "listen(fd: %d, backlog: %d) failed", fd, BACKLOG);

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
	int ret, file_fd, connected_fd = -1, server_fd;
	struct sockaddr_storage sa;
	socklen_t sa_len = sizeof sa;
	struct sigaction sigterm_sa;

	sigterm_sa.sa_handler = SIG_IGN;
	sigemptyset(&sigterm_sa.sa_mask);
	sigterm_sa.sa_flags = 0;
	sigaction(SIGPIPE, &sigterm_sa, NULL);

	msg(GENTLE, "receiver mode");

	file_fd = open_output_file();

	connected_fd = server_fd = instigate_cs();

#ifdef HAVE_AF_TIPC
	if (opts.family == AF_TIPC) {
		connected_fd = tipc_accept(server_fd, (struct sockaddr *) &sa, &sa_len);
		if (connected_fd == -1)
			err_sys_die(EXIT_FAILNET, "accept");
	}
#endif
	switch (opts.protocol) {
	case IPPROTO_TCP:
	case IPPROTO_DCCP:
	case IPPROTO_SCTP: {
		char peer[1024];

		connected_fd = accept(server_fd, (struct sockaddr *) &sa, &sa_len);
		if (connected_fd == -1) {
			err_sys("accept error");
			exit(EXIT_FAILNET);
		}
		ret = getnameinfo((struct sockaddr *)&sa, sa_len, peer,
				sizeof(peer), NULL, 0, 0);
		if (ret != 0)
			err_msg_die(EXIT_FAILNET, "getnameinfo error: %s",  gai_strerror(ret));
		msg(GENTLE, "accept from %s", peer);
		}
	}

	/* read netsend header */
	meta_exchange_rcv(connected_fd);

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	msg(LOUDISH, "block in read");

	cs_read(file_fd, connected_fd);

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
