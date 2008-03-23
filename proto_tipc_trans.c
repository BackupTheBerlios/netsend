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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "debug.h"
#include "global.h"
#include "xfuncs.h"
#include "proto_tipc.h"

extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t io_call_map[];
extern struct sock_callbacks sock_callbacks;

/* Creates our server socket and initialize
** options
*/
static int init_tipc_trans(void)
{
	int fd;

	assert(opts.family == AF_TIPC);

	fd = tipc_socket_connect();
	if (fd < 0)
		err_sys_die(EXIT_FAILNET, "tipc_socket_connect");

	return fd;
}


/*
** o initialize server socket
** o fstat and open our sending-file
** o block in socket and wait for client
** o sendfile(2), write(2), ...
** o print diagnostic info
*/
void tipc_trans_mode(void)
{
	int connected_fd, file_fd;

	msg(GENTLE, "transmit mode (file: %s  -  hostname: %s)",
		opts.infile, opts.hostname);

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_tipc_trans();

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	trans_start(file_fd, connected_fd);

	gettimeofday(&opts.endtime, NULL);
}

/* vim:set ts=4 sw=4 tw=78 noet: */
