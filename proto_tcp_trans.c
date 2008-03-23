/*
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

#include "config.h"

#define _XOPEN_SOURCE 600	/* needed for posix_madvise/fadvise */
#include <sys/mman.h>
#include <fcntl.h>
#undef _XOPEN_SOURCE

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "debug.h"
#include "global.h"
#include "xfuncs.h"
#include "proto_tipc.h"
#include "tcp_md5sig.h"

extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];
extern struct sock_callbacks sock_callbacks;


static void tcp_set_socketopts(int fd, const struct sockaddr *sa)
{
	if (opts.tcp_use_md5sig) {
		static const char key[] = "netsend";
		struct tcp_md5sig sig = { .tcpm_keylen = sizeof(key) };

		memcpy(sig.tcpm_key, key, sizeof(key));

		memcpy(&sig.tcpm_addr, (const struct sockaddr_storage *) sa,
				min(sizeof(sig.tcpm_addr), sizeof(*sa)));

		xsetsockopt(fd, IPPROTO_TCP, TCP_MD5SIG, &sig, sizeof(sig), "TCP_MD5SIG");
	}

	set_socketopts(fd);
}


/* Creates our server socket and initialize
** options
*/
static int init_tcp_trans(void)
{
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;
	struct protoent *protoent;

#ifdef HAVE_AF_TIPC
	if (opts.family == AF_TIPC) {
		fd = tipc_socket_connect();
		if (fd < 0)
			err_sys_die(EXIT_FAILNET, "tipc_socket_connect");

		return fd;
	}
#endif
	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
	hosthints.ai_flags    = AI_ADDRCONFIG;

	xgetaddrinfo(opts.hostname, opts.port, &hosthints, &hostres);

	addrtmp = hostres;

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

		protoent = getprotobynumber(addrtmp->ai_protocol);
		if (protoent)
			msg(LOUDISH, "socket created - protocol %s(%d)",
				protoent->p_name, protoent->p_proto);

		/* mulicast checks */
		if (addrtmp->ai_protocol == IPPROTO_UDP) {
			switch (addrtmp->ai_family) {
				case AF_INET6:
					if (IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6 *)
									addrtmp->ai_addr)->sin6_addr)) {
						use_multicast = true;
					}
					break;
				case AF_INET:
					if (IN_MULTICAST(ntohl(((struct sockaddr_in *)
									addrtmp->ai_addr)->sin_addr.s_addr))) {
						use_multicast = true;
					}
					break;
				default:
					err_msg_die(EXIT_FAILINT, "Programmed Failure");
			}
		}

		if (use_multicast) {
			int hops_ttl = 30;
				int on = 1;
			switch (addrtmp->ai_family) {
				case AF_INET6:
					xsetsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (char *)&hops_ttl,
								sizeof(hops_ttl), "IPV6_MULTICAST_HOPS");
					xsetsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
							&on, sizeof(int), "IPV6_MULTICAST_LOOP");
					break;
				case AF_INET:
					xsetsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
					         (char *)&hops_ttl, sizeof(hops_ttl), "IP_MULTICAST_TTL");

					xsetsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
							&on, sizeof(int), "IP_MULTICAST_LOOP");
					msg(STRESSFUL, "set IP_MULTICAST_LOOP option");
					break;
				default:
					err_msg_die(EXIT_FAILINT, "Programmed Failure");
			}
		}

		tcp_set_socketopts(fd, addrtmp->ai_addr);

		/* Connect to peer
		** There are three advantages to call connect for all types
		** of our socket protocols (especially udp)
		**
		** 1. We don't need to specify a destination address (only call write)
		** 2. Performance advantages (kernel level)
		** 3. Error detection (e.g. destination port unreachable at udp)
		*/
		ret = connect(fd, addrtmp->ai_addr, addrtmp->ai_addrlen);
		if (ret == -1)
			err_sys_die(EXIT_FAILNET, "Can't connect to %s", opts.hostname);

		msg(LOUDISH, "socket connected to %s via port %s",
			opts.hostname, opts.port);
	}

	if (fd < 0)
		err_msg_die(EXIT_FAILNET, "No suitable socket found");

	freeaddrinfo(hostres);
	return fd;
}


/* *** Main Server Routine ***
**
** o initialize server socket
** o fstat and open our sending-file
** o block in socket and wait for client
** o sendfile(2), write(2), ...
** o print diagnostic info
*/
void tcp_trans_mode(void)
{
	int connected_fd, file_fd, child_status;
	struct sigaction sa;

	msg(GENTLE, "transmit mode (file: %s  -  hostname: %s)",
		opts.infile, opts.hostname);

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_tcp_trans();

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGPIPE, &sa, NULL);

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	switch (opts.io_call) {
	case IO_SENDFILE:
		trans_sendfile(file_fd, connected_fd);
		break;
	case IO_SPLICE:
		trans_splice(file_fd, connected_fd);
		break;
	case IO_MMAP:
		trans_mmap(file_fd, connected_fd);
		break;
	case IO_RW:
		trans_rw(file_fd, connected_fd);
		break;
	default:
		err_msg_die(EXIT_FAILINT, "Programmed Failure");
	}

	gettimeofday(&opts.endtime, NULL);

	/* if we spawn a child - reaping it here */
	waitpid(-1, &child_status, 0);
}

/* vim:set ts=4 sw=4 tw=78 noet: */
