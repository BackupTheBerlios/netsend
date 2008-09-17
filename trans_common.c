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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600	/* needed for posix_madvise/fadvise */
#include <sys/mman.h>
#include <fcntl.h>
#undef _XOPEN_SOURCE

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "debug.h"
#include "global.h"
#include "xfuncs.h"
//#include "proto_tipc.h"
#include "proto_tcp.h"


extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];
extern struct sock_callbacks sock_callbacks;

static int get_mem_adv_m(int adv)
{
	switch (adv) {
		case MEMADV_SEQUENTIAL: return POSIX_MADV_SEQUENTIAL;
		case MEMADV_DONTNEED: return POSIX_MADV_DONTNEED;
		case MEMADV_RANDOM: return POSIX_MADV_RANDOM;
		case MEMADV_NORMAL: return POSIX_MADV_NORMAL;
		case MEMADV_NOREUSE: /* there is only POSIX_FADV_NOREUSE */
		case MEMADV_WILLNEED: return POSIX_MADV_WILLNEED;
		default: break;
	}
	DEBUGPRINTF("adv number %d unknown\n", adv);
	exit(EXIT_FAILMISC);
}


static int get_mem_adv_f(int adv)
{
	switch (adv) {
		case MEMADV_SEQUENTIAL: return POSIX_FADV_SEQUENTIAL;
		case MEMADV_DONTNEED: return POSIX_FADV_DONTNEED;
		case MEMADV_RANDOM: return POSIX_FADV_RANDOM;
		case MEMADV_NORMAL: return POSIX_FADV_NORMAL;
		case MEMADV_NOREUSE: return POSIX_FADV_NOREUSE;
		case MEMADV_WILLNEED: return POSIX_FADV_WILLNEED;
		default: break;
	}
	DEBUGPRINTF("adv number %d unknown\n", adv);
	exit(EXIT_FAILMISC);
}


static ssize_t write_len(int fd, const void *buf, size_t len)
{
	const char *bufptr = buf;
	ssize_t total = 0;
	do {
		ssize_t written = sock_callbacks.cb_write(fd, bufptr, len);
		net_stat.total_tx_calls += 1;
		if (written < 0) {
			int real_errno;

			if (errno == EINTR || errno == EAGAIN)
				continue;

			real_errno = errno;
			err_msg("Could not write %u bytes: %s", len, strerror(errno));
			if (opts.protocol == IPPROTO_SCTP && real_errno == EMSGSIZE) {
				err_msg("for SCTP the maximum size of data that can be sent in a "
					"single send call is limited by SO_SNDBUF.\n"
					"Either increase send buffer size (-s SO_SNDBUF) or "
					"lower the write buffer size (-b)");

			}
			errno = real_errno;
			break;
		}
		total += written;
		bufptr += written;
		len -= written;
	} while (len > 0);

	return total > 0 ? total : -1;
}


static ssize_t trans_rw(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t cnt, cnt_coll = 0;
	unsigned char *buf;

	msg(STRESSFUL, "send via read/write io operation");

	/* user option or default */
	buflen = opts.buffer_size ? opts.buffer_size : DEFAULT_BUFSIZE;

	buf = xmalloc(buflen);
	if (opts.change_mem_advise &&
		posix_fadvise(file_fd, 0, 0, get_mem_adv_f(opts.mem_advice))) {
		err_sys("posix_fadvise");	/* do not exit */
	}

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	while ((cnt = read(file_fd, buf, buflen)) > 0) {
		cnt_coll = write_len(connected_fd, buf, cnt);
		if (cnt_coll == -1)
			break;
		/* correct statistics */
		net_stat.total_tx_bytes += cnt_coll;

		/* if we reached a user transfer limit? */
		if (opts.multiple_barrier) {
			unsigned long long limit = buflen * opts.multiple_barrier;
			if (net_stat.total_tx_bytes >= limit)
				break;
		}
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	free(buf);

	return cnt_coll;
}


static ssize_t trans_mmap(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc, written = 0, write_cnt;
	struct stat stat_buf;
	void *mmap_buf;

	msg(STRESSFUL, "send via mmap/write io operation");

	xfstat(file_fd, &stat_buf, opts.infile);

	net_stat.total_tx_bytes = 0;
	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	mmap_buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, file_fd, 0);
	if (mmap_buf == MAP_FAILED)
		err_sys_die(EXIT_FAILMISC, "Can't mmap file %s: %s\n",
				opts.infile, strerror(errno));

	if (opts.change_mem_advise &&
		posix_madvise(mmap_buf, stat_buf.st_size, get_mem_adv_m(opts.mem_advice)))
		err_sys("posix_madvise");	/* do not exit */

	/* full or partial write */
	write_cnt = opts.buffer_size ?
		opts.buffer_size : stat_buf.st_size;

	/* write chunked sized frames */
	while (stat_buf.st_size - written >= write_cnt) {
		char *tmpbuf = mmap_buf;
		rc = write_len(connected_fd, tmpbuf + written, write_cnt);
		if (rc == -1)
			goto write_fail;
		written += rc;
	}
	/* and write remaining bytes, if any */
	write_cnt = stat_buf.st_size - written;
	if (write_cnt > 0) {
		char *tmpbuf = mmap_buf;
		rc = write_len(connected_fd, tmpbuf + written, write_cnt);
		if (rc == -1) {
 write_fail:
			touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);
			net_stat.total_tx_bytes = written;
			return munmap(mmap_buf, stat_buf.st_size);
		}
		written += rc;
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	if (stat_buf.st_size != written) {
		fprintf(stderr, "ERROR: Can't flush buffer within write call: %s!\n",
				strerror(errno));
		fprintf(stderr, " size: %ld written %zd\n", (long)stat_buf.st_size, written);
	}

	ret = munmap(mmap_buf, stat_buf.st_size);
	if (ret == -1)
		err_sys("Can't munmap buffer");

	/* correct statistics */
	net_stat.total_tx_bytes = stat_buf.st_size;

	return rc;
}

#ifdef HAVE_SPLICE
static long splice_chunk(int pipe_fd, int fd_out, size_t len, int flags)
{
	long written, total = 0;

	do {
		written = splice(pipe_fd, NULL, fd_out, NULL, len, flags);
		if (written < 0) {
			err_sys("Failure in splice from pipe");
			break;
		}

		net_stat.total_tx_calls++;
		total += written;
		len -= written;
        } while (len > 0);

	net_stat.total_tx_bytes += total;
	return total;
}



static ssize_t
ss_splice_frompipe(int pipe_fd, int connected_fd, ssize_t write_cnt)
{
	ssize_t written, total = 0;

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	do {
		written = splice(pipe_fd, NULL, connected_fd, NULL, write_cnt, SPLICE_F_MOVE|SPLICE_F_MORE);
		if (written < 0) {
			err_sys("Failure in splice from pipe");
			break;
		}
		net_stat.total_tx_calls += 1;
		total += written;
        } while (written > 0);

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	net_stat.total_tx_bytes = total;

	return 0;
}


static ssize_t get_splice_size(int file_fd, struct stat *stat_buf)
{
	ssize_t write_cnt;

	xfstat(file_fd, stat_buf, opts.infile);

	if (opts.buffer_size)
		write_cnt = opts.buffer_size;
	else if (S_ISREG(stat_buf->st_mode))
		write_cnt = stat_buf->st_size;
	else
		write_cnt = 65536;

	if (write_cnt > 65536)
		write_cnt = 65536;

	if (opts.buffer_size > 65536)
		 msg(STRESSFUL, "reduced splice buffer length to 64k");

	return write_cnt;
}
#endif


static ssize_t trans_splice(int file_fd, int connected_fd)
{
#ifdef HAVE_SPLICE
	int pipefds[2];
	struct stat stat_buf;
	ssize_t rc, write_cnt;
	loff_t offset = 0;

	msg(STRESSFUL, "send via splice io operation");

	write_cnt = get_splice_size(file_fd, &stat_buf);

	if (S_ISFIFO(stat_buf.st_mode))
		return ss_splice_frompipe(file_fd, connected_fd, write_cnt);

	xpipe(pipefds);

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	/* write chunked sized frames */
	while (stat_buf.st_size - offset - 1 >= write_cnt) {
		rc = splice(file_fd, &offset, pipefds[1], NULL, write_cnt, SPLICE_F_MOVE);
		if (rc == -1)
			err_sys_die(EXIT_FAILMISC, "Failure in splice to pipe");
		if (splice_chunk(pipefds[0], connected_fd, rc, SPLICE_F_MOVE|SPLICE_F_MORE) < 0)
			goto finish;
	}
	/* and write remaining bytes, if any */
	write_cnt = stat_buf.st_size - offset - 1;
	if (write_cnt >= 0) {
		rc = splice(file_fd, &offset, pipefds[1], NULL, write_cnt + 1, 0);
		if (rc == -1)
			err_sys_die(EXIT_FAILMISC, "Failure in splice to pipe");

		splice_chunk(pipefds[0], connected_fd, rc, SPLICE_F_MOVE);
	}
 finish:
	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	if (offset != stat_buf.st_size)
		err_msg("Incomplete transfer in splice: %d of %ld bytes",
						offset , stat_buf.st_size);
	close(pipefds[0]);
	close(pipefds[1]);
	return rc;
#else
	err_msg_die(EXIT_FAILMISC, "splice support not compiled in");
#endif
}


static ssize_t trans_sendfile(int file_fd, int connected_fd)
{
	struct stat stat_buf;
	ssize_t rc, write_cnt;
	off_t offset = 0;

	msg(STRESSFUL, "send via sendfile io operation");

	xfstat(file_fd, &stat_buf, opts.infile);

	if (stat_buf.st_size == 0)
		err_msg("%s: empty file", opts.infile);

	/* full or partial write */
	write_cnt = opts.buffer_size ?
		opts.buffer_size : stat_buf.st_size;

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	/* write chunked sized frames */
	while (stat_buf.st_size - offset - 1 >= write_cnt) {
		rc = sendfile(connected_fd, file_fd, &offset, write_cnt);
		if (rc == -1)
			err_sys_die(EXIT_FAILNET, "Failure in sendfile routine");
		net_stat.total_tx_calls += 1;
	}
	/* and write remaining bytes, if any */
	write_cnt = stat_buf.st_size - offset - 1;
	if (write_cnt >= 0) {
		rc = sendfile(connected_fd, file_fd, &offset, write_cnt + 1);
		if (rc == -1)
			err_sys_die(EXIT_FAILNET, "Failure in sendfile routine");
		net_stat.total_tx_calls += 1;
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	if (offset != stat_buf.st_size)
		err_msg_die(EXIT_FAILNET, "Incomplete transfer from sendfile: %d of %ld bytes",
				offset , stat_buf.st_size);

	/* correct statistics */
	net_stat.total_tx_bytes = stat_buf.st_size;
	return rc;
}


void trans_start(int file_fd, int connected_fd)
{
	switch (opts.io_call) {
	case IO_SENDFILE:
		trans_sendfile(file_fd, connected_fd);
		break;
	case IO_SPLICE:
		trans_splice(file_fd, connected_fd);
		break;
	case IO_MMAP:
		trans_mmap(file_fd, connected_fd);
		break;
	case IO_RW:
		trans_rw(file_fd, connected_fd);
		break;
	default:
		err_msg_die(EXIT_FAILINT, "Programmed Failure");
	}
}


/* Creates our server socket and initialize
** options
*/
static int init_stream_trans(int ip_protocol)
{
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;
	struct protoent *protoent;

	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = ip_protocol;
	hosthints.ai_flags    = AI_ADDRCONFIG;

	xgetaddrinfo(opts.hostname, opts.port, &hosthints, &hostres);

	addrtmp = hostres;

	for (addrtmp = hostres; addrtmp != NULL ; addrtmp = addrtmp->ai_next) {

		if (opts.family != AF_UNSPEC &&
			addrtmp->ai_family != opts.family) { /* user fixed family! */
			continue;
		}

		fd = socket(addrtmp->ai_family, addrtmp->ai_socktype,
				addrtmp->ai_protocol);
		if (fd < 0) {
			err_sys("socket");
			continue;
		}

		protoent = getprotobynumber(addrtmp->ai_protocol);
		if (protoent)
			msg(LOUDISH, "socket created - protocol %s(%d)",
				protoent->p_name, protoent->p_proto);

		assert(addrtmp->ai_protocol == ip_protocol);

		if (use_multicast) {
			int hops_ttl = 30;
			int on = 1;
			switch (addrtmp->ai_family) {
			case AF_INET6:
				xsetsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (char *)&hops_ttl,
							sizeof(hops_ttl), "IPV6_MULTICAST_HOPS");
				xsetsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
						&on, sizeof(int), "IPV6_MULTICAST_LOOP");
				break;
			case AF_INET:
				xsetsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
				         (char *)&hops_ttl, sizeof(hops_ttl), "IP_MULTICAST_TTL");

				xsetsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
						&on, sizeof(int), "IP_MULTICAST_LOOP");
				msg(STRESSFUL, "set IP_MULTICAST_LOOP option");
				break;
			default:
				err_msg_die(EXIT_FAILINT, "Programmed Failure");
			}
		}

		if (opts.tcp_use_md5sig)
			tcp_setsockopt_md5sig(fd, addrtmp->ai_addr);
		set_socketopts(fd);

		/* Connect to peer
		** There are three advantages to call connect for all types
		** of our socket protocols (especially udp)
		**
		** 1. We don't need to specify a destination address (only call write)
		** 2. Performance advantages (kernel level)
		** 3. Error detection (e.g. destination port unreachable at udp)
		*/
		ret = connect(fd, addrtmp->ai_addr, addrtmp->ai_addrlen);
		if (ret == -1)
			err_sys_die(EXIT_FAILNET, "Can't connect to %s", opts.hostname);

		msg(LOUDISH, "socket connected to %s via port %s",
			opts.hostname, opts.port);
	}

	if (fd < 0)
		err_msg_die(EXIT_FAILNET, "No suitable socket found");

	freeaddrinfo(hostres);
	return fd;
}


/*
 * initialize server socket
 * fstat and open our sending-file
 * block in socket and wait for client
 * sendfile(2), write(2), ...
 * print diagnostic info
*/
void ip_stream_trans_mode(struct opts *optsp, int ipproto)
{
	int connected_fd, file_fd;

	msg(GENTLE, "transmit mode (file: %s  -  hostname: %s)",
		opts.infile, opts.hostname);

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = init_stream_trans(ipproto);

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	trans_start(file_fd, connected_fd);

	if (ipproto == IPPROTO_TCP && VL_LOUDISH(opts.verbose)) {
		struct tcp_info tcp_info;

		if (tcp_get_info(connected_fd, &tcp_info))
			tcp_print_info(&tcp_info);
	}
}

/* vim:set ts=4 sw=4 tw=78 noet: */
