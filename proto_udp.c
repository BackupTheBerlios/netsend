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

void udplite_setsockopt_send_csov(int connected_fd, uint16_t coverage)
{
	udplite_setsockopt_sendrecv_csov(connected_fd, coverage, UDPLITE_SEND_CSCOV, "UDPLITE_SEND_CSCOV");
}

void udplite_setsockopt_recv_csov(int connected_fd, uint16_t coverage)
{
	udplite_setsockopt_sendrecv_csov(connected_fd, coverage, UDPLITE_RECV_CSCOV, "UDPLITE_RECV_CSCOV");
}

