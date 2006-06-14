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

#include "global.h"

extern struct opts opts;
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
	fprintf(stderr, "Not Implemented (%s:%d)\n", __FILE__, __LINE__);
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
	fprintf(stderr, "Not Implemented (%s:%d)\n", __FILE__, __LINE__);
	exit(EXIT_FAILMISC);
}


static ssize_t
ss_rw(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t cnt, cnt_coll = 0;
	unsigned char *buf;

	/* user option or default */
	buflen = opts.buffer_size;

	/* allocate buffer */
	buf = malloc(buflen);
	if (!buf) {
		fprintf(stderr, "ERROR: Can't allocate %d bytes: %s!\n",
				buflen, strerror(errno));
		exit(EXIT_FAILMEM);
	}

	/* XXX: proof this code for this case that we read from
	** STDIN. Exact: the default buflen and the interaction
	** between glibc buffersize for STDIN.
	** Do we want to change the default buffer behavior for
	** STDIN(linebuffer, fullbuffer, ...)?  --HGN
	*/

	if (opts.mem_advice &&
		posix_fadvise(file_fd, 0, 0, get_mem_adv_f(opts.mem_advice))) {
		err_sys("posix_fadvise");	/* do not exit */
	}

	while ((cnt = read(file_fd, buf, buflen)) > 0) {
		unsigned char *bufptr;
		net_stat.read_call_cnt++;
		bufptr = buf;
		do {
			ssize_t written = write(connected_fd, bufptr, cnt);
			if (written < 0) {
				if (errno != EINTR)
					goto out;
				continue;
			}
			cnt -= written;
			bufptr += written;
		} while (cnt > 0);
		cnt_coll += cnt;
	}
out:
	free(buf);
	return cnt_coll;
}


static ssize_t
ss_mmap(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc;
	struct stat stat_buf;
	void *mmap_buf;

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "ERROR: Can't fstat file %s: %s\n", opts.infile,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	mmap_buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, file_fd, 0);
	if (mmap_buf == MAP_FAILED) {
		fprintf(stderr, "ERROR: Can't mmap file %s: %s\n",
				opts.infile, strerror(errno));
	}

	if (opts.mem_advice && posix_madvise(mmap_buf, stat_buf.st_size, get_mem_adv_m(opts.mem_advice)))
		perror("posix_madvise");	/* do not exit */

	rc = write(connected_fd, mmap_buf, stat_buf.st_size);
	if (rc != stat_buf.st_size) {
		fprintf(stderr, "ERROR: Can't flush buffer within write call: %s!\n",
				strerror(errno));
	}

	ret = munmap(mmap_buf, stat_buf.st_size);
	if (ret == -1) {
		fprintf(stderr, "ERROR: Can't munmap buffer: %s\n", strerror(errno));
	}

	return rc;
}


static ssize_t
ss_sendfile(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc;
	struct stat stat_buf;
	off_t offset = 0;

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "ERROR: Can't fstat file %s: %s\n", opts.infile,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	rc = sendfile(connected_fd, file_fd, &offset, stat_buf.st_size);
	if (rc == -1) {
		fprintf(stderr, "ERROR: Failure in sendfile routine: %s\n", strerror(errno));
		exit(EXIT_FAILNET);
	}
	if (rc != stat_buf.st_size) {
		fprintf(stderr, "ERROR: Incomplete transfer from sendfile: %d of %ld bytes",
				rc, stat_buf.st_size);
		exit(EXIT_FAILNET);
	}

	return rc;
}


/* Creates our server socket and initialize
** options
*/
static int
instigate_ss(void)
{
	int ret, fd, i;
	struct addrinfo  hosthints, *hostres, *addrtmp;


	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
	hosthints.ai_flags    = AI_PASSIVE;


	xgetaddrinfo(opts.hostname, opts.port, &hosthints, &hostres);

	addrtmp = hostres;

	do {
		/* We do not wan't unix domain socket's */
		if (addrtmp->ai_family == PF_LOCAL) {
			continue;
		}

		/* TODO: add some sanity checks here ... */

		/* We have found for what we are looking for */
		if (addrtmp->ai_family == opts.family) {
			break;
		}

	} while ((addrtmp = addrtmp->ai_next));


	fd = socket(hostres->ai_family, hostres->ai_socktype,
			hostres->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Can't create server socket: %s\n",
				strerror(errno));
		exit(EXIT_FAILNET);
	}

	/* loop over all selectable socket options */
	for (i = 0; socket_options[i].sockopt_name; i++) {

		if (socket_options[i].user_issue) { /* ... jaap */

			switch (socket_options[i].sockopt_type) {
				case SVT_BOOL:
				case SVT_ON:
					ret = setsockopt(fd, socket_options[i].level,
							         socket_options[i].option,
									 &socket_options[i].value,
									 sizeof(socket_options[i].value));
					break;
				default:
					fprintf(stderr, "Not Implemented (%s:%d)\n",
							__FILE__, __LINE__);
					exit(EXIT_FAILMISC);
			}
			if (ret < 0) {
				fprintf(stderr, "ERROR: Can't set socket option %s: %s",
					socket_options[i].sockopt_name, strerror(errno));
				exit(EXIT_FAILMISC);
			}

		}
	}

	ret = bind(fd, hostres->ai_addr, hostres->ai_addrlen);
	if (ret < 0) {
		err_sys("Can't bind() myself");
		exit(EXIT_FAILNET);
	}

	if (opts.change_congestion) {

		if (VL_LOUDISH(opts.verbose)) {
			fprintf(stderr, "Congestion Avoidance: %s\n",
					congestion_map[opts.congestion].conf_string);
		}

		struct protoent *pptr = getprotobyname("tcp");
		if (!pptr) {
			fprintf(stderr, "getprotobyname() return uncleanly!\n");
			exit(EXIT_FAILNET);
		}

		ret = setsockopt(fd, pptr->p_proto, TCP_CONGESTION,
				congestion_map[opts.congestion].conf_string,
				strlen(congestion_map[opts.congestion].conf_string) + 1);
		if (ret < 0 && VL_GENTLE(opts.verbose)) {
			fprintf(stderr, "Can't set congestion avoidance algorithm(%s): %s!\n"
					"Did you build a kernel with proper ca support?\n",
					congestion_map[opts.congestion].conf_string,
					strerror(errno));
		}
	}


	ret = listen(fd, BACKLOG);
	if (ret < 0) {
		fprintf(stderr, "Can't listen(): %s\n", strerror(errno));
		exit(EXIT_FAILNET);
	}

	fd = accept(fd, hostres->ai_addr, &hostres->ai_addrlen);

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
server_mode(void)
{
	int connected_fd, file_fd, child_status;

	if (VL_GENTLE(opts.verbose)) {
		fprintf(stderr, "Server Mode (send file: %s)\n",
				opts.execstring ? "" : opts.infile);
	}

	/* check if the transmitted file is present and readable */
	file_fd = open_input_file();

	do {

		/* block and wait for client */
		connected_fd = instigate_ss();

		/* fetch sockopt before the first byte  */
		get_sock_opts(connected_fd, &net_stat);

		/* take the transmit start time for diff */
		gettimeofday(&opts.starttime, NULL);

		/* depend on the io_call we must handle our input
		** file a little bit different
		*/
		switch (opts.io_call) {
			case IO_SENDFILE:
				ss_sendfile(file_fd, connected_fd);
				break;
			case IO_NMAP:
				ss_mmap(file_fd, connected_fd);
				break;
			case IO_RW:
				ss_rw(file_fd, connected_fd);
				break;
			default:
				fprintf(stderr, "Programmed Failure(%s:%d)!\n", __FILE__, __LINE__);
				exit(EXIT_FAILMISC);
				break;
		}

		gettimeofday(&opts.endtime, NULL);

		/* if we spawn a child - reaping it here */
		waitpid(-1, &child_status, 0);

	} while (0); /* XXX: Further improvement: iterating server ;-) */
}




/* vim:set ts=4 sw=4 tw=78 noet: */
