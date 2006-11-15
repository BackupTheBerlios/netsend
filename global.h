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


#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"
#ifdef HAVE_RDTSCLL
# include <linux/timex.h>
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

#ifndef DCCP_SOCKOPT_PACKET_SIZE
# define DCCP_SOCKOPT_PACKET_SIZE 1
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

/* dccp packet default size */
#define	DCCP_STD_PACKET_SIZE 256

/* Our makros start here */

#define NIPQUAD(addr)   ((unsigned char *)&addr)[0], \
                        ((unsigned char *)&addr)[1], \
                        ((unsigned char *)&addr)[2], \
                        ((unsigned char *)&addr)[3]

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

/* Netsend return codes */
#define	EXIT_OK       0
#define	EXIT_FAILMEM  1
#define	EXIT_FAILOPT  2
#define	EXIT_FAILMISC 3
#define	EXIT_FAILNET  4
#define	EXIT_FAILINT  5 /* INTernal error */

/* Verbose levels */
#define	VL_QUITSCENT(x)  (x)
#define	VL_GENTLE(x)     (x >= 1)
#define	VL_LOUDISH(x)    (x >= 2)
#define	VL_STRESSFUL(x)  (x >= 3)

#define	PROGRAMNAME   "netsend"
#define	VERSIONSTRING "0.01"

/* Default values */
#define	DEFAULT_PORT    "6666"
#define	BACKLOG         0
#define	DEFAULT_BUFSIZE (8 * 1024)

enum sockopt_val_types {
	SVT_BOOL = 0,
	SVT_INT,
	SVT_ON
};

struct socket_options {
	const char *sockopt_name;
	int   level;
	int   option;
	int   sockopt_type;
	int   user_issue;
	int   value;
};

/* NOTE: if you rerange here - rerange also in main.c */
#define	CNT_SO_KEEPALIVE             0
#define	CNT_SO_REUSEADDR             1
#define	CNT_SO_BROADCAST             2
#define	CNT_TCP_NODELAY              3
#define	CNT_SO_SNDBUF                4
#define	CNT_SO_RCVBUF                5
#define	CNT_SO_SNDLOWAT              6
#define	CNT_SO_RCVLOWAT              7
#define	CNT_SO_SNDTIMEO              8
#define	CNT_SO_RCVTIMEO              9
#define	CNT_DCCP_SOCKOPT_PACKET_SIZE 10

struct conf_map_t {
	int          conf_code;
	const char  *conf_string;
};

/* Supported congestion algorithms by netsend */
enum {
	CA_BIC = 0,
	CA_WESTWOOD,
	CA_VEGAS,
	CA_SCALABLE,
	CA_HYBLA,
	CA_HTCP,
	CA_CUBIC,
	CA_RENO,
};
#define CA_MAX CA_RENO

/* CA defaults to bic (binary increase congestion avoidance) */
#define	CA_DEFAULT CA_BIC



/* supported posix_madvise / posix_fadvice hints */
enum memaccess_advice {
	MEMADV_NONE = 0,
	MEMADV_NORMAL,
	MEMADV_RANDOM,
	MEMADV_SEQUENTIAL,
	MEMADV_WILLNEED,
	MEMADV_DONTNEED,
	MEMADV_NOREUSE /* POSIX_FADV_NOREUSE */
};
#define MEMADV_MAX	MEMADV_NOREUSE

/* Supported io operations */

enum io_call {
	IO_MMAP = 0,
	IO_SENDFILE,
	IO_RW,
	IO_READ,
};
#define	IO_MAX IO_READ

/* Centralize our statistic data */

struct use_stat {
	struct timeval time;
	struct rusage  ru;
#ifdef HAVE_RDTSCLL
	long long      tsc;
#endif
};

struct net_stat {
	int  mss;
	int  keep_alive;

	unsigned int total_rx_calls;
	ssize_t      total_rx_bytes;

	unsigned int total_tx_calls;
	ssize_t      total_tx_bytes;

	struct use_stat use_stat_start;
	struct use_stat use_stat_end;
};

/* Command-line options */


enum workmode { MODE_NONE = 0, MODE_TRANSMIT, MODE_RECEIVE };

struct opts {
	int family;
	int protocol;
	int socktype;
	int reuse;
	int nodelay;
	int change_congestion;
	int congestion;
	int mem_advice;

	int   verbose;
	int   statistics;
	char *me;

	char           *port;
	char           *hostname;
	char           *infile;
	char		   *outfile;
	char           *execstring;
	enum workmode  workmode;
	enum io_call   io_call;

	/* if user set multiple_barrier then
	** (buffer_size * multiple_barrier)
	** is the maximum transfer amount
	*/
	int buffer_size;
	int multiple_barrier;

	struct timeval starttime;
	struct timeval endtime;

	int	sched_user; /* this is true if user wan't to change scheduling */
	int sched_policy;
	int priority;
	int nice;
};

/* error handling macros */
#define err_msg(format, args...) \
	do { \
		x_err_ret(__FILE__, __LINE__,  format , ## args); \
	} while (0)

#define err_sys(format, args...) \
	do { \
		x_err_sys(__FILE__, __LINE__,  format , ## args); \
	} while (0)

#define err_sys_die(exitcode, format, args...) \
	do { \
		x_err_sys(__FILE__, __LINE__, format , ## args); \
		exit( exitcode ); \
	} while (0)

#define err_msg_die(exitcode, format, args...) \
	do { \
		x_err_ret(__FILE__, __LINE__,  format , ## args); \
		exit( exitcode ); \
	} while (0)

/* abort if buffer is not large enough */
#define DO_SNPRINTF( buf, len, fmt, ... ) ({ \
        int _xlen = snprintf((buf), (len), fmt, __VA_ARGS__ ); \
        if (_xlen < 0 || _xlen >= (len)) \
                err_msg_die(EXIT_FAILINT, "buflen %u not sufficient (ret %d)", (len), _xlen); \
        _xlen; \
})


enum {
	QUITSCENT = 0,
	GENTLE,
	LOUDISH,
	STRESSFUL
};

/*** Interface ***/

/* error.c */
void x_err_ret(const char *file, int line_no, const char *, ...);
void x_err_sys(const char *file, int line_no, const char *, ...);
void msg(const int, const char *, ...);
void print_bt(void);


/* xfunc.c definitions */
void * alloc(size_t);
void * salloc(int, size_t);
#define	zalloc(x) salloc(0, x)


enum where_send {
	TOUCH_BEFORE_SEND = 0,
    TOUCH_AFTER_SEND
};
#define TIME_GT(x,y) (x->tv_sec > y->tv_sec || (x->tv_sec == y->tv_sec && x->tv_usec > y->tv_usec))
#define TIME_LT(x,y) (x->tv_sec < y->tv_sec || (x->tv_sec == y->tv_sec && x->tv_usec < y->tv_usec))
unsigned long long tsc_diff(unsigned long long, unsigned long long);
int subtime(struct timeval *, struct timeval *, struct timeval *);

/* Gcc is smart enough to realize that argument 'where' is static
** at compile time and reorder the branch - this is tested!
** Through this optimization our rdtscll call is closer
** to send routine and therefor accurater.
** --HGN
*/
static inline void
touch_use_stat(enum where_send where, struct use_stat *use_stat)
{

	if (where == TOUCH_BEFORE_SEND) {
		if (getrusage(RUSAGE_SELF, &use_stat->ru) < 0)
			err_sys("Failure in getrusage()");
		if (gettimeofday(&use_stat->time, NULL) < 0)
			err_sys("Failure in gettimeofday()");
#ifdef HAVE_RDTSCLL
		rdtscll(use_stat->tsc);
#endif
	} else { /* TOUCH_AFTER_SEND */
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
int parse_opts(int, char **);

/* net.c */
void xgetaddrinfo(const char *, const char *,
		struct addrinfo *, struct addrinfo **);
int get_sock_opts(int, struct net_stat *);
void change_congestion(int fd);


/* receive.c */
void receive_mode(void);

/* transmit.c */
void transmit_mode(void);





/* vim:set ts=4 sw=4 tw=78 noet: */
