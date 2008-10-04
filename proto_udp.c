/*
 * udp/udplite functions.
 * http://netsend.berlios.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <limits.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "global.h"
#include "xfuncs.h"

#include "proto_udp.h"

int udp_listen(int sockfd __attribute__((unused)),
				int ignored __attribute__((unused)))
{
	return 0;
}

static void udplite_setsockopt_sendrecv_csov(int connected_fd, uint16_t coverage, int recv_send, const char *str)
{
	int ret, i = coverage;

	msg(GENTLE, "set UDPLite checksum coverage for %d bytes", i);

	ret = setsockopt(connected_fd, IPPROTO_UDPLITE, recv_send, &i, sizeof(int));
	if (ret)
		err_sys("setsockopt option %s failed", str);
}

static void udplite_setsockopt_send_csov(int connected_fd, uint16_t coverage)
{
	udplite_setsockopt_sendrecv_csov(connected_fd, coverage, UDPLITE_SEND_CSCOV, "UDPLITE_SEND_CSCOV");
}

void udplite_setsockopt_recv_csov(int connected_fd, uint16_t coverage)
{
	udplite_setsockopt_sendrecv_csov(connected_fd, coverage, UDPLITE_RECV_CSCOV, "UDPLITE_RECV_CSCOV");
}


/* Creates our server socket and initialize
** options
*/
static int init_udp_trans(const struct opts *optsp)
{
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;
	struct protoent *protoent;

	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = optsp->family;
	hosthints.ai_socktype = optsp->socktype;
	hosthints.ai_protocol = optsp->protocol;
	hosthints.ai_flags    = AI_ADDRCONFIG;

	xgetaddrinfo(optsp->hostname, optsp->port, &hosthints, &hostres);

	addrtmp = hostres;

	for (addrtmp = hostres; addrtmp != NULL ; addrtmp = addrtmp->ai_next) {

		if (optsp->family != AF_UNSPEC &&
			addrtmp->ai_family != optsp->family) { /* user fixed family! */
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

		if (optsp->protocol == IPPROTO_UDPLITE && optsp->udplite_checksum_coverage != LONG_MAX)
			udplite_setsockopt_send_csov(fd, optsp->udplite_checksum_coverage);
		set_socketopts(fd);

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
			err_sys_die(EXIT_FAILNET, "Can't connect to %s", optsp->hostname);

		msg(LOUDISH, "socket connected to %s via port %s",
			optsp->hostname, optsp->port);
	}

	if (fd < 0)
		err_msg_die(EXIT_FAILNET, "No suitable socket found");

	freeaddrinfo(hostres);
	return fd;
}

extern struct net_stat net_stat;
/*
** o initialize server socket
** o fstat and open our sending-file
** o block in socket and wait for client
** o sendfile(2), write(2), ...
** o print diagnostic info
*/
void
udp_trans_mode(struct opts *optsp)
{
	int connected_fd, file_fd;

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_udp_trans(optsp);

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	trans_start(file_fd, connected_fd);
}

/* vim:set ts=4 sw=4 tw=78 noet: */
