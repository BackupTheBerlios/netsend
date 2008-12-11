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

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/socket.h>

#include "analyze.h"
#include "global.h"
#include "proto_tcp.h"
#include "proto_tipc.h"
#include "proto_udp.h"
#include "proto_unix.h"

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
};


struct socket_options socket_options[] = {
  {"SO_KEEPALIVE", SOL_SOCKET,  SO_KEEPALIVE, SVT_BOOL, false, {0}},
  {"SO_REUSEADDR", SOL_SOCKET,  SO_REUSEADDR, SVT_BOOL, false, {0}},
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, false, {0}},
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, false, {0}},
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, false, {0}},
  {"TCP_NODELAY",  SOL_TCP, TCP_NODELAY,  SVT_BOOL, false, {0}},
  {"TCP_CONGESTION", SOL_TCP, TCP_CONGESTION, SVT_STR, false, {0}},
  {"TCP_CORK",     SOL_TCP, TCP_CORK,  SVT_BOOL, false, {0}},
  {"TCP_KEEPCNT",  SOL_TCP, TCP_KEEPCNT,  SVT_INT, false, {0}},
  {"TCP_KEEPIDLE",  SOL_TCP, TCP_KEEPIDLE,  SVT_INT, false, {0}},
  {"TCP_KEEPINTVL",  SOL_TCP, TCP_KEEPINTVL,  SVT_INT, false, {0}},
  {"TCP_SYNCNT",  SOL_TCP, TCP_SYNCNT,  SVT_INT, false, {0}},
  {"TCP_WINDOW_CLAMP",  SOL_TCP, TCP_WINDOW_CLAMP,  SVT_INT, false, {0}},
  {"SCTP_DISABLE_FRAGMENTS", IPPROTO_SCTP, SCTP_DISABLE_FRAGMENTS, SVT_BOOL, 0, {0}},
/* DONT, _WANT,  0,1.. ,although an extra convert hook would be nice to have */
  {"IP_MTU_DISCOVER", IPPROTO_IP, IP_MTU_DISCOVER, SVT_INT, 0, {0}}, 
  {"SO_RCVBUF",    SOL_SOCKET,  SO_RCVBUF,    SVT_INT,  false, {0}},
  {"SO_SNDLOWAT",  SOL_SOCKET,  SO_SNDLOWAT,  SVT_INT,  false, {0}},
  {"SO_RCVLOWAT",  SOL_SOCKET,  SO_RCVLOWAT,  SVT_INT,  false, {0}},
  {"SO_SNDTIMEO",  SOL_SOCKET,  SO_SNDTIMEO,  SVT_TIMEVAL,  false, {0}},
  {"SO_RCVTIMEO",  SOL_SOCKET,  SO_RCVTIMEO,  SVT_TIMEVAL,  false, {0}},
  {"UDPLITE_SEND_CSCOV", IPPROTO_UDPLITE, UDPLITE_SEND_CSCOV, SVT_INT, false, {8}},
  {NULL, 0, 0, 0, 0, {0}}
};

struct opts opts;
struct net_stat net_stat;

struct sock_callbacks sock_callbacks = {
	.cb_read = read,
	.cb_write = write,
	.cb_accept = accept,
	.cb_listen = listen
};

#define	MAX_STATLEN 4096


static void
ignore_sigpipe(void)
{
	struct sigaction sa = { .sa_handler = SIG_IGN };
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGPIPE, &sa, NULL);
}


int
main(int argc, char *argv[])
{
	int ret = EXIT_OK;

	/* parse_opts will exit if an error occurr */
	parse_opts(argc, argv, &opts);

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

	ignore_sigpipe();

	/* Branch to final workmode ... */
	switch (opts.workmode) {
	case MODE_TRANSMIT:
		msg(GENTLE, "transmit mode (file: %s  -  hostname: %s)",
			opts.infile, opts.hostname);
		switch (opts.ns_proto) {
			case NS_PROTO_TCP:
				msg(LOUDISH, "branch to IPPROTO_TCP");
				ip_stream_trans_mode(&opts);
				break;
			case NS_PROTO_UDP:
				msg(LOUDISH, "branch to IPROTO_UDP");
				udp_trans_mode(&opts);
				break;
			case NS_PROTO_UDPLITE:
				msg(LOUDISH, "branch to IPPROTO_UDPLITE");
				udp_trans_mode(&opts);
				break;
			case NS_PROTO_SCTP:
				msg(LOUDISH, "branch to IPPROTO_SCCP");
				ip_stream_trans_mode(&opts);
				break;
			case NS_PROTO_DCCP:
				msg(LOUDISH, "branch to IPPROTO_DCCP");
				ip_stream_trans_mode(&opts);
				break;
			case NS_PROTO_TIPC:
				msg(LOUDISH, "branch to AF_TIPC");
				tipc_trans_mode();
				break;
			case NS_PROTO_UNIX:
				msg(LOUDISH, "branch to AF_UNIX");
				unix_trans_mode(&opts);
				break;
			case NS_PROTO_UNSPEC:
				err_msg_die(EXIT_FAILINT, "Programmed Error");
		}
		break;
	case MODE_RECEIVE:
		receive_mode();
		break;
	default:
		err_msg_die(EXIT_FAILMISC, "Programmed Failure");
	}

	if (opts.statistics || opts.machine_parseable) {
		char buf[MAX_STATLEN];

		if (opts.machine_parseable)
			gen_machine_analyse(buf, MAX_STATLEN);
		else
			gen_human_analyse(buf, MAX_STATLEN);

		fputs(buf, stderr);
		fflush(stderr);
	}

	return ret;
}


/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
