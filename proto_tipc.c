/*
 * $Id$
 *
 * netsend - a high performance filetransfer and diagnostic tool
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "global.h"
#include "debug.h"
#include "proto_tipc.h"
#include "xfuncs.h"


extern struct opts opts;
extern struct sock_callbacks sock_callbacks;
#ifdef HAVE_AF_TIPC

#define NETSEND_TIPC_SERVER	231337

/*
 * TIPC does not support connect() for SOCK_DGRAM and SOCK_RDM at this time,
 * so we need to use sendto instead of write
 */
static struct sockaddr_tipc *sendto_dest_addr;

static struct sockaddr_tipc sa_tipc_get(void)
{
	struct sockaddr_tipc sa_tipc = { .family = AF_TIPC,.scope = TIPC_ZONE_SCOPE };

	assert(opts.family == AF_TIPC);

	sa_tipc.addr.nameseq.type = NETSEND_TIPC_SERVER;
	return sa_tipc;
}


int tipc_socket_bind(void)
{
	int fd;
	struct sockaddr_tipc sa_tipc = sa_tipc_get();

	sa_tipc.addrtype = TIPC_ADDR_NAMESEQ;

	fd = socket(AF_TIPC, opts.socktype, opts.protocol);
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "socket");

	if (bind(fd, (struct sockaddr*)&sa_tipc, sizeof(sa_tipc)))
		err_sys_die(EXIT_FAILNET, "bind");

	sock_callbacks.cb_listen = tipc_listen;
	switch (opts.socktype) {
	case SOCK_RDM: case SOCK_DGRAM:
		assert(sendto_dest_addr == NULL);
		sendto_dest_addr = xmalloc(sizeof(*sendto_dest_addr));
		*sendto_dest_addr = sa_tipc;
		sock_callbacks.cb_write = tipc_write;
	}
	return fd;
}


int tipc_socket_connect(void)
{
	int fd;
	struct sockaddr_tipc sa_tipc = sa_tipc_get();

	sa_tipc.addrtype = TIPC_ADDR_NAME;

	fd = socket(AF_TIPC, opts.socktype, opts.protocol);
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "socket");

	switch (opts.socktype) {
	case SOCK_STREAM: case SOCK_SEQPACKET:
		if (connect(fd, (struct sockaddr*)&sa_tipc, sizeof(sa_tipc)))
			err_sys_die(EXIT_FAILNET, "connect");
		return fd;
	case SOCK_RDM: case SOCK_DGRAM:
		assert(sendto_dest_addr == NULL);
		sendto_dest_addr = xmalloc(sizeof(*sendto_dest_addr));
		*sendto_dest_addr = sa_tipc;
		sock_callbacks.cb_write = tipc_write;
		sock_callbacks.cb_accept = tipc_accept;
		return fd;
	}
	return -1;
}


int tipc_listen(int sockfd, int log)
{
	assert(opts.family == AF_TIPC);

	switch (opts.socktype) {
	case SOCK_RDM: case SOCK_DGRAM:
		return 0;
	case SOCK_STREAM: case SOCK_SEQPACKET:
		return listen(sockfd, log);
	}
	return -1;
}


int tipc_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	assert(opts.family == AF_TIPC);

	switch (opts.socktype) {
	case SOCK_RDM: case SOCK_DGRAM:
		return sockfd;
	case SOCK_STREAM: case SOCK_SEQPACKET:
		return accept(sockfd, addr, addrlen);
	}
	return -1;
}


ssize_t tipc_write(int sockfd, const void *buf, size_t buflen)
{
	assert(sendto_dest_addr != NULL);

	return sendto(sockfd, buf, buflen, 0, (struct sockaddr*)sendto_dest_addr, (socklen_t) sizeof(*sendto_dest_addr));
}
#endif
/* vim:set ts=4 sw=4 tw=78 noet: */
