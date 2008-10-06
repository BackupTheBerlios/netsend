/*
 * netsend - a file transfer and diagnostic tool
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

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "debug.h"
#include "global.h"
#include "error.h"
#include "xfuncs.h"
#include "proto_unix.h"

extern struct net_stat net_stat;
extern struct opts opts;
extern struct sock_callbacks sock_callbacks;

static int create_unixsock(struct opts *optsp, struct sockaddr_un *sa_un)
{
	size_t len;
	int fd;

	memset(sa_un, 0, sizeof(*sa_un));

	sa_un->sun_family = AF_UNIX;

	fd = socket(AF_UNIX, optsp->socktype, 0);
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "socket");

	assert(optsp->port);
	len = strlen(optsp->port);
	if (len >= sizeof(sa_un->sun_path))
		err_msg_die(EXIT_FAILOPT, "AF_UNIX path name too long (%u chars, must be < %u",
							len, sizeof(sa_un->sun_path));
	strcpy(sa_un->sun_path, optsp->port);
	sock_callbacks.cb_accept = unix_accept;
	sock_callbacks.cb_listen = unix_listen;
	return fd;
}

int unix_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int fd;

	switch (opts.socktype) {
	case SOCK_RDM: case SOCK_DGRAM:
		return sockfd;
	case SOCK_STREAM: case SOCK_SEQPACKET:
		fd = accept(sockfd, addr, addrlen);
		if (fd < 0)
			return fd;
		return fd;
	}
	return -1;
}

int unix_listen(int sockfd, int log)
{
	if (opts.socktype == SOCK_DGRAM)
		return 0;
	return listen(sockfd, log);
}

int unix_socket_bind(void)
{
	struct sockaddr_un sa_un;
	int fd = create_unixsock(&opts, &sa_un);

	if (bind(fd, (struct sockaddr*)  &sa_un, sizeof(sa_un)))
		err_sys_die(EXIT_FAILNET, "bind");
	return fd;
}


static int init_unix_trans(struct opts *optsp)
{
	struct sockaddr_un sa_un;
	int fd, ret;

	fd = create_unixsock(optsp, &sa_un);
	ret = connect(fd, (struct sockaddr*) &sa_un, sizeof(sa_un));
	if (ret == -1)
		err_sys_die(EXIT_FAILNET, "Can't connect to %s", opts.port);

	msg(LOUDISH, "socket connected to %s", opts.port);
	return fd;
}

void
unix_trans_mode(struct opts *optsp)
{
	int connected_fd, file_fd;

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_unix_trans(optsp);

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);
	trans_start(file_fd, connected_fd);
}

/* vim:set ts=4 sw=4 tw=78 noet: */
