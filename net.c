/*
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

#include "proto_tipc.h"
#include "global.h"

extern struct opts opts;
extern struct socket_options socket_options[];


/* set TCP_NODELAY opption on socket
** return the previous value (0, 1) or
** -1 if a error occur
*/
int
set_nodelay(int fd, int flag)
{
	int ret = 0; socklen_t ret_size;

	if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ret, &ret_size) < 0)
		return -1;

	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
		return -1;

	return ret;
}


static int
get_ip_sock_opts(int fd, struct net_stat *ns)
{
	(void) fd;
	(void) ns;

	return 0;
}


static int
get_tcp_sock_opts(int fd, struct net_stat *ns)
{
	int ret;
	socklen_t len;

	/* NOTE:
	** ipv4/tcp.c:tcp_getsockopt() returns
	** tp->mss_cache_std;
	** if (!val && ((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN)))
	**     val = tp->rx_opt.user_mss;
	*/
	len = sizeof(ns->sock_stat.mss);
	ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &ns->sock_stat.mss, &len);
	if ((ret == -1) || (ns->sock_stat.mss <= 0)) {
		fprintf(stderr, "Can't determine mss for socket (mss: %d): %s "
				"(fall back to 1500 bytes)\n", ns->sock_stat.mss, strerror(errno));
		ns->sock_stat.mss = 1500;
	}

	return 0;
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
#ifdef HAVE_AF_TIPC
	if (opts.family == AF_TIPC)
		return 0;
#endif
	ret = get_ip_sock_opts(fd, ns);
	if (ret != 0) {
		return ret;
	}

	switch (opts.protocol) {
		case IPPROTO_TCP:
			return get_tcp_sock_opts(fd, ns);
			break;
		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_DCCP:
		case IPPROTO_SCTP:
			return 0;
		default:
			err_msg_die(EXIT_FAILMISC, "Programmed Failure");
	}

	return 0;
}


/*
 * performs all socketopts specified, except
 * for some highly protocol dependant options (e.g. TCP_MD5SIG).
 */
void set_socketopts(int fd)
{
	int i, ret;
	const void *optval;
	socklen_t optlen;

	/* loop over all selectable socket options */
	for (i = 0; socket_options[i].sockopt_name; i++) {
		if (!socket_options[i].user_issue)
			continue;
		/*
		 * this switch statement checks that the particular
		 * socket option matches our selected socket-type
		 */
		switch (socket_options[i].level) {
		case SOL_SOCKET: break; /* works on every socket */
		/* fall-through begins here ... */
		case IPPROTO_TCP:
			if (opts.protocol == IPPROTO_TCP)
				break;
		case IPPROTO_UDP:
			if (opts.protocol == IPPROTO_UDP)
				break;
		case IPPROTO_UDPLITE:
			if (opts.protocol == IPPROTO_UDPLITE)
				break;
		case IPPROTO_SCTP:
			if (opts.protocol == IPPROTO_SCTP)
				break;
		case SOL_DCCP:
			if (opts.protocol == IPPROTO_DCCP)
				break;
		default:
		/* and exit if socketoption and sockettype did not match */
		err_msg_die(EXIT_FAILMISC, "You selected an socket option which isn't "
					"compatible with this particular socket option");
		}

		/* ... and do the dirty: set the socket options */
		switch (socket_options[i].sockopt_type) {
		case SVT_BOOL:
		case SVT_INT:
			optlen = sizeof(socket_options[i].value);
			optval = &socket_options[i].value;
		break;
		case SVT_TIMEVAL:
			optlen = sizeof(socket_options[i].tv);
			optval = &socket_options[i].tv;
		break;
		case SVT_STR:
			optlen = strlen(socket_options[i].value_ptr) + 1;
			optval = socket_options[i].value_ptr;
		break;
		default:
			err_msg_die(EXIT_FAILNET, "Unknown sockopt_type %d\n",
					socket_options[i].sockopt_type);
		}
		ret = setsockopt(fd, socket_options[i].level, socket_options[i].option, optval, optlen);
		if (ret)
			err_sys("setsockopt option %d (name %s) failed", socket_options[i].sockopt_type,
										socket_options[i].sockopt_name);
	}
}



