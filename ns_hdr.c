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
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>

#include "global.h"
#include "debug.h"

extern struct opts opts;

static ssize_t
writen(int fd, const void *buf, size_t len)
{
	const char *bufptr = buf;
	ssize_t total = 0;
	do {
		ssize_t written = write(fd, bufptr, len);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		total += written;
		bufptr += written;
		len -= written;
	} while (len > 0);

	return total > 0 ? total : -1;
}

/* read exactly buflen bytes; only return on fatal error */
static ssize_t readn(int fd, void *buf, size_t buflen)
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

/* This is the plan:
** send n rtt packets into the wire and wait until all n reply packets
** arrived. If a timeout occur we count this packet as lost
*/
static int
probe_rtt(int peer_fd, int next_hdr, int probe_no, uint16_t backing_data_size)
{
	uint16_t seq = 0; int i, current_next_hdr;
	uint16_t packet_len; ssize_t to_write;
	char rtt_buf[backing_data_size + sizeof(struct ns_rtt)];
	struct ns_rtt *ns_rtt = (struct ns_rtt *) rtt_buf;
	char *data_ptr = rtt_buf + sizeof(struct ns_rtt);

	if (probe_no <= 0)
		err_msg_die(EXIT_FAILINT, "Programmed error");

	memset(ns_rtt, 0, sizeof(struct ns_rtt));
	memset(data_ptr, 'A', backing_data_size);

	/* packet backing data MUST a multiple of four */
	packet_len = ((uint16_t)(backing_data_size / 4)) * 4;
	to_write = packet_len + sizeof(struct ns_rtt);

	/* we announce packetsize in 4 byte slices (32bit)
	** minus nse_nxt_hdr and nse_len header (4 byte)
	*/
	ns_rtt->nse_len = htons((to_write - 4) / 4);

	ns_rtt->ident = htons(getpid() & 0xffff);

	current_next_hdr = NSE_NXT_RTT;

	for (i = 0; i < probe_no; ) {

		char reply_buf[to_write];
		struct ns_rtt *ns_rtt_reply;
		ssize_t to_read = to_write;

		if (++i >= probe_no)
			current_next_hdr = next_hdr;

		ns_rtt->nse_nxt_hdr = htons(current_next_hdr);
		ns_rtt->type = (htons((uint16_t)RTT_REQUEST_TYPE));
		ns_rtt->seq_no = htons(seq++);
		ns_rtt->timestamp = htonl(666); /* gettimeofday() */

		/* transmitt rtt probe ... */
		if (writen(peer_fd, ns_rtt, to_write) != to_write)
			err_msg_die(EXIT_FAILHEADER, "Can't send rtt extension header!\n");

		/* ... and receive probe */
		if (readn(peer_fd, reply_buf, to_read) != to_read)
			return -1;

		ns_rtt_reply = (struct ns_rtt *) reply_buf;

		if (ntohs(ns_rtt_reply->ident) != (getpid() & 0xffff))
			err_msg("received a unknown rtt probe reply (ident  should: %d is: %d)",
					ntohs(ns_rtt_reply->ident),  (getpid() & 0xffff));
		msg(STRESSFUL, "receive rtt reply probe (sequence: %d, len %d)",
				ntohs(ns_rtt_reply->seq_no), to_read);
	}

	return 0;
}


#define	TIMEOUT_SEC 10

static void
timout_handler(int sig_no)
{
	if (sig_no != SIGALRM)
		err_msg_die(EXIT_FAILINT, "Programmed error (received an unknow signal)");

	alarm(TIMEOUT_SEC);

	msg(STRESSFUL, "timout occure while wait for rtt probe response");
}


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


	/* FIXME: the code till the end in this function is alpha, there need some
	** glue around it like a nice next header handling
	*/
	ns_hdr.nse_nxt_hdr = htons(NSE_NXT_RTT);

	len = sizeof(struct ns_hdr);
	if (writen(connected_fd, &ns_hdr, len) != len)
		err_msg_die(EXIT_FAILHEADER, "Can't send netsend header!\n");

	/* FIXME: add a commandline argument */
	if (1) {

		struct sigaction sa;

		/* initialize signalhandler for timeout handling */
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_INTERRUPT;	/* don't restart system calls */
		sa.sa_handler = timout_handler;
		if (sigaction(SIGALRM, &sa, NULL) != 0)
			err_sys("Can't add signal handler");

		alarm(TIMEOUT_SEC);

		probe_rtt(connected_fd, NSE_NXT_DATA, 10, 500);

		alarm(0);
	}

	/* XXX: add shasum next header if opts.sha, modify nse_nxt_hdr processing */

	return ret;
}

/* probe_rtt read a rtt probe packet, set nse_nxt_hdr to zero,
** nse_len to the current packet size and send it back to origin
*/
static int
process_rtt(int peer_fd, uint16_t nse_len)
{
	int ret = 0; uint16_t *intptr;
	struct ns_rtt *ns_rtt_ptr;
	char buf[nse_len * 4 + 4];
	ssize_t to_read = nse_len * 4;


	if (readn(peer_fd, buf + 4, to_read) != to_read)
		return -1;

	ns_rtt_ptr = (struct ns_rtt *) buf;

	msg(STRESSFUL, "process rtt probe (sequence: %d, type: %d packet_size: %d)",
			ntohs(ns_rtt_ptr->seq_no), ntohs(ns_rtt_ptr->type), to_read);

	ns_rtt_ptr->type = htons(RTT_REPLY_TYPE);

	intptr = (uint16_t *)buf;
	*intptr = 0;

	intptr = (uint16_t *)buf + sizeof(uint16_t);
	*intptr = htons(nse_len);

	if (writen(peer_fd, buf, to_read + 4) != to_read + 4)
		err_msg_die(EXIT_FAILHEADER, "Can't reply to rtt probe!\n");

	return ret;
}

static int
process_nonxt(int peer_fd, uint16_t nse_len)
{
	char buf[nse_len];
	ssize_t to_read = nse_len;

	if (readn(peer_fd, buf, to_read) != to_read)
		return -1;

	return 0;
}

#define	INVALID_EXT_TRESH_NO 8

/* return -1 if a failure occure, zero apart from that */
int
meta_exchange_rcv(int peer_fd)
{
	int ret;
	int invalid_ext_seen = 0;
	uint16_t extension_type, extension_size;
	unsigned char *ptr;
	ssize_t rc = 0, to_read = sizeof(struct ns_hdr);
	struct ns_hdr ns_hdr;

	memset(&ns_hdr, 0, sizeof(struct ns_hdr));

	ptr = (unsigned char *) &ns_hdr;

	msg(STRESSFUL, "fetch general header (%d byte)", sizeof(struct ns_hdr));

	/* read minimal ns header */
	if (readn(peer_fd, &ptr[rc], to_read) != to_read)
		return -1;

	/* ns header is in -> sanity checks and look if peer specified extension header */
	if (ntohs(ns_hdr.magic) != NS_MAGIC) {
		err_msg_die(EXIT_FAILHEADER, "received an corrupted header"
				"(should %d but is %d)!\n", NS_MAGIC, ntohs(ns_hdr.magic));
	}

	msg(STRESSFUL, "header info (magic: %d, version: %d, data_size: %d)",
			ntohs(ns_hdr.magic), ntohs(ns_hdr.version), ntohl(ns_hdr.data_size));


	extension_type = ntohs(ns_hdr.nse_nxt_hdr);

	if (extension_type == NSE_NXT_DATA) {
		msg(STRESSFUL, "end of extension header processing (NSE_NXT_DATA, no extension header)");
		return 0;
	}

	while (invalid_ext_seen < INVALID_EXT_TRESH_NO) {

		/* FIXME: define some header size macros */
		uint16_t common_ext_head[2];
		to_read = sizeof(uint16_t) * 2;

		/* read first 4 octets of extension header */
		if (readn(peer_fd, common_ext_head, to_read) != to_read)
			return -1;

		extension_size = ntohs(common_ext_head[1]);

		switch (extension_type) {

			case NSE_NXT_DATA:
				msg(STRESSFUL, "end of extension header processing (NSE_NXT_DATA)");
				return 0;

			case NSE_NXT_NONXT:
				msg(STRESSFUL, "end of extension header processing (NSE_NXT_NONXT)");
				return process_nonxt(peer_fd, extension_size);
				break;

			case NSE_NXT_DIGEST:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_DIGEST");
				err_msg("Not implementet yet: NSE_NXT_DIGEST\n");
				break;

			case NSE_NXT_RTT:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_RTT");
				ret = process_rtt(peer_fd, extension_size);
				if (ret == -1)
					return -1;
				break;

			default:
				++invalid_ext_seen;
				err_msg("received an unknown extension type (%d)!\n", extension_type);
				ret = process_nonxt(peer_fd, extension_size);
				if (ret == -1)
					return -1;
				break;
		}


		extension_type = ntohs(common_ext_head[0]);

	};

	/* failure if we reach here (failure in previous while loop */
	return -1;
}

#undef INVALID_EXT_TRESH_NO



/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
