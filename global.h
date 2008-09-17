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


#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"
#include "error.h"
#ifdef HAVE_RDTSCLL
# include <linux/timex.h>

# ifndef rdtscll
#define rdtscll(val) \
     __asm__ __volatile__("rdtsc" : "=A" (val))
# endif
#endif /* HAVE_RDTSCLL */

#ifndef SOCK_DCCP
# define SOCK_DCCP 6
#endif

#ifndef SOL_DCCP
# define SOL_DCCP 269
#endif

#ifndef IPPROTO_DCCP
# define IPPROTO_DCCP 33
#endif

#ifndef DCCP_SOCKOPT_SERVICE
# define DCCP_SOCKOPT_SERVICE 2
#endif

#ifndef DCCP_SOCKOPT_CCID_RX_INFO
# define DCCP_SOCKOPT_CCID_RX_INFO 128
#endif

#ifndef DCCP_SOCKOPT_CCID_TX_INFO
# define DCCP_SOCKOPT_CCID_TX_INFO 192
#endif

#ifndef TCP_CONGESTION
# define TCP_CONGESTION  13
#endif

#ifndef SOL_SCTP
# define SOL_SCTP 132
#endif

#ifndef SCTP_MAXSEG
# define SCTP_MAXSEG    13
#endif

#ifndef IPPROTO_UDPLITE
# define IPPROTO_UDPLITE 136
#endif

#ifndef SCTP_DISABLE_FRAGMENTS
# define SCTP_DISABLE_FRAGMENTS	8
#endif

#ifndef UDPLITE_SEND_CSCOV
# define UDPLITE_SEND_CSCOV   10
#endif

#ifndef UDPLITE_RECV_CSCOV
# define UDPLITE_RECV_CSCOV   11
#endif

/* Forces a function to be always inlined
** 'must inline' - so that they get inlined even
** if optimizing for size
*/
#undef __always_inline
#if __GNUC_PREREQ (3,2)
# define __always_inline __inline __attribute__ ((__always_inline__))
#else
# define __always_inline __inline
#endif

#define min(x,y) ({			\
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);	\
	_x < _y ? _x : _y; })

#define max(x,y) ({			\
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);	\
	_x > _y ? _x : _y; })

#if !defined likely && !defined unlikely
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/* determine the size of an array */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Netsend return codes */
#define	EXIT_OK         0
#define	EXIT_FAILMEM    1
#define	EXIT_FAILOPT    2
#define	EXIT_FAILMISC   3
#define	EXIT_FAILNET    4
#define	EXIT_FAILHEADER 6
#define	EXIT_FAILINT    7 /* INTernal error */

#define SUCCESS 0
#define FAILURE -1

/* Verbose levels */
#define	VL_QUITSCENT(x)  (x)
#define	VL_GENTLE(x)     (x >= 1)
#define	VL_LOUDISH(x)    (x >= 2)
#define	VL_STRESSFUL(x)  (x >= 3)

#define	PROGRAMNAME   "netsend"
#define	VERSIONSTRING "002"

/* Default values */
#define	DEFAULT_PORT    "6666"
#define	BACKLOG         1
#define	DEFAULT_BUFSIZE (8 * 1024)

enum sockopt_val_types {
	SVT_BOOL = 0,
	SVT_INT,
	SVT_TIMEVAL,
	SVT_STR
};

struct socket_options {
	const char *sockopt_name;
	int   level;
	int   option;
	int   sockopt_type;
	bool  user_issue;
	union {
		int value;
		struct timeval tv;
		const char *value_ptr;
	};
};

struct conf_map_t {
	int          conf_code;
	const char  *conf_string;
};

/* supported posix_madvise / posix_fadvice hints */
enum memaccess_advice {
	MEMADV_NORMAL = 0,
	MEMADV_RANDOM,
	MEMADV_SEQUENTIAL,
	MEMADV_WILLNEED,
	MEMADV_DONTNEED,
	MEMADV_NOREUSE /* POSIX_FADV_NOREUSE */
};
#define MEMADV_MAX	MEMADV_NOREUSE

/* Supported io operations */

enum io_call {
	IO_RW,/* 0=default xmit method */
	IO_SENDFILE,
	IO_MMAP,
	IO_SPLICE
};
#define	IO_MAX IO_SPLICE

/* Centralize our statistic data */

struct use_stat {
	struct timeval time;
	struct rusage  ru;
#ifdef HAVE_RDTSCLL
	long long      tsc;
#endif
};

struct sock_stat {
	/* tcp attributes */
	uint16_t mss;
	int keep_alive;
	/* ip attributes */
};

struct net_stat {
	struct rtt_probe {
		double usec;
		double variance;
	} rtt_probe;
	struct sock_stat sock_stat;

	unsigned int total_rx_calls;
	unsigned long long total_rx_bytes;

	unsigned int total_tx_calls;
	unsigned long long total_tx_bytes;

	struct use_stat use_stat_start;
	struct use_stat use_stat_end;
};

/* this struct collect all information
 * sent by the peer (transmiter) and contain
 * information like data size, rtt information,
 * ... */
struct peer_header_info {
	unsigned int data_size; /* < the size of the incoming data */
};

/* Command-line options */

#define	BIT_UNIT  1
#define	BYTE_UNIT 2
#define	STAT_PREFIX_SI 1
#define	STAT_PREFIX_BINARY 2
enum workmode { MODE_NONE = 0, MODE_TRANSMIT, MODE_RECEIVE };


struct sock_callbacks {
	ssize_t (*cb_read)(int, void *, size_t);
	ssize_t (*cb_write)(int, const void *, size_t);
	int (*cb_listen)(int,int);
	int (*cb_accept)(int,struct sockaddr *addr,socklen_t *);
};

/* command line arguments */

#define	HDR_MSK_SOCKOPT (1 << 0)
#define HDR_MSK_DIGEST  (1 << 1)

/* bitmask set for short_opts_mask */
#define	SOPTS_VERSION      (1 << 1)
#define SOPTS_NUMERIC      (1 << 2)
#define	SOPTS_IPV4         (1 << 3)
#define	SOPTS_IPV6         (1 << 4)

enum ns_proto {
	NS_PROTO_UNSPEC = 0,
	NS_PROTO_TCP,
	NS_PROTO_UDP,
	NS_PROTO_UDPLITE,
	NS_PROTO_DCCP,
	NS_PROTO_TIPC,
	NS_PROTO_SCTP
};

struct opts {

	unsigned long short_opts_mask;

	enum ns_proto ns_proto;

	int family;
	int protocol;
	int socktype;
	int reuse;
	int nodelay;
	int mem_advice;
	int change_mem_advise;

	long ext_hdr_mask;

	long threads; /* < number of threads to parallelize transmit stream */

	int  verbose;
	int  statistics;
	int  machine_parseable;
	int  stat_unit;
	int  stat_prefix;
	const char *me;

	const char *port;
	const char *hostname;
	const char *infile;
	const char *outfile;
	enum workmode  workmode;
	enum io_call   io_call;

	/* if user set multiple_barrier then
	** (buffer_size * multiple_barrier)
	** is the maximum transfer amount
	*/
	int buffer_size;
	int multiple_barrier;

	int	sched_user; /* this is true if user wan't to change scheduling */
	int sched_policy;
	int priority;
	long nice;

	long int udplite_checksum_coverage;

	bool tcp_use_md5sig;
	const char *tcp_md5sig_peeraddr; /* receive mode: need ip addr of peer allowed to connect */

#define	DEFAULT_RTT_FILTER 4

	/* this stores option for the rtt probe commandline option '-R' */
	struct rtt_probe_opt {
		int iterations;
		int data_size;
		int deviation_filter;
		int force_ms;
	} rtt_probe_opt;
	int perform_rtt_probe;
};

/*** Interface ***/

enum where_send {
    TOUCH_BEFORE_OP = 0,
    TOUCH_AFTER_OP
};


/* Gcc is smart enough to realize that argument 'where' is static
** at compile time and reorder the branch - this is tested!
** Through this optimization our rdtscll call is closer
** to send routine and therefor accurater.
** --HGN
*/
static inline void
touch_use_stat(enum where_send where, struct use_stat *use_stat)
{

	if (where == TOUCH_BEFORE_OP) {
		if (getrusage(RUSAGE_SELF, &use_stat->ru) < 0)
			err_sys("Failure in getrusage()");
		if (gettimeofday(&use_stat->time, NULL) < 0)
			err_sys("Failure in gettimeofday()");
#ifdef HAVE_RDTSCLL
		rdtscll(use_stat->tsc);
#endif
	} else { /* TOUCH_AFTER_OP */
#ifdef HAVE_RDTSCLL
		rdtscll(use_stat->tsc);
#endif
		if (gettimeofday(&use_stat->time, NULL) < 0)
			err_sys("Failure in gettimeofday()");
		if (getrusage(RUSAGE_SELF, &use_stat->ru) < 0)
			err_sys("Failure in getrusage()");
	}
	return;
};



/* file.c */
int open_input_file(void);
int open_output_file(void);

/* getopt.c */
void usage(void);
void parse_opts(int, char **, struct opts *);

/* net.c */
int get_sock_opts(int, struct net_stat *);
int set_nodelay(int, int);
void set_socketopts(int fd);

/* ns_hdr.c */
int meta_exchange_snd(int, int);
int meta_exchange_rcv(int, struct peer_header_info **);

/* receive.c */
void receive_mode(void);

/* transmit.c */
void transmit_mode(void);

void udp_trans_mode(void);
void udplite_trans_mode(void);
void tipc_trans_mode(void);
void dccp_trans_mode(void);
void sctp_trans_mode(void);

/* proto_udplite_recv.c */
int init_receive_socket_udplite(struct opts *, int);

/* trans_common.c */
void trans_start(int, int);
void ip_stream_trans_mode(struct opts*, int proto);

/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
