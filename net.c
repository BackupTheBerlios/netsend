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
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "global.h"

inline void
xgetaddrinfo(const char *node, const char *service,
		struct addrinfo *hints, struct addrinfo **res)
{
	int ret;

	ret = getaddrinfo(node, service, hints, res);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Call to getaddrinfo() failed: %s!\n",
				(ret == EAI_SYSTEM) ?  strerror(errno) : gai_strerror(ret));
		exit(EXIT_FAILNET);
	}
}

/* get_sock_opts() appoint some socket specific
** values for further use ... (hopefully ;-)
** Values are determined by hand for the possibility
** to change something
** We should call this function after socket creation
** and at the and off our transmit/receive phase
**   --HGN
*/
int
get_sock_opts(int fd, struct net_stat *ns)
{
	int ret;
	socklen_t len;

	/* NOTE:
	** ipv4/tcp.c:tcp_getsockopt() returns
	** tp->mss_cache_std;
	** if (!val && ((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN)))
	**     val = tp->rx_opt.user_mss;
	*/
	len = sizeof(ns->mss);
	ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &ns->mss, &len);
	if ((ret == -1) || (ns->mss <= 0)) {
		fprintf(stderr, "Can't determine mss for socket (mss: %d): %s "
				"(fall back to 1500 bytes)\n", ns->mss, strerror(errno));
		ns->mss = 1500;
	}

	/* TODO:
	**
	** IP:
	**
	** SO_DEBUG
	** SO_DONTROUTE
	** SO_BROADCAST
	** SO_SNDBUF
	** SO_RCVBUF
	** SO_REUSEADDR
	** SO_KEEPALIVE
	** SO_TYPE
	** SO_ERROR
	** SO_OOBINLINE
	** SO_NO_CHECK
	** SO_PRIORITY
	** SO_LINGER
	** SO_BSDCOMPAT ;-)
	** SO_TIMESTAMP
	** SO_RCVTIMEO
	** SO_SNDTIMEO
	** SO_RCVLOWAT
	** SO_SNDLOWAT
	** SO_PASSCRED
	** SO_PEERCRED
	** SO_PEERNAME
	** SO_ACCEPTCONN
	** SO_PEERSEC
	**
	** TCP:
	**
	** TCP_NODELAY
	** TCP_CORK
	** TCP_KEEPIDLE
	** TCP_KEEPINTVL
	** TCP_KEEPCNT
	** TCP_SYNCNT
	** TCP_LINGER2
	** TCP_DEFER_ACCEPT
	** TCP_WINDOW_CLAMP
	** TCP_QUICKACK
	** TCP_INFO
	*/

	return 0;
}



/* vim:set ts=4 sw=4 tw=78 noet: */
