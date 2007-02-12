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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "global.h"
#include "debug.h"

extern struct opts opts;

int
send_ns_hdr(int connected_fd, int file_fd)
{
	int ret = 0;
	ssize_t len;
	ssize_t file_size;
	struct ns_hdr ns_hdr;
	struct stat stat_buf;

	memset(&ns_hdr, 0, sizeof(struct ns_hdr));

	/* fetch file size */
	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		err_sys("Can't fstat file %s", opts.infile);
		exit(EXIT_FAILMISC);
	}

	file_size = S_ISREG(stat_buf.st_mode) ? stat_buf.st_size : 0;


	ns_hdr.magic = htons(NS_MAGIC);
	ns_hdr.version = htonl(strtol(VERSIONSTRING, (char **)NULL, 10));
	ns_hdr.data_size = htonl(file_size);

	ns_hdr.nse_nxt_hdr = htons(NSE_NXT_DATA);

	len = write_len(connected_fd, &ns_hdr, sizeof(struct ns_hdr));
	if (len != sizeof(struct ns_hdr))
		err_msg_die(EXIT_FAILNET, "Can't send netsend header!\n");

	/* XXX: add shasum next header if opts.sha, modify nse_nxt_hdr processing */

	return ret;
}

int
read_ns_hdr(int peer_fd)
{
	int ret = 0;
	int extension_type;
	unsigned char *ptr;
	ssize_t rc = 0, to_read = sizeof(struct ns_hdr);
	struct ns_hdr ns_hdr;

	memset(&ns_hdr, 0, sizeof(struct ns_hdr));

	ptr = (unsigned char *) &ns_hdr;

	/* read minimal ns header */
	while ((rc += read(peer_fd, &ptr[rc], to_read)) > 0) {
		to_read -= rc;
	}

	/* ns header in, sanity checks and
	** look if peer specified extension
	** header */
	if (ntohs(ns_hdr.magic) != NS_MAGIC) {
		err_msg_die(EXIT_FAILMISC, "Netsend received an corrupted header"
				"(should %d but is %d)!\n", NS_MAGIC, ntohs(ns_hdr.magic));
	}

	extension_type = ntohs(ns_hdr.nse_nxt_hdr);

	switch (extension_type) {

		case NSE_NXT_DATA:
		case NSE_NXT_NONXT:
			return 0;
			break;

		case NSE_NXT_DIGEST:
			err_msg("Not implementet yet: NSE_NXT_DIGEST\n");
			break;

		default:
			err_msg_die(EXIT_FAILINT, "Received an unknown extension type (%d)!\n",
					extension_type);
			break;
	}

	return ret;
}



/* vim:set ts=4 sw=4 tw=78 noet: */

