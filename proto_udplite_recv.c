/*
** netsend - a high performance filetransfer and diagnostic tool
** http://netsend.berlios.de
**
** Copyright (C) 2008 - Hagen Paul Pfeifer
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

#include <limits.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "global.h"
#include "xfuncs.h"

int init_receive_socket_udplite(struct opts *optsp, int connected_fd)
{
	int ret = SUCCESS, i;

	/* check if there was a command line option to enforce
	 * udplite header checksum */
	if (optsp->udplite_checksum_coverage != LONG_MAX) {
		i = optsp->udplite_checksum_coverage;

		msg(GENTLE, "set UDPLite checksum coverage for %d bytes", i);

		ret = setsockopt(connected_fd, IPPROTO_UDPLITE, UDPLITE_RECV_CSCOV, &i, sizeof(int));
		if (ret) {
			err_sys("setsockopt option %d (name UDPLITE_RECV_CSCOV) failed",
				UDPLITE_RECV_CSCOV);
			return FAILURE;
		}
	}
	return ret;
}

/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
