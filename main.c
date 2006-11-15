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
#include <sys/utsname.h>

#include "global.h"

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
	{ IO_RW,		"rw"		},
	{ IO_READ,		"read"		},
};

/* Don't arrange wild! Look at the CNT_* order! */
struct socket_options socket_options[] = {
  {"SO_KEEPALIVE", SOL_SOCKET,  SO_KEEPALIVE, SVT_BOOL, 0, 0},
  {"SO_REUSEADDR", SOL_SOCKET,  SO_REUSEADDR, SVT_BOOL, 0, 0},
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, 0, 0},
  {"TCP_NODELAY",  IPPROTO_TCP, TCP_NODELAY,  SVT_BOOL, 0, 0},
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

#define	MAX_STATLEN 4096

static void
print_analyse(FILE *out)
{
	int written = 0;
	struct timeval tv_tmp;
	struct utsname utsname;
	double total_real, delta_time, delta_time_2, tmp;
	char buf[MAX_STATLEN];

	if (!uname(&utsname)) {
		fprintf(out, "%s (kernel: %s, arch: %s)\n",
				utsname.nodename, utsname.release, utsname.machine);
	}

	written += snprintf(buf + written, MAX_STATLEN - written,
			"Statistic\n");

	fprintf(out, "%s", buf);


	fprintf(out, "Netsend Statistic:\n\n"
			"Network Data:\n"
			"MTU:                   %d\n"
			"IO Operations:\n"
			"Read Calls:            %u\n"
			"Read Bytes:            %zd\n"
			"Write/Sendefile Calls: %u\n"
			"Write/Sendefile Bytes: %zd\n",
			net_stat.mss,
			net_stat.total_rx_calls, net_stat.total_rx_bytes,
			net_stat.total_tx_calls, net_stat.total_tx_bytes);

	subtime(&net_stat.use_stat_end.time, &net_stat.use_stat_start.time, &tv_tmp);
	total_real = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	if (total_real <= 0.0)
		total_real = 0.0001;

	fprintf(out, "# real: %.5f sec\n", total_real);
	fprintf(out, "# %.5f KB/sec\n", (((double)net_stat.total_tx_bytes) / total_real) / 1024);

#ifdef HAVE_RDTSCLL
	fprintf(out, "# %lld cpu cycles\n",
			tsc_diff(net_stat.use_stat_end.tsc, net_stat.use_stat_start.tsc));
#endif

	fputs("\n", stderr); /* barrier */

	subtime(&net_stat.use_stat_end.ru.ru_utime, &net_stat.use_stat_start.ru.ru_utime, &tv_tmp);
	delta_time = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	fprintf(out, "# utime: %.5f sec\n", delta_time);

	subtime(&net_stat.use_stat_end.ru.ru_stime, &net_stat.use_stat_start.ru.ru_stime, &tv_tmp);
	delta_time_2 = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	fprintf(out, "# stime: %.5f sec\n", delta_time_2);

	tmp = delta_time + delta_time_2;

	if (tmp <= 0.0)
		tmp = 0.0001;

	fprintf(out, "# cpu:   %.5f sec (CPU %.2f%%)\n", tmp, (tmp / total_real ) * 100 );


}


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

	/* Branch to final workmode ... */
	switch (opts.workmode) {
		case MODE_TRANSMIT:
			transmit_mode();
			break;
		case MODE_RECEIVE:
			receive_mode();
			break;
		default:
			err_msg("Programmed Failure");
			exit(EXIT_FAILMISC);
	}

	if (opts.statistics) {
		print_analyse(stderr);
	}

	return ret;
}



/* vim:set ts=4 sw=4 tw=78 noet: */
