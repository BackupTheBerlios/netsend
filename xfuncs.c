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
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/utsname.h>

#include <limits.h>

#ifndef ULLONG_MAX
# define ULLONG_MAX 18446744073709551615ULL
#endif

#include "global.h"

struct net_stat net_stat;
struct opts opts;

/* abort if buffer is not large enough */
#define DO_SNPRINTF( buf, len, fmt, ... ) ({ \
        int _xlen = snprintf((buf), (len), fmt, __VA_ARGS__ ); \
        if (_xlen < 0 || ((size_t)_xlen) >= (len)) \
                err_msg_die(EXIT_FAILINT, "buflen %u not sufficient (ret %d)", (len), _xlen); \
        _xlen; \
})

#define	T2S(x) ((opts.statistics > 1) ? statistic_map[x].l_name : statistic_map[x].s_name)


struct statistic_map_t
{
	const char *s_name;
	const char *l_name;
} statistic_map[] =
{
#define	STAT_MTU      0
	{ "mtu:        ", "Maximum Transfer Unit:        " },

#define	STAT_RX_CALLS 1
	{ "rx-calls:   ", "Number of read calls:         " },
#define	STAT_RX_BYTES 2
	{ "rx-amount:  ", "Received data quantum:        " },
#define	STAT_TX_CALLS 3
	{ "tx-calls:   ", "Number of write calls:        " },
#define	STAT_TX_BYTES 4
	{ "tx-amount:  ", "Transmitted data quantum:     " },

#define	STAT_REAL_TIME 5
	{ "real:       ", "Cumulative real time:         " },
#define	STAT_UTIME 6
	{ "utime:      ", "Cumulative user space time:   " },
#define	STAT_STIME 7
	{ "stime:      ", "Cumulative kernel space time: " },
#define	STAT_CPU_TIME 8
	{ "cpu:        ", "Cumulative us/ks time:        " },
#define	STAT_CPU_CYCLES 9
	{ "cpu-ticks:  ", "CPU ticks since programm start:" },
#define	STAT_THROUGH 10
	{ "throughput: ", "Throughput:                   :" },
};

#define	MAX_STATLEN 4096

#define	KiB (1024)
#define	MiB (KiB * 1024)
#define	GiB (MiB * 1024)

#define	kB (1000)
#define	MB (kB * 1000)
#define	GB (MB * 1000)

void
print_analyse(FILE *out)
{
	int len = 0;
	char buf[MAX_STATLEN];
	struct timeval tv_tmp;
	struct utsname utsname;
	double total_real, total_utime, total_stime, total_cpu;
	double throughput;

	if (uname(&utsname))
		*utsname.nodename = *utsname.release = *utsname.machine = 0;

	len += DO_SNPRINTF(buf + len, sizeof buf - len,
			"\n** %s statistics (%s | %s | %s) ** \n",
			opts.workmode == MODE_TRANSMIT ? "tx" : "rx",
			utsname.nodename, utsname.release, utsname.machine);

	if (opts.workmode == MODE_TRANSMIT) {

		const char *tx_call_str;

		/* display system call count */
		switch (opts.io_call) {
			case IO_SENDFILE: tx_call_str = "sendfile"; break;
			case IO_MMAP: tx_call_str = "mmap"; break;
			case IO_RW: tx_call_str = "write"; break;
			default: tx_call_str = ""; break;
		}

		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %d (%s)\n",
				T2S(STAT_TX_CALLS),
				net_stat.total_tx_calls, tx_call_str);

		/* display data amount */
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %zd Byte",
				T2S(STAT_TX_BYTES), net_stat.total_tx_bytes);
		if ( (net_stat.total_tx_bytes / KiB) > 1) { /* display KiB */
			len += DO_SNPRINTF(buf + len, sizeof buf - len, " (%zd KiB",
					(net_stat.total_tx_bytes / KiB));
			if ( (net_stat.total_tx_bytes / MiB) > 1) { /* display MiB */
				len += DO_SNPRINTF(buf + len, sizeof buf - len, ", %zd MiB",
						(net_stat.total_tx_bytes / MiB));
			}
			len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s", ")");
		}
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s", "\n"); /* newline */

	} else { /* MODE_RECEIVE */

		/* display system call count */
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %d (read)\n",
				T2S(STAT_RX_CALLS),
				net_stat.total_rx_calls);

		/* display data amount */
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %zd Byte",
				T2S(STAT_RX_BYTES), net_stat.total_rx_bytes);
		if ( (net_stat.total_rx_bytes / KiB) > 1) { /* display KiB */
			len += DO_SNPRINTF(buf + len, sizeof buf - len, " (%zd KiB",
					(net_stat.total_rx_bytes / KiB));
			if ( (net_stat.total_rx_bytes / MiB) > 1) { /* display MiB */
				len += DO_SNPRINTF(buf + len, sizeof buf - len, ", %zd MiB",
						(net_stat.total_rx_bytes / MiB));
			}
		}
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s", ")\n"); /* newline */
	}

	subtime(&net_stat.use_stat_end.time, &net_stat.use_stat_start.time, &tv_tmp);
	total_real = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	if (total_real <= 0.0)
		total_real = 0.00001;

	/* real time */
	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %.4f sec\n",
			T2S(STAT_REAL_TIME), total_real);

	/* user, system, cpu-time & cpu cycles */
	subtime(&net_stat.use_stat_end.ru.ru_utime, &net_stat.use_stat_start.ru.ru_utime, &tv_tmp);
	total_utime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	subtime(&net_stat.use_stat_end.ru.ru_stime, &net_stat.use_stat_start.ru.ru_stime, &tv_tmp);
	total_stime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	total_cpu = total_utime + total_stime;
	if (total_cpu <= 0.0)
		total_cpu = 0.0001;

	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %.4f sec\n",
			T2S(STAT_UTIME), total_utime);
	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %.4f sec\n",
			T2S(STAT_STIME), total_stime);
	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %.4f sec (cpu/real: %.2f%%)\n",
			T2S(STAT_CPU_TIME), total_cpu, (total_cpu / total_real ) * 100);
#ifdef HAVE_RDTSCLL
	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %lld cycles\n",
			T2S(STAT_CPU_CYCLES),
			tsc_diff(net_stat.use_stat_end.tsc, net_stat.use_stat_start.tsc));
#endif


	/* throughput */
	throughput = opts.workmode == MODE_TRANSMIT ?
		((double)net_stat.total_tx_bytes) / total_real :
		((double)net_stat.total_rx_bytes) / total_real;

	len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s %.5f KiB/sec",
			T2S(STAT_THROUGH), throughput / KiB);
	if ((throughput / MiB) > 1) {
		len += DO_SNPRINTF(buf + len, sizeof buf - len, " (%.5f MiB/sec",
				(throughput / MiB));
		if ((throughput / GiB) > 1) {
			len += DO_SNPRINTF(buf + len, sizeof buf - len, ", %.5f GiB/sec",
					(throughput / GiB));
		}
		len += DO_SNPRINTF(buf + len, sizeof buf - len, "%s", ")\n"); /* newline */
	}

	fprintf(out, "%s", buf);
	fflush(out);

}

#undef T2S
#undef DO_SNPRINTF
#undef MAX_STATLEN


/* Simple malloc wrapper - prevent error checking */
void *
alloc(size_t size) {

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	return ptr;
}

void *
salloc(int c, size_t size){

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	memset(ptr, c, size);

	return ptr;
}

unsigned long long
tsc_diff(unsigned long long end, unsigned long long start)
{
	long long ret = end - start;

	if (ret >= 0)
		return ret;

	return (ULLONG_MAX - start) + end;
}

int
subtime(struct timeval *op1, struct timeval *op2, struct timeval *result)
{
	int borrow = 0, sign = 0;
	struct timeval *temp_time;

	if (TIME_LT(op1, op2)) {
		temp_time = op1;
		op1  = op2;
		op2  = temp_time;
		sign = 1;
	}
	if (op1->tv_usec >= op2->tv_usec) {
		result->tv_usec = op1->tv_usec-op2->tv_usec;
	}
	else {
		result->tv_usec = (op1->tv_usec + 1000000) - op2->tv_usec;
		borrow = 1;
	}
	result->tv_sec = (op1->tv_sec-op2->tv_sec) - borrow;

	return sign;
}


/* vim:set ts=4 sw=4 tw=78 noet: */
