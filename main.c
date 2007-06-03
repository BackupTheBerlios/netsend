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

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/socket.h>

#include "analyze.h"
#include "global.h"
#include "proto_tipc.h"
#include "proto_udp.h"

struct conf_map_t congestion_map[] = {
	{ CA_BIC,      "bic"      },
	{ CA_WESTWOOD, "westwood" },
	{ CA_VEGAS,    "vegas"    },
	{ CA_SCALABLE, "scalable" },
	{ CA_HYBLA,    "hybla"    },
	{ CA_HTCP,     "htcp"     },
	{ CA_CUBIC,    "cubic"    },
	{ CA_RENO,     "reno"     }
};


struct conf_map_t memadvice_map[] = {
	{ MEMADV_NORMAL,	"normal" },
	{ MEMADV_RANDOM,	"random" },
	{ MEMADV_SEQUENTIAL,	"sequential" },
	{ MEMADV_WILLNEED,	"willneed" },
	{ MEMADV_DONTNEED,	"dontneed" },
	{ MEMADV_NOREUSE,	"noreuse" }
};


struct conf_map_t io_call_map[] = {
	{ IO_MMAP,		"mmap"		},
	{ IO_SENDFILE,	"sendfile"  },
	{ IO_SPLICE,	"splice"  },
	{ IO_RW,		"rw"		},
	{ IO_READ,		"read"		},
};

/* Don't arrange wild! Look at the CNT_* order! */
struct socket_options socket_options[] = {
  {"SO_KEEPALIVE", SOL_SOCKET,  SO_KEEPALIVE, SVT_BOOL, 0, 0},
  {"SO_REUSEADDR", SOL_SOCKET,  SO_REUSEADDR, SVT_BOOL, 0, 0},
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, 0, 0},
  {"TCP_NODELAY",  IPPROTO_TCP, TCP_NODELAY,  SVT_BOOL, 0, 0},
  {"TCP_CORK",     IPPROTO_TCP, TCP_CORK,  SVT_BOOL, 0, 0},
  {"SO_SNDBUF",    SOL_SOCKET,  SO_SNDBUF,    SVT_INT,  0, 0},
  {"SO_RCVBUF",    SOL_SOCKET,  SO_RCVBUF,    SVT_INT,  0, 0},
  {"SO_SNDLOWAT",  SOL_SOCKET,  SO_SNDLOWAT,  SVT_INT,  0, 0},
  {"SO_RCVLOWAT",  SOL_SOCKET,  SO_RCVLOWAT,  SVT_INT,  0, 0},
  {"SO_SNDTIMEO",  SOL_SOCKET,  SO_SNDTIMEO,  SVT_INT,  0, 0},
  {"SO_RCVTIMEO",  SOL_SOCKET,  SO_RCVTIMEO,  SVT_INT,  0, 0},
  {"DCCP_SOCKOPT_PACKET_SIZE",  SOL_DCCP,  DCCP_SOCKOPT_PACKET_SIZE,  SVT_INT,  0, 0},
  {NULL, 0, 0, 0, 0, 0}
};

struct opts opts;
struct net_stat net_stat;

struct sock_callbacks sock_callbacks = { .cb_read = read, .cb_write = write, .cb_accept = accept, .cb_listen = listen};

#define	MAX_STATLEN 4096


int
main(int argc, char *argv[])
{
	int ret = EXIT_OK;

	if (parse_opts(argc, argv)) {
		usage();
		exit(EXIT_FAILOPT);
	}

	msg(GENTLE, PROGRAMNAME " - " VERSIONSTRING);

	if (opts.sched_user) {
		struct sched_param sp;
		sp.sched_priority = opts.priority;

		if (sched_setscheduler(0, opts.sched_policy, &sp)) {
			err_sys("sched_setscheduler()");
		}
	}
	if ((opts.nice != INT_MAX) && (nice(opts.nice) == -1)) {
		err_sys("nice()");
	}

	switch (opts.protocol) {
	case IPPROTO_UDP: case IPPROTO_UDPLITE:
		sock_callbacks.cb_listen = udp_listen;
	}
	/* Branch to final workmode ... */
	switch (opts.workmode) {
		case MODE_TRANSMIT:
			transmit_mode();
			break;
		case MODE_RECEIVE:
			receive_mode();
			break;
		default:
			err_msg_die(EXIT_FAILMISC, "Programmed Failure");
	}

	if (opts.statistics || opts.mashine_parseable) {
		char buf[MAX_STATLEN];

		if (opts.mashine_parseable)
			gen_mashine_analyse(buf, MAX_STATLEN);
		else
			gen_human_analyse(buf, MAX_STATLEN);

		fputs(buf, stderr);
		fflush(stderr);
	}

	return ret;
}



/* vim:set ts=4 sw=4 tw=78 noet: */
