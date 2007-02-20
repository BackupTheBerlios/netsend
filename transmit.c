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
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "debug.h"
#include "global.h"


extern struct opts opts;
extern struct net_stat net_stat;
extern struct conf_map_t congestion_map[];
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];

static int
get_mem_adv_m(int adv)
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


static int
get_mem_adv_f(int adv)
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


ssize_t
write_len(int fd, const void *buf, size_t len)
{
	const char *bufptr = buf;
	ssize_t total = 0;
	do {
		ssize_t written = write(fd, bufptr, len);
		net_stat.total_tx_calls += 1;
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


static ssize_t
ss_rw(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t cnt, cnt_coll = 0;
	unsigned char *buf;

	msg(STRESSFUL, "send via read/write io operation");

	/* user option or default */
	buflen = opts.buffer_size ? opts.buffer_size : DEFAULT_BUFSIZE;

	/* allocate buffer */
	buf = alloc(buflen);

	/* XXX: proof this code for this case that we read from
	** STDIN. Exact: the default buflen and the interaction
	** between glibc buffersize for STDIN.
	** Do we want to change the default buffer behavior for
	** STDIN(linebuffer, fullbuffer, ...)?  --HGN
	*/

	if (opts.change_mem_advise &&
		posix_fadvise(file_fd, 0, 0, get_mem_adv_f(opts.mem_advice))) {
		err_sys("posix_fadvise");	/* do not exit */
	}

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	while ((cnt = read(file_fd, buf, buflen)) > 0) {
		cnt_coll = write_len(connected_fd, buf, cnt);
		if (cnt_coll == -1) {
			err_sys("write error");
			exit(EXIT_FAILNET);;
		}
		/* correct statistics */
		net_stat.total_tx_bytes += cnt_coll;

		/* if we reached a user transfer limit? */
		if (opts.multiple_barrier &&
				net_stat.total_tx_bytes >= (buflen * opts.multiple_barrier)) {
			break;
		}
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	free(buf);

	return cnt_coll;
}


static ssize_t
ss_mmap(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc, written = 0, write_cnt;
	struct stat stat_buf;
	void *mmap_buf;

	msg(STRESSFUL, "send via mmap/write io operation");

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "ERROR: Can't fstat file %s: %s\n", opts.infile,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	mmap_buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, file_fd, 0);
	if (mmap_buf == MAP_FAILED) {
		err_sys_die(EXIT_FAILMISC, "Can't mmap file %s: %s\n",
				opts.infile, strerror(errno));
	}

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
		written += rc;
	};
	/* and write remaining bytes, if any */
	write_cnt = stat_buf.st_size - written;
	if (write_cnt > 0) {
		char *tmpbuf = mmap_buf;
		rc = write_len(connected_fd, tmpbuf + written, write_cnt);
		written += rc;
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);

	if (stat_buf.st_size != written) {
		fprintf(stderr, "ERROR: Can't flush buffer within write call: %s!\n",
				strerror(errno));
		fprintf(stderr, " size: %ld written %d\n", (long)stat_buf.st_size, written);
	}

	ret = munmap(mmap_buf, stat_buf.st_size);
	if (ret == -1) {
		err_sys("Can't munmap buffer");
	}

	/* correct statistics */
	net_stat.total_tx_bytes = stat_buf.st_size;

	return rc;
}


static ssize_t
ss_sendfile(int file_fd, int connected_fd)
{
	int ret = 0;
	struct stat stat_buf;
	ssize_t rc, write_cnt;
	off_t offset = 0;

	msg(STRESSFUL, "send via sendfile io operation");

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		err_sys("Can't fstat file %s", opts.infile);
		exit(EXIT_FAILMISC);
	}

	/* full or partial write */
	write_cnt = opts.buffer_size ?
		opts.buffer_size : stat_buf.st_size;

	touch_use_stat(TOUCH_BEFORE_OP, &net_stat.use_stat_start);

	/* write chunked sized frames */
	while (stat_buf.st_size - offset - 1 >= write_cnt) {
		rc = sendfile(connected_fd, file_fd, &offset, write_cnt);
		if (rc == -1) {
			err_sys("Failure in sendfile routine");
			exit(EXIT_FAILNET);
		}
		net_stat.total_tx_calls += 1;
	};
	/* and write remaining bytes, if any */
	write_cnt = stat_buf.st_size - offset - 1;
	if (write_cnt >= 0) {
		rc = sendfile(connected_fd, file_fd, &offset, write_cnt + 1);
		if (rc == -1) {
			err_sys("Failure in sendfile routine");
			exit(EXIT_FAILNET);
		}
		net_stat.total_tx_calls += 1;
	}

	touch_use_stat(TOUCH_AFTER_OP, &net_stat.use_stat_end);


	if (offset != stat_buf.st_size) {
		err_msg("Incomplete transfer from sendfile: %d of %ld bytes",
				offset , stat_buf.st_size);
		exit(EXIT_FAILNET);
	}

	/* correct statistics */
	net_stat.total_tx_bytes = stat_buf.st_size;

	return rc;
}



static void
set_socketopts(int fd)
{
	int i;
	/* loop over all selectable socket options */
	for (i = 0; socket_options[i].sockopt_name; i++) {
		int ret;

		if (!socket_options[i].user_issue)
			continue;

		/* this switch statement look that the particular
		** socketoption match our selected socket-type
		*/
		switch (socket_options[i].level) {
			case SOL_SOCKET:
				break; /* works on every socket */

			/* fall-through begins here ... */
			case IPPROTO_TCP:
				if (opts.protocol == IPPROTO_TCP) {
					break;
				}
			case IPPROTO_UDP:
				if (opts.protocol == IPPROTO_UDP) {
					break;
				}
			case SOL_DCCP:
				if (opts.protocol == IPPROTO_DCCP) {
					break;
				}
				/* and exit if socketoption and sockettype did not match */
				err_msg("You selected an socketoption who isn't "
						"compatible with this particular socket option");
				exit(EXIT_FAILMISC);
			default:
				err_msg("Programmed Error");
				exit(EXIT_FAILNET);
		}

		/* ... and do the dirty: set the socket options */
		switch (socket_options[i].sockopt_type) {
			case SVT_BOOL:
			case SVT_ON:
				ret = setsockopt(fd, socket_options[i].level,
						         socket_options[i].option,
								 &socket_options[i].value,
								 sizeof(socket_options[i].value));
				break;
			default:
				DEBUGPRINTF("Unknown sockopt_type %d\n",
						socket_options[i].sockopt_type);
				exit(EXIT_FAILMISC);
		}
		if (ret < 0) {
			err_sys("ERROR: Can't set socket option %s: ",
				socket_options[i].sockopt_name);
		}
	}
}


/* Creates our server socket and initialize
** options
*/
static int
instigate_ss(void)
{
	bool use_multicast = false;
	int fd = -1, ret;
	struct addrinfo  hosthints, *hostres, *addrtmp;
	struct protoent *protoent;

	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
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
		msg(LOUDISH, "socket created - protocol %s(%d)",
			protoent->p_name, protoent->p_proto);

		/* mulicast checks */
		if (addrtmp->ai_protocol == IPPROTO_UDP) {
			switch (addrtmp->ai_family) {
				case AF_INET6:
					if (IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6 *)
									addrtmp->ai_addr)->sin6_addr)) {
						use_multicast = true;
					}
					break;
				case AF_INET:
					if (IN_MULTICAST(ntohl(((struct sockaddr_in *)
									addrtmp->ai_addr)->sin_addr.s_addr))) {
						use_multicast = true;
					}
					break;
				default:
					err_msg("Programmed Error");
					exit(EXIT_FAILINT);
			}
		}

		if (use_multicast) {
			int hops_ttl = 30;
				int on = 1;
			switch (addrtmp->ai_family) {
				case AF_INET6:
					ret = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
							         (char *)&hops_ttl, sizeof(hops_ttl));
					if (ret == -1) {
						err_sys("Can't set socketoption IPV6_MULTICAST_HOPS");
						exit(EXIT_FAILNET);
					}

					ret = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
							&on, sizeof(int));
					if (ret == -1) {
						err_sys("setsockopt (IPV6_MULTICAST_LOOP) failed");
						exit(EXIT_FAILNET);
					}
					break;
				case AF_INET:
					ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
							         (char *)&hops_ttl, sizeof(hops_ttl));
					if (ret == -1) {
						err_sys("Can't set socketoption IP_MULTICAST_TTL");
						exit(EXIT_FAILNET);
					}

					ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
							&on, sizeof(int));
					if (ret == -1) {
						err_sys("setsockopt (IP_MULTICAST_LOOP) failed");
						exit(EXIT_FAILNET);
					}
					msg(STRESSFUL, "set IP_MULTICAST_LOOP option");
					break;
				default:
					err_msg("Programmed Error");
					exit(EXIT_FAILINT);
					break;
			}
		}

		if (opts.protocol == IPPROTO_TCP && opts.change_congestion)
			change_congestion(fd);

		/* NOTE: in the first paragraph we set default socketoptions
		** if the user doesn't select one and the socket
		** need this (e.g. DCCP_SOCKOPT_PACKET_SIZE)
		**
		** In set_socketopts() we set user socketoptions
		*/

		/* set dccp packet size */
		if (opts.protocol == IPPROTO_DCCP) {

			int packet_size = DCCP_STD_PACKET_SIZE;

			/* if user doesn't selected a packet size */
			if (socket_options[CNT_DCCP_SOCKOPT_PACKET_SIZE].user_issue) {
				ret = setsockopt(fd, SOL_DCCP, DCCP_SOCKOPT_PACKET_SIZE,
						&packet_size, sizeof(packet_size));
				if (ret == -1) {
					err_sys("setsockopt dccp packet size");
					exit(EXIT_FAILNET);
				}
			}
		}


		/* We iterate over our commandline argument array - where the user
		** set socketoption and set this on our socket
		** NOTE: it is necessary to set the soketoption before we call
		** connect, which will invoke a syn packet!
		** Example: if we set the receive buffer size to a greater value, tcp
		** must handle this case and send in the initial packet a window scale
		** option! Now you realize why we send the socketoption before we call
		** connect.    --HGN
		*/
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
		if (ret == -1) {
			err_sys("Can't connect to %s", opts.hostname);
			exit(EXIT_FAILNET);
		}

		msg(LOUDISH, "socket connected to %s via port %s",
			opts.hostname, opts.port);
	}

	if (fd < 0) {
		err_msg("No suitable socket found");
		exit(EXIT_FAILNET);
	}


	freeaddrinfo(hostres);
	return fd;
}


/* *** Main Server Routine ***
**
** o initialize server socket
** o fstat and open our sending-file
** o block in socket and wait for client
** o sendfile(2), write(2), ...
** o print diagnostic info
*/
void
transmit_mode(void)
{
	int connected_fd, file_fd, child_status;

	msg(GENTLE, "transmit mode (file: %s  -  hostname: %s)",
		opts.infile, opts.hostname);

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();
	connected_fd = instigate_ss();

	/* fetch sockopt before the first byte  */
	get_sock_opts(connected_fd, &net_stat);

	/* construct and send netsend header to peer */
	meta_exchange_snd(connected_fd, file_fd);

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	switch (opts.io_call) {
		case IO_SENDFILE:
			ss_sendfile(file_fd, connected_fd);
			break;
		case IO_MMAP:
			ss_mmap(file_fd, connected_fd);
			break;
		case IO_RW:
			ss_rw(file_fd, connected_fd);
			break;
		default:
			err_msg("Programmed Failure");
			exit(EXIT_FAILMISC);
			break;
	}

	gettimeofday(&opts.endtime, NULL);

	/* print tcp statistic if we run verbose (LOUDISCH) */
	if (opts.protocol == IPPROTO_TCP && VL_LOUDISH(opts.verbose)) {

		struct tcp_info tcp_info;

		if (get_tcp_info(connected_fd, &tcp_info) >= 0) {
			 fprintf(stderr, "\ntcp info:\n"
					 "\tretransmits:   %d\n"
					 "\tprobes:        %d\n"
					 "\tbackoff:       %d\n",
					 tcp_info.tcpi_retransmits, tcp_info.tcpi_probes,
					 tcp_info.tcpi_backoff);
			 fprintf(stderr,
					 "\toptions:       ");
			 /* see netinet/tcp.h for definition */
			 if (tcp_info.tcpi_options & TCPI_OPT_TIMESTAMPS)
				 fprintf(stderr, "TIMESTAMPS ");
			 if (tcp_info.tcpi_options & TCPI_OPT_SACK)
				 fprintf(stderr, "SACK ");
			 if (tcp_info.tcpi_options & TCPI_OPT_WSCALE)
				 fprintf(stderr, "WSCALE ");
			 if (tcp_info.tcpi_options & TCPI_OPT_ECN)
				 fprintf(stderr, "ECN ");
			 fputs("\n", stderr);
			 fprintf(stderr,
					 "\tsnd_wscale:    %d\n"
					 "\trcv_wscale:    %d\n"
					 "\trto:           %d\n"
					 "\tato:           %d\n"
					 "\tsnd_mss:       %d\n"
					 "\trcv_mss:       %d\n"
					 "\tunacked:       %d\n"
					 , tcp_info.tcpi_snd_wscale, tcp_info.tcpi_rcv_wscale,
					 tcp_info.tcpi_rto, tcp_info.tcpi_ato, tcp_info.tcpi_snd_mss,
					 tcp_info.tcpi_rcv_mss, tcp_info.tcpi_unacked);


		}
	}

	/* if we spawn a child - reaping it here */
	waitpid(-1, &child_status, 0);

}



/* vim:set ts=4 sw=4 tw=78 noet: */
