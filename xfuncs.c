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
#include <unistd.h>

#include <sys/time.h>
#include <sys/utsname.h>

#include <limits.h>

#ifndef ULLONG_MAX
# define ULLONG_MAX 18446744073709551615ULL
#endif

#include "global.h"
#include "xfuncs.h"

extern struct net_stat net_stat;
extern struct conf_map_t memadvice_map[];
extern struct opts opts;

/* abort if buffer is not large enough */
#define DO_SNPRINTF( buf, len, fmt, ... ) ({ \
        int _xlen = snprintf((buf), (len), fmt, __VA_ARGS__ ); \
        if (_xlen < 0 || ((size_t)_xlen) >= (len)) \
                err_msg_die(EXIT_FAILINT, "Buflen %u not sufficient (ret %d)", (len), _xlen); \
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
	{ "rx-calls:    ", "Number of read calls:         " },
#define	STAT_RX_BYTES 2
	{ "rx-amount:   ", "Received data quantum:        " },
#define	STAT_TX_CALLS 3
	{ "tx-calls:    ", "Number of write calls:        " },
#define	STAT_TX_BYTES 4
	{ "tx-amount:   ", "Transmitted data quantum:     " },

#define	STAT_REAL_TIME 5
	{ "real:        ", "Cumulative real time:         " },
#define	STAT_UTIME 6
	{ "utime:       ", "Cumulative user space time:   " },
#define	STAT_STIME 7
	{ "stime:       ", "Cumulative kernel space time: " },
#define	STAT_CPU_TIME 8
	{ "cpu:         ", "Cumulative us/ks time:        " },
#define	STAT_CPU_CYCLES 9
	{ "cpu-ticks:   ", "CPU ticks since programm start:" },
#define	STAT_THROUGH 10
	{ "throughput:  ", "Throughput:                    " },
#define	STAT_SWAPS 11
	{ "swaps:       ", "Times process swapped:         " },
#define	STAT_MAXRSS 12
	{ "maxrss:      ", "Resident set memory used:      " },
#define	STAT_VCS 13
	{ "voluntary cs:", "Voluntary context switches:    " },
#define	STAT_NICECS 14
	{ "nice cs:     ", "Nice context switches:         " },
};


/* unit stuff */
struct unit_map_t
{
	const char *name_short;
	const char *name_long;
	const uint64_t factor;
} unit_map[] =
{
	{ NULL, NULL, 1 },

	/* 2**n (Binary prefixes, IEC 60027-2) */
#define KiB 1
	{ "KiB",   "kibibyte", 1024 },
#define	Kibit 2
	{ "Kibit", "kibit",    128 },
#define	MiB 3
	{ "MiB",   "mebibyte", 1048576},
#define	Mibit 4
	{ "Mibit", "mebibit",  131072},
#define	GiB 5
	{ "GiB",   "gibibyte", 1073741824LL},
#define	Gibit 6
	{ "Gibit", "gibibit",  134217728LL},

	/* 10**n (SI prefixes) */
#define	kB 7
	{ "kB",    "kilobyte", 1000 },
#define	kb 8
	{ "kbit",  "kilobit",  125 },
#define	MB 9
	{ "MB",    "megabyte", 1000000 },
#define	Mb 10
	{ "Mbit",  "megabit",  125000 },
#define	GB 11
	{ "GB",    "gigabyte", 1000000000LL },
#define	Gb 12
	{ "Gbit",  "gigabit",  125000000LL },
};

#define	K_UNIT ( (opts.stat_prefix == STAT_PREFIX_SI) ? \
		((opts.stat_unit == BYTE_UNIT) ? kB   : kb) : \
		((opts.stat_unit == BYTE_UNIT) ? KiB  : Kibit) )
#define	M_UNIT ( (opts.stat_prefix == STAT_PREFIX_SI) ? \
		((opts.stat_unit == BYTE_UNIT) ? MB   : Mb) : \
		((opts.stat_unit == BYTE_UNIT) ? MiB  : Mibit) )
#define	G_UNIT ( (opts.stat_prefix == STAT_PREFIX_SI) ? \
		((opts.stat_unit == BYTE_UNIT) ? GB   : Gb) : \
		((opts.stat_unit == BYTE_UNIT) ? GiB  : Gibit) )

#define	UNIT_MAX 128

static char *
unit_conv(char *buf, int buf_len, ssize_t bytes, int unit_scale)
{
	int ret;
	ssize_t res = bytes / unit_map[unit_scale].factor;

	ret = snprintf(buf, buf_len, "%zd %s", res,
			(opts.statistics > 1) ? unit_map[unit_scale].name_long :
			unit_map[unit_scale].name_short);
	if (ret < 0 || (ssize_t)ret >= buf_len) {
		err_msg_die(EXIT_FAILINT, "Buflen %u not sufficient (ret %d)",
				buf_len, ret);
	}
	return buf;
}

#define	UNIT_CONV(byte, unit_scale) (unit_conv(unit_buf, UNIT_MAX, byte, unit_scale))
#define	UNIT_N2F(x) (unit_map[x].factor)
#define	UNIT_N2S(x) ((opts.statistics > 1) ? \
		unit_map[x].name_long : \
		unit_map[x].name_short)

void
gen_human_analyse(char *buf, unsigned int max_buf_len)
{
	int len = 0, page_size;
	char unit_buf[UNIT_MAX];
	struct timeval tv_tmp;
	struct utsname utsname;
	double total_real, total_utime, total_stime, total_cpu;
	double throughput;

	page_size = getpagesize();

	if (uname(&utsname))
		*utsname.nodename = *utsname.release = *utsname.machine = 0;

	len += DO_SNPRINTF(buf + len, max_buf_len - len,
			"\n** %s statistics (%s | %s | %s) ** \n",
			opts.workmode == MODE_TRANSMIT ? "tx" : "rx",
			utsname.nodename, utsname.release, utsname.machine);

	if (opts.workmode == MODE_TRANSMIT) {

		const char *tx_call_str;

		/* display system call count */
		switch (opts.io_call) {
			case IO_SENDFILE: tx_call_str = "sendfile"; break;
			case IO_MMAP:     tx_call_str = "mmap"; break;
			case IO_RW:       tx_call_str = "write"; break;
			default:          tx_call_str = ""; break;
		}

		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %d (%s)\n",
				T2S(STAT_TX_CALLS),
				net_stat.total_tx_calls, tx_call_str);

		/* display data amount */
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %zd %s",
				T2S(STAT_TX_BYTES), opts.stat_unit == BYTE_UNIT ?
				net_stat.total_tx_bytes : net_stat.total_tx_bytes * 8,
				opts.stat_unit == BYTE_UNIT ? "Byte" : "Bit");

		if ( (net_stat.total_tx_bytes / UNIT_N2F(K_UNIT)) > 1) { /* display Kilo */
			len += DO_SNPRINTF(buf + len, max_buf_len - len, " (%s",
					UNIT_CONV(net_stat.total_tx_bytes, K_UNIT));

			if ( (net_stat.total_tx_bytes / UNIT_N2F(M_UNIT)) > 1) { /* display mega */
				len += DO_SNPRINTF(buf + len, max_buf_len - len, ", %s",
						UNIT_CONV(net_stat.total_tx_bytes, M_UNIT));
			}
			len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s", ")");
		}
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s", "\n"); /* newline */

	} else { /* MODE_RECEIVE */

		/* display system call count */
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %d (read)\n",
				T2S(STAT_RX_CALLS),
				net_stat.total_rx_calls);

		/* display data amount */
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %zd %s",
				T2S(STAT_RX_BYTES), opts.stat_unit == BYTE_UNIT ?
				net_stat.total_rx_bytes : net_stat.total_rx_bytes * 8,
				opts.stat_unit == BYTE_UNIT ? "Byte" : "Bit");

		if ( (net_stat.total_rx_bytes / UNIT_N2F(K_UNIT)) > 1) { /* display kilo */

			len += DO_SNPRINTF(buf + len, max_buf_len - len, " (%s",
					UNIT_CONV(net_stat.total_rx_bytes, K_UNIT));

			if ( (net_stat.total_rx_bytes / UNIT_N2F(M_UNIT)) > 1) { /* display mega */
				len += DO_SNPRINTF(buf + len, max_buf_len - len, ", %s",
						UNIT_CONV(net_stat.total_rx_bytes, M_UNIT));
			}
		}
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s", ")\n"); /* newline */
	}

	subtime(&net_stat.use_stat_end.time, &net_stat.use_stat_start.time, &tv_tmp);
	total_real = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	if (total_real <= 0.0)
		total_real = 0.00001;

	/* real time */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %.4f sec\n",
			T2S(STAT_REAL_TIME), total_real);

	/* user, system, cpu-time & cpu cycles */
	subtime(&net_stat.use_stat_end.ru.ru_utime, &net_stat.use_stat_start.ru.ru_utime, &tv_tmp);
	total_utime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	subtime(&net_stat.use_stat_end.ru.ru_stime, &net_stat.use_stat_start.ru.ru_stime, &tv_tmp);
	total_stime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	total_cpu = total_utime + total_stime;
	if (total_cpu <= 0.0)
		total_cpu = 0.0001;

	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %.4f sec\n",
			T2S(STAT_UTIME), total_utime);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %.4f sec\n",
			T2S(STAT_STIME), total_stime);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %.4f sec (cpu/real: %.2f%%)\n",
			T2S(STAT_CPU_TIME), total_cpu, (total_cpu / total_real ) * 100);
#ifdef HAVE_RDTSCLL
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %lld cycles\n",
			T2S(STAT_CPU_CYCLES),
			tsc_diff(net_stat.use_stat_end.tsc, net_stat.use_stat_start.tsc));
#endif

	if (opts.verbose >= LOUDISH) {

		long res;

		/* test for signed long overflow in subtraction */
		if (((net_stat.use_stat_end.ru.ru_nswap ^ net_stat.use_stat_start.ru.ru_nswap) &
			((net_stat.use_stat_end.ru.ru_nswap - net_stat.use_stat_start.ru.ru_nswap) ^
			 net_stat.use_stat_end.ru.ru_nswap)) >> (sizeof(long)*CHAR_BIT-1) ) {
			err_sys("Overlow in long substraction");
		}
		res = net_stat.use_stat_end.ru.ru_nswap - net_stat.use_stat_start.ru.ru_nswap;

		/* swaps */
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %ld \n",
				T2S(STAT_SWAPS), res);

		/* voluntary context switches */
		res = sublong(net_stat.use_stat_end.ru.ru_nvcsw,
				net_stat.use_stat_start.ru.ru_nvcsw);
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %ld\n",
		T2S(STAT_VCS), res);

		/* nice (involuntary) context switches */
		res = sublong(net_stat.use_stat_end.ru.ru_nivcsw,
				net_stat.use_stat_start.ru.ru_nivcsw);
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %ld\n",
		T2S(STAT_NICECS), res);

#if 0
		/* ru_maxrss - bytes of resident set memory used */
		res = sublong(net_stat.use_stat_end.ru.ru_maxrss,
				net_stat.use_stat_start.ru.ru_maxrss);
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %ld bytes\n",
		T2S(STAT_MAXRSS), res * page_size);
#endif


	}

	/* throughput (bytes/s)*/
	throughput = opts.workmode == MODE_TRANSMIT ?
		((double)net_stat.total_tx_bytes) / total_real :
		((double)net_stat.total_rx_bytes) / total_real;


	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s %.5f %s/sec",
			T2S(STAT_THROUGH), throughput / UNIT_N2F(K_UNIT),
		    UNIT_N2S(K_UNIT));

	if ((throughput / UNIT_N2F(M_UNIT)) > 1) {
		len += DO_SNPRINTF(buf + len, max_buf_len - len, " (%.5f %s/sec",
				(throughput / UNIT_N2F(M_UNIT)), UNIT_N2S(M_UNIT));
		if ((throughput / UNIT_N2F(G_UNIT)) >= 1) {
			len += DO_SNPRINTF(buf + len, max_buf_len - len, ", %.5f %s/sec",
					(throughput / UNIT_N2F(G_UNIT)), UNIT_N2S(G_UNIT));
		}
		len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s", ")\n"); /* newline */
	}
}

#undef T2S

void
gen_mashine_analyse(char *buf, unsigned int max_buf_len)
{
	int len = 0, page_size; long res;
	const char *call_str;
	struct timeval tv_tmp;
	double total_real, total_utime, total_stime, total_cpu;

	page_size = getpagesize();

	/* XXX:
	** If you change the format or sequence
	**  1) increment VERSIONSTRING
	**  2) comment these in http://netsend.berlios.de/usag.html
	*/


	/* 1. versionstring */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s ", VERSIONSTRING);

	/* 2. workmode: rx || tx */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s ",
	(opts.workmode == MODE_TRANSMIT) ? "tx" : "rx");

	/* 3. utilized io call */
	if (opts.workmode == MODE_TRANSMIT) {
		switch (opts.io_call) {
			case IO_SENDFILE: call_str = "sendfile"; break;
			case IO_MMAP:     call_str = "mmap"; break;
			case IO_RW:       call_str = "write"; break;
			default: err_msg_die(EXIT_FAILINT, "Programmed Failure"); break;
		}
	} else {
		call_str = "read";
	}
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s ", call_str);

	/* memory advise */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s ",
			opts.change_mem_advise ?
			memadvice_map[opts.mem_advice].conf_string : "none");

	/* buffer size (only digits that a regex can match this entry
	** clean (\d+))
	*/
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%d ",
			opts.buffer_size ?
			opts.buffer_size : 0);

	/* nice level
	** FIXME: don't take opts.nice level, call getnice()
	*/
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%d ",
			opts.nice == INT_MAX ? 0 : opts.nice);

	/* 4. io-function call count */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%d ",
		(opts.workmode == MODE_TRANSMIT) ? net_stat.total_tx_calls :
		 net_stat.total_rx_calls);

	/* 5. byte transmitted/received */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%zd ",
		(opts.workmode == MODE_TRANSMIT) ? net_stat.total_tx_bytes :
		 net_stat.total_rx_bytes);

	/* 6. realtime */
	subtime(&net_stat.use_stat_end.time, &net_stat.use_stat_start.time, &tv_tmp);
	total_real = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	if (total_real <= 0.0)
		total_real = 0.00001;
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%.4f ", total_real);

	/* 7. user time */
	subtime(&net_stat.use_stat_end.ru.ru_utime, &net_stat.use_stat_start.ru.ru_utime, &tv_tmp);
	total_utime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%.4f ", total_utime);

	/* 8. system time */
	subtime(&net_stat.use_stat_end.ru.ru_stime, &net_stat.use_stat_start.ru.ru_stime, &tv_tmp);
	total_stime = tv_tmp.tv_sec + ((double) tv_tmp.tv_usec) / 1000000;
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%.4f ", total_stime);

	/* 9. cpu-time */
	total_cpu = total_utime + total_stime;
	if (total_cpu <= 0.0)
		total_cpu = 0.0001;
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%.4f ", total_cpu);

	/* 10. swaps (see http://www.khmere.com/freebsd_book/html/ch07.html */
	res = sublong(net_stat.use_stat_end.ru.ru_nswap, net_stat.use_stat_start.ru.ru_nswap);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%ld ", res);

	/* 11. voluntary context switches */
	res = sublong(net_stat.use_stat_end.ru.ru_nvcsw,
			net_stat.use_stat_start.ru.ru_nvcsw);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%ld ", res);

	/* 12. total number of context switches */
	res = sublong(net_stat.use_stat_end.ru.ru_nivcsw,
			net_stat.use_stat_start.ru.ru_nivcsw);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%ld ", res);

#if 0
	/* 11. ru_maxrss - bytes of resident set memory used */
	res = sublong(net_stat.use_stat_end.ru.ru_maxrss, net_stat.use_stat_start.ru.ru_maxrss);
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%ld ", res * page_size);
#endif


	/* trailing newline */
	len += DO_SNPRINTF(buf + len, max_buf_len - len, "%s", "\n");

}

#undef DO_SNPRINTF

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

long
sublong(long a, long b)
{
	if (((a ^ b) & ((a - b) ^ a)) >> ( sizeof(long) * CHAR_BIT - 1) ) {
		err_sys("Overlow in long substraction");
	}
	return a - b;
}

void xgetaddrinfo(const char *node, const char *service,
		struct addrinfo *hints, struct addrinfo **res)
{
	int ret;

	ret = getaddrinfo(node, service, hints, res);
	if (ret != 0) {
		err_msg_die(EXIT_FAILNET, "Call to getaddrinfo() failed: %s!\n",
				(ret == EAI_SYSTEM) ?  strerror(errno) : gai_strerror(ret));
	}
}


void xsetsockopt(int s, int level, int optname, const void *optval, socklen_t optlen, const char *str)
{
	int ret = setsockopt(s, level, optname, optval, optlen);
	if (ret)
		err_sys_die(EXIT_FAILNET, "Can't set socketoption %s", str);
}
/* vim:set ts=4 sw=4 tw=78 noet: */
