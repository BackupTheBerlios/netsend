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
meta_exchange_snd(int connected_fd, int file_fd)
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
	/* FIXME: handle overflow */
	ns_hdr.version = htons((uint16_t) strtol(VERSIONSTRING, (char **)NULL, 10));
	ns_hdr.data_size = htonl(file_size);

	ns_hdr.nse_nxt_hdr = htons(NSE_NXT_DATA);

	len = write_len(connected_fd, &ns_hdr, sizeof(struct ns_hdr));
	if (len != sizeof(struct ns_hdr))
		err_msg_die(EXIT_FAILHEADER, "Can't send netsend header!\n");

	/* XXX: add shasum next header if opts.sha, modify nse_nxt_hdr processing */

	return ret;
}


/* read exactly buflen bytes; only return on fatal error */
static ssize_t read_len(int fd, void *buf, size_t buflen)
{
	char *bufptr = buf;
	ssize_t total = 0;
	do {
		ssize_t ret = read(fd, bufptr, buflen);
		switch (ret) {
		case -1:
			if (errno == EINTR)
			continue;
			/* fallthru */
		case 0: goto out;
		}

		total += ret;
		bufptr += ret;
		buflen -= ret;
	} while (buflen > 0);
 out:
	return total > 0 ? total : -1;
}


int
meta_exchange_rcv(int peer_fd)
{
	int ret = 0;
	int invalid_ext_thresh = 8;
	int extension_type;
	unsigned char *ptr;
	ssize_t rc = 0, to_read = sizeof(struct ns_hdr);
	struct ns_hdr ns_hdr;

	memset(&ns_hdr, 0, sizeof(struct ns_hdr));

	ptr = (unsigned char *) &ns_hdr;

	msg(STRESSFUL, "fetch general header (%d byte)", sizeof(struct ns_hdr));

	/* read minimal ns header */
	if (read_len(peer_fd, &ptr[rc], to_read) != to_read)
		return -1;

	/* ns header is in -> sanity checks and look if peer specified extension header */
	if (ntohs(ns_hdr.magic) != NS_MAGIC) {
		err_msg_die(EXIT_FAILHEADER, "received an corrupted header"
				"(should %d but is %d)!\n", NS_MAGIC, ntohs(ns_hdr.magic));
	}

	msg(STRESSFUL, "header info (magic: %d, version: %d, data_size: %d)",
			ntohs(ns_hdr.magic), ntohs(ns_hdr.version), ntohl(ns_hdr.data_size));

	if (ntohs(ns_hdr.nse_nxt_hdr) == NSE_NXT_DATA)
		return 0;

	while (invalid_ext_thresh > 0) {

		uint16_t common_ext_head[2];
		rc = 0, to_read = sizeof(uint16_t) * 2;

		/* read first 4 octects of extension header */
		if (read_len(peer_fd, &common_ext_head[rc], to_read) != to_read)
			return -1;

		extension_type = ntohs(common_ext_head[0]);

		switch (extension_type) {

			case NSE_NXT_DATA:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_DATA");
				return 0;

			case NSE_NXT_NONXT:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_NONXT");
				return 0;
				break;

			case NSE_NXT_DIGEST:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_DIGEST");
				err_msg("Not implementet yet: NSE_NXT_DIGEST\n");
				break;

			default:
				invalid_ext_thresh--;
				err_msg("received an unknown extension type (%d)!\n", extension_type);
				/* read extension header to /dev/null/ */
				break;
		}
	};

	return ret;
}



/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */

