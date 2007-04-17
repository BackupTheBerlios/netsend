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
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <arpa/inet.h>

#include "global.h"
#include "debug.h"

extern struct opts opts;
extern struct net_stat net_stat;
extern struct sock_callbacks sock_callbacks;

static ssize_t
writen(int fd, const void *buf, size_t len)
{
	const char *bufptr = buf;
	ssize_t total = 0;
	do {
		ssize_t written = sock_callbacks.cb_write(fd, bufptr, len);
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
	int i, j, current_next_hdr;
	double rtt_ms[probe_no + 1], deviation = 0, covariance = 0;
	double d_tmp = 0;
	uint16_t packet_len; ssize_t to_write;
	char rtt_buf[backing_data_size + sizeof(struct ns_rtt_probe)];
	struct ns_rtt_probe *ns_rtt_probe = (struct ns_rtt_probe *) rtt_buf;
	char *data_ptr = rtt_buf + sizeof(struct ns_rtt_probe);

	if (probe_no <= 0)
		err_msg_die(EXIT_FAILINT, "Programmed Failure");

	memset(ns_rtt_probe, 0, sizeof(struct ns_rtt_probe));
	memset(data_ptr, 'A', backing_data_size);

	/* packet backing data MUST a multiple of four */
	packet_len = ((uint16_t)(backing_data_size / 4)) * 4;
	to_write = packet_len + sizeof(struct ns_rtt_probe);

	/* we announce packetsize in 4 byte slices (32bit)
	** minus nse_nxt_hdr and nse_len header (4 byte)
	*/
	ns_rtt_probe->nse_len = htons((to_write - 4) / 4);

	ns_rtt_probe->ident = htons(getpid() & 0xffff);

	current_next_hdr = NSE_NXT_RTT_PROBE;

	/* FIXME:
	** The first rtt probe packet had nearly a rtt of additional
	** 200ms. So we ignore the first packet silently and calculate
	** rtt and variation. I think this increased measurement is caused
	** by "cold code paths"[TM], but these is to validate.
	*/
	for (i = 0; i <= probe_no; ) {

		char reply_buf[to_write];
		struct ns_rtt_probe *ns_rtt_reply;
		ssize_t to_read = to_write;
		struct timeval tv, tv_tmp, tv_res;

		if (i++ >= probe_no)
			current_next_hdr = next_hdr;

		ns_rtt_probe->nse_nxt_hdr = htons(current_next_hdr);
		ns_rtt_probe->type = (htons((uint16_t)RTT_REQUEST_TYPE));
		ns_rtt_probe->seq_no = htons(i);

		/* set timeval for packet */
		if (gettimeofday(&tv, NULL) != 0)
			err_sys("Can't call gettimeofday");

		ns_rtt_probe->sec = htonl(tv.tv_sec);
		ns_rtt_probe->usec = htonl(tv.tv_usec);

		/* transmitt rtt probe ... */
		if (writen(peer_fd, ns_rtt_probe, to_write) != to_write)
			err_msg_die(EXIT_FAILHEADER, "Can't send rtt extension header!\n");

		/* ... and receive probe */
		if (readn(peer_fd, reply_buf, to_read) != to_read)
			return -1;

		if (gettimeofday(&tv, NULL) != 0)
			err_sys("Can't call gettimeofday");

		ns_rtt_reply = (struct ns_rtt_probe *) reply_buf;

		tv_tmp.tv_sec = ntohl(ns_rtt_reply->sec);
		tv_tmp.tv_usec = ntohl(ns_rtt_reply->usec);

		subtime(&tv_tmp, &tv, &tv_res);

		/* sanity check (ident) */
		if (ntohs(ns_rtt_reply->ident) != (getpid() & 0xffff))
			err_msg("received a unknown rtt probe reply (ident  should: %d is: %d)",
					ntohs(ns_rtt_reply->ident),  (getpid() & 0xffff));

		/* XXX: if you change something here, then check all counter
		** variables, etc
		*/
		if (i == 1)
			continue;

		rtt_ms[i - 2] = (tv_res.tv_sec * 1000) + ((double)tv_res.tv_usec / 1000);

		msg(STRESSFUL, "receive rtt reply probe (sequence: %d, len %d, rtt: %.3fms)",
				ntohs(ns_rtt_reply->seq_no), to_read, rtt_ms[i - 2]);

	}

	/* average */
	for (j = 0; j < probe_no; j++)
		net_stat.rtt_probe.usec += rtt_ms[j];

	net_stat.rtt_probe.usec /= --j;

	/* ... covariance and standard deviation */
	for (j = 0; j < probe_no; j++)
		covariance += pow(rtt_ms[j] - net_stat.rtt_probe.usec, 2);

	covariance /= --j;
	deviation = sqrt(covariance);

	d_tmp = 0;

	/* low and high pass deviation based filter, calculates new rtt average */
	for (j = 0, i = 0; j < probe_no; j++) {

		if (((rtt_ms[j] > net_stat.rtt_probe.usec) &&
			(rtt_ms[j] < (net_stat.rtt_probe.usec +
				(deviation * opts.rtt_probe_opt.deviation_filter)))) ||
		   ((rtt_ms[j] < net_stat.rtt_probe.usec) &&
			(rtt_ms[j] > (net_stat.rtt_probe.usec -
				(deviation * opts.rtt_probe_opt.deviation_filter))))) {
			d_tmp += rtt_ms[j];
			++i;
		}
	}
	net_stat.rtt_probe.usec = d_tmp / i;

	msg(LOUDISH, "average rtt: %.3fms (after filter), covariance: %.3fms^2, standard deviation %.3fms",
			net_stat.rtt_probe.usec, covariance, deviation);

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

#define	RTT_PAYLOAD_SIZE 500
#define	RTT_NO_PROBES 5


static int
send_rtt_info(int fd, int next_hdr, struct rtt_probe *rtt_probe)
{
	(void) fd;
	(void) next_hdr;
	(void) rtt_probe;
	return -1;
}

int
meta_exchange_snd(int connected_fd, int file_fd)
{
	int ret = 0;
	ssize_t len;
	ssize_t file_size;
	struct ns_hdr ns_hdr;
	struct stat stat_buf;
	int perform_rtt;

	if (opts.socktype != SOCK_STREAM) {
		msg(STRESSFUL, "skipping rtt probing: not SOCK_STREAM");
		return 0;
	}
	memset(&ns_hdr, 0, sizeof(struct ns_hdr));

	/* fetch file size */
	ret = fstat(file_fd, &stat_buf);
	if (ret == -1)
		err_sys_die(EXIT_FAILMISC, "Can't fstat file %s", opts.infile);

	file_size = S_ISREG(stat_buf.st_mode) ? stat_buf.st_size : 0;


	ns_hdr.magic = htons(NS_MAGIC);
	/* FIXME: catch overflow */
	ns_hdr.version = htons((uint16_t) strtol(VERSIONSTRING, (char **)NULL, 10));
	ns_hdr.data_size = htonl(file_size);

	perform_rtt = (opts.rtt_probe_opt.iterations > 0) ? 1 : 0;


	ns_hdr.nse_nxt_hdr = perform_rtt ? htons(NSE_NXT_RTT_PROBE) : htons(NSE_NXT_DATA);

	len = sizeof(struct ns_hdr);
	if (writen(connected_fd, &ns_hdr, len) != len)
		err_msg_die(EXIT_FAILHEADER, "Can't send netsend header!\n");

	/* probe for effective round trip time */
	if (opts.rtt_probe_opt.iterations > 0) {

		int flag_old;
		struct sigaction sa;

		/* initialize signalhandler for timeout handling */
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_INTERRUPT;	/* don't restart system calls */
		sa.sa_handler = timout_handler;
		if (sigaction(SIGALRM, &sa, NULL) != 0)
			err_sys("Can't add signal handler");

		/* set TCP_NODELAY so tcp writes dont get buffered */
		if (opts.protocol == IPPROTO_TCP) {
			if ((flag_old = set_nodelay(connected_fd, 1)) < 0) {
				err_sys("Can't set TCP_NODELAY for socket (ret: %d)",
						flag_old);
			}
		}

		alarm(TIMEOUT_SEC);
		probe_rtt(connected_fd, NSE_NXT_DATA,
				opts.rtt_probe_opt.iterations, opts.rtt_probe_opt.data_size);
		alarm(0);

		/* and restore TCP_NOPUSH */
		if (opts.protocol == IPPROTO_TCP) {
			if ((set_nodelay(connected_fd, flag_old)) < 0) {
				err_sys("Can't set TCP_NODELAY for socket");
			}
		}

		/* transmitt our rtt probe results to our peer */
		send_rtt_info(connected_fd, NSE_NXT_DATA, &net_stat.rtt_probe);

	}

	/* XXX: add shasum next header if opts.sha, modify nse_nxt_hdr processing */

	return ret;
}

/* probe_rtt read a rtt probe packet, set nse_nxt_hdr to zero,
** nse_len to the current packet size and send it back to origin
*/
static int
process_rtt_probe(int peer_fd, uint16_t nse_len)
{
	int ret = 0; uint16_t *intptr;
	struct ns_rtt_probe *ns_rtt_probe_ptr;
	char buf[nse_len * 4 + 4];
	ssize_t to_read = nse_len * 4;


	if (readn(peer_fd, buf + 4, to_read) != to_read)
		return -1;

	ns_rtt_probe_ptr = (struct ns_rtt_probe *) buf;

	msg(STRESSFUL, "process rtt probe (sequence: %d, type: %d packet_size: %d)",
			ntohs(ns_rtt_probe_ptr->seq_no), ntohs(ns_rtt_probe_ptr->type), to_read);

	ns_rtt_probe_ptr->type = htons(RTT_REPLY_TYPE);

	intptr = (uint16_t *)buf;
	*intptr = 0;

	intptr = (uint16_t *)buf + sizeof(uint16_t);
	*intptr = htons(nse_len);

	if (writen(peer_fd, buf, to_read + 4) != to_read + 4)
		err_msg_die(EXIT_FAILHEADER, "Can't reply to rtt probe!\n");

	return ret;
}


static int
process_rtt_info(int peer_fd, uint16_t nse_len)
{
	char buf[nse_len * 4 + sizeof(uint16_t) * 2];
	ssize_t to_read = nse_len * 4;
	struct ns_rtt_info *ns_rtt_info;

	if (readn(peer_fd, buf + sizeof(uint16_t) * 2, to_read) != to_read)
		return -1;

	ns_rtt_info = (struct ns_rtt_info *)buf;


	return 0;
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

	if (opts.socktype != SOCK_STREAM) {
		msg(STRESSFUL, "skipping rtt probing: not SOCK_STREAM");
		return 0;
	}

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

		/* read first 4 octets of extension header, because we now
		** there IS a extension header and a extension header is always
		** 4 byte
		*/
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

			case NSE_NXT_RTT_PROBE:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_RTT_PROBE");
				ret = process_rtt_probe(peer_fd, extension_size);
				if (ret == -1)
					return -1;
				break;

			case NSE_NXT_RTT_INFO:
				msg(STRESSFUL, "next extension header: %s", "NSE_NXT_RTT_INFO");
				ret = process_rtt_info(peer_fd, extension_size);
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
		switch (extension_type) {
			case NSE_NXT_DATA:
				msg(STRESSFUL, "end of extension header processing (NSE_NXT_DATA)");
				return 0;

			case NSE_NXT_NONXT:
				msg(STRESSFUL, "end of extension header processing (NSE_NXT_NONXT)");
				return process_nonxt(peer_fd, extension_size);
				break;
		}
	};

	/* failure if we reach here (failure in previous while loop */
	return -1;
}

#undef INVALID_EXT_TRESH_NO



/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */

