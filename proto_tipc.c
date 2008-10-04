/*
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
extern struct net_stat net_stat;
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


static int tipc_socket_connect(void)
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

void tipc_log_sockaddr(int level, const struct sockaddr_tipc *s)
{
	uint32_t a;

	switch (s->addrtype) {
	case TIPC_ADDR_NAMESEQ:
		msg(level, "NAMESEQ, type %u (%u:%u)",
			s->addr.nameseq.type, s->addr.nameseq.lower, s->addr.nameseq.upper);
	break;
	case TIPC_ADDR_NAME:
		msg(level, "ADDR_NAME type %u instance %u",
			s->addr.name.name.type, s->addr.name.name.instance);
	break;
	case TIPC_ADDR_ID:
		a = s->addr.id.node;
		msg(level, "ADDR_ID %d.%d.%d ref %u",
			tipc_zone(a), tipc_cluster(a), tipc_node(a), s->addr.id.ref);
	break;
	default:
		err_msg("Warning: Unrecognized TIPC addrtype %d", (int) s->addrtype);
	break;
	}
}

ssize_t tipc_write(int sockfd, const void *buf, size_t buflen)
{
	assert(sendto_dest_addr != NULL);

	return sendto(sockfd, buf, buflen, 0, (struct sockaddr*)sendto_dest_addr, (socklen_t) sizeof(*sendto_dest_addr));
}


static int init_tipc_trans(void)
{
	int fd;

	assert(opts.family == AF_TIPC);

	fd = tipc_socket_connect();
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "tipc_socket_connect");

	return fd;
}
#endif

void tipc_trans_mode(void)
{
#ifdef HAVE_AF_TIPC
	int connected_fd, file_fd;

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_tipc_trans();

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	trans_start(file_fd, connected_fd);
#else
	err_msg_die(EXIT_FAILMISC, "TIPC support not compiled in");
#endif
}

/* vim:set ts=4 sw=4 tw=78 noet: */
