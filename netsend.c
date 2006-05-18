/*
** $Id$
**
** netsend - a high performance filetransfer and diagnostic tool
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

/* TODO:
**
** o A proper Largefile support
** o Broadcast/Multicast support to find server whitout knowing
**   the server address
** o Option to autotune recv buffer (like in web100 ftp suite)
** o Option to display statistic
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>
#include <netdb.h>

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <linux/types.h>


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

/* Our makros start here */

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

#ifdef DEBUG
# define XDEBUG(format, args...) fprintf(stderr, format, ## args)
#else
# define XDEBUG(format, args...)
#endif

/* Netsend return codes */
#define	EXIT_OK       0
#define	EXIT_FAILMEM  1
#define	EXIT_FAILOPT  2
#define	EXIT_FAILMISC 3
#define	EXIT_FAILNET  4

/* Verbose levels */
#define	VL_QUITSCENT(x)  (x)
#define	VL_GENTLE(x)     (x >= 1)
#define	VL_LOUDISH(x)    (x >= 2)
#define	VL_STRESSFUL(x)  (x >= 3)

#define	PROGRAMNAME   "netsend"
#define	VERSIONSTRING "0.01"

/* Default values */

#define	DEFAULT_PORT    6666
#define	BACKLOG         0
#define	DEFAULT_BUFSIZE (8 * 1024)

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

struct conf_map_t congestion_map[] = {
	{ CA_BIC,      "bic"      },
	{ CA_WESTWOOD, "westwood" },
	{ CA_VEGAS,    "vegas"    },
	{ CA_SCALABLE, "scalable" },
	{ CA_HYBLA,    "hybla"    },
	{ CA_HTCP,     "htcp"     },
	{ CA_CUBIC,    "cubic"    },
	{ CA_RENO,     "reno"     },
};


/* Supported io operations */

enum io_call {
	IO_MMAP = 0,
	IO_SENDFILE,
	IO_RW,
	IO_READ,
};
#define	IO_MAX IO_READ

struct conf_map_t io_call_map[] = {
	{ IO_MMAP,     "mmap"     },
	{ IO_SENDFILE, "sendfile" },
	{ IO_RW,       "rw"    },
	{ IO_READ,     "read"     },
};


/* Centralize our statistic data */

struct net_stat {
	int mtu;
	int read_call_cnt;
};

struct net_stat net_stat;


/* Command-line options */

enum workmode { MODE_SERVER = 0, MODE_CLIENT };

struct opts {
	int           family;
	int           socktype;
	int           reuse;
	int           change_congestion;
	int           congestion;
	int           verbose;
	uint16_t      port;
	char          *me;
	char          *hostname;
	char          *filename;
	enum workmode workmode;
	enum io_call  io_call;
	int           protocol;
	int           buffer_size;
};

struct opts opts;

/* getopt stuff */
extern char *optarg;
extern int opterr;

static void
usage(void)
{
	fprintf(stderr,
			"USAGE: %s [options] ( -t filename  |  -r hostname )\n\n"
			"-m <tcp | udp | dccp>    specify default transfer protocol (default: tcp)\n"
			"-p <port>                set portnumber (default: 6666)\n"
			"-u <sendfile | mmap | rw | read >\n"
			"                         utilize specific functioncall for IO operation\n"
			"                         (this depends on operation mode (-t, -r)\n"
			"-c <congestion>          set the congestion algorithm\n"
			"       available algorithms (kernelversion >= 2.6.16)\n"
			"       bic\n"
			"       cubic\n"
			"       highspeed\n"
			"       htcp\n"
			"       hybla\n"
			"       scalable\n"
			"       vegas\n"
			"       westwood\n"
			"       reno\n"
			"-e                       reuse port\n"
			"-6                       prefer ipv6\n"
			"-v                       display statistic information\n"
			"-V                       print version\n"
			"-h                       print this help screen\n"
			"*****************************************************\n"
			"Bugs, hints or else, feel free and mail to hagen@jauu.net\n"
			"SEE MAN-PAGE FOR FURTHER INFORMATION\n", opts.me);
}

/* Simple malloc wrapper */
static void *
alloc(size_t size) {

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	return ptr;
}

#if 0 /* shut up gcc warnings - at time no utilization for func */
static void *
salloc(int c, size_t size) {

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	memset(ptr, c, size);

	return ptr;
}

#define	zalloc(x) salloc(0, x)

#endif


/* Parse our command-line options and set some default options */
static int
parse_opts(int argc, char *argv[])
{
	int ret = 0, c, i, option_index = 0;
	static const char *options = "u:c:p:vehVtrm:6b:", *tmp;
	static const struct option long_options[] = {
		{"buffer",     1, 0, 'b'},
		{"utilize",    1, 0, 'u'},
		{"congestion", 1, 0, 'c'},
		{"reuse",      0, 0, 'e'},
		{"mode",       1, 0, 'm'},
		{"port",       1, 0, 'p'},
		{"inet6",      0, 0, '6'},
		{"transmit",   0, 0, 't'},
		{"receive",    0, 0, 'r'},
		{"verbose",    0, 0, 'v'},
		{"help",       0, 0, 'h'},
		{"version",    0, 0, 'V'},
		{0, 0, 0, 0}
	};


	/* Zero out opts struct and set programname */
	memset(&opts, 0, sizeof(struct opts));
	if (likely(((tmp = strrchr(argv[0], '/'))) != NULL)) {
		tmp++;
		opts.me = alloc(strlen(tmp) + 1);
		strcpy(opts.me, tmp);
	} else {
		opts.me = alloc(strlen(argv[0]) + 1);
		strcpy(opts.me, argv[0]);
	}

	/* Initialize some default values */
	opts.workmode    = MODE_SERVER;
	opts.io_call     = IO_SENDFILE;
	opts.port        = DEFAULT_PORT;
	opts.protocol    = IPPROTO_TCP;
	opts.socktype    = SOCK_STREAM;
	opts.family      = AF_INET;
	opts.buffer_size = DEFAULT_BUFSIZE;

	/* Do the dirty commandline parsing */
	while ((c = getopt_long(argc, argv, options,
					long_options, &option_index)) != -1) {

		switch (c) {

			case 'u': {
				for (i = 0; i <= IO_MAX; i++ ) {
					if (!strncasecmp(optarg,
								io_call_map[i].conf_string,
								max(strlen(io_call_map[i].conf_string),
									strlen(optarg))))
					{
						opts.io_call = io_call_map[i].conf_code;
						fprintf(stderr, "IO: %d\n", opts.io_call);
						break;
					}
				}
				}
				break;

			case 'm':
				if (!strncasecmp(optarg, "tcp", 3)) {
					/* nothing to change here - default values */
				} else if (!strncasecmp(optarg, "udp", 3)) {
					opts.protocol = IPPROTO_UDP;
					opts.socktype = SOCK_DGRAM;
				} else if (!strncasecmp(optarg, "dccp", 4)) {
					opts.protocol = IPPROTO_DCCP;
					opts.socktype = SOCK_DCCP;
				} else {
					fprintf(stderr, "Protocol \"%s\" not supported!\n", optarg);
					return -1;
				}
				break;

			case 'c': {
				for (i = 0; i <= CA_MAX; i++ ) {
					if (!strncasecmp(optarg,
								congestion_map[i].conf_string,
								max(strlen(congestion_map[i].conf_string),
									strlen(optarg))))
					{
						opts.congestion = congestion_map[i].conf_code;
						opts.change_congestion++;
						break;
					}
				}
				}
				break;

			case 'b':
				opts.buffer_size = strtol(optarg, (char **)NULL, 10);
				if (opts.buffer_size <= 0) {
					fprintf(stderr, "Buffer size to small (%d byte)!\n",
							opts.buffer_size);
				}
				break;

			case 'p':
				opts.port = strtol(optarg, (char **)NULL, 10);
				break;

			case 'r':
				opts.workmode = MODE_CLIENT;
				/* Client utilize per default read(2) */
				opts.io_call = IO_READ;
				break;

			case 't':
				opts.workmode = MODE_SERVER;
				break;

			case '6':
				opts.family = AF_INET6;
				break;

			case 'e':
				opts.reuse++;
				break;

			case 'v':
				opts.verbose++;
				break;

			case 'h':
				return -1;
				break;

			case 'V':
				printf("%s -- %s\n", PROGRAMNAME, VERSIONSTRING);
				exit(EXIT_OK);
				break;

			case '?':
				usage();
				return -1;
				break;

			default:
				fprintf(stderr, "?? getopt returned character code 0%o ??\n", c);
				return -1;
				break;
		}
	}

	/* Parse necessary and last argument (Host or Filename)
	** In subject to our workmode -- Server: Filename, Client: Hostname
	*/
	if (optind >= argc) { /* final argument missing */
		fprintf(stderr, "%s missing\n",
				opts.workmode == MODE_SERVER ? "Filename" : "Hostname");
		return -1;
	} else { /* host OR filename OR trash(failure) found */
		if (opts.workmode == MODE_SERVER) {
			opts.filename = alloc(strlen(argv[optind] + 1));
			strcpy(opts.filename, argv[optind]);
		} else if (opts.workmode == MODE_CLIENT) {
			opts.hostname = alloc(strlen(argv[optind] + 1));
			strcpy(opts.hostname, argv[optind]);
		} else {
			return -1;
		}

	}

	/* OK - parsing the command-line seems fine!
	** Last but not least we drive some consistency checks
	*/
	if (opts.workmode == MODE_SERVER) {
		switch (opts.io_call) { /* only sendfile(), mmap(), ... allowed */
			case IO_SENDFILE:
			case IO_MMAP:
			case IO_RW:
				break;
			default:
				opts.io_call = IO_SENDFILE;
				break;
		}
	} else { /* MODE_CLIENT */
		switch (opts.io_call) { /* read() allowed */
			case IO_READ:
				break;
			default:
				opts.io_call = IO_READ;
		}
	}

	return ret;
}

static int
get_mtu(int fd)
{
	int ret, mss, len_mss;

	len_mss = sizeof(mss);
	ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &mss, &len_mss);
	if ((ret == -1) || (mss <= 0)) {
		fprintf(stderr, "Can't determine mss for socket: %s "
				"(fall back to 1500bytes)\n", strerror(errno));
		return 1500;
	}

	return mss;
}


static int
open_file(void)
{
	int fd;

	fd = open(opts.filename, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Can't open input file: %s!\n", strerror(errno));
		exit(EXIT_FAILMISC);
	}

	return fd;
}


static inline void
xgetaddrinfo(const char *node, const char *service,
		struct addrinfo *hints, struct addrinfo **res)
{
	int ret;

	ret = getaddrinfo(node, service, hints, res);
	if (ret != 0) {
		fprintf(stderr, "Call to getaddrinfo() failed: %s!\n",
				(ret == EAI_SYSTEM) ?  strerror(errno) : gai_strerror(ret));
		exit(EXIT_FAILNET);
	}
}


static int
ss_rw(int file_fd, int connected_fd)
{
	int ret = 0, buflen;
	ssize_t cnt;
	unsigned char *buf;

	/* user option or default */
	buflen = opts.buffer_size;

	/* allocate buffer */
	buf = malloc(buflen);
	if (!buf) {
		fprintf(stderr, "Can't allocate %d bytes: %s!\n",
				buflen, strerror(errno));
		exit(EXIT_FAILMEM);
	}

	while ((cnt = read(file_fd, buf, buflen)) > 0) {
		net_stat.read_call_cnt++;
		if (write(connected_fd, buf, cnt) != cnt) {
			/* FIXME */
			break;
		}
	}

	return ret;
}


static int
ss_mmap(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc;
	struct stat stat_buf;
	void *mmap_buf;

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "Can't fstat file %s: %s\n", opts.filename,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	mmap_buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, file_fd, 0);
	if (mmap_buf == MAP_FAILED) {
		fprintf(stderr, "Can't mmap file %s: %s\n",
				opts.filename, strerror(errno));
	}

	rc = write(connected_fd, mmap_buf, stat_buf.st_size);
	if (rc != stat_buf.st_size) {
		fprintf(stderr, "Can't flush buffer within write call: %s!\n",
				strerror(errno));
	}

	ret = munmap(mmap_buf, stat_buf.st_size);
	if (ret == -1) {
		fprintf(stderr, "Can't munmap buffer: %s\n", strerror(errno));
	}

	return 0;
}


static int
ss_sendfile(int file_fd, int connected_fd)
{
	int ret = 0;
	ssize_t rc;
	struct stat stat_buf;
	off_t offset = 0;

	ret = fstat(file_fd, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "Can't fstat file %s: %s\n", opts.filename,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	rc = sendfile(connected_fd, file_fd, &offset, stat_buf.st_size);
	if (rc == -1) {
		fprintf(stderr, "Failure in sendfile routine: %s\n", strerror(errno));
		exit(EXIT_FAILNET);
	}
	if (rc != stat_buf.st_size) {
		fprintf(stderr, "Incomplete transfer from sendfile: %d of %ld bytes",
				rc, stat_buf.st_size);
	}

	return ret;
}


/* Creates our server socket and initialize
** options
*/
static int
instigate_ss(void)
{
	int ret, fd;
	char port_str[6]; /* strlen(UINT16_MAX) + 1  ;-) */
	struct addrinfo  hosthints, *hostres, *addrtmp;


	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;
	hosthints.ai_flags    = AI_PASSIVE;

	/* convert int port value to string */
	snprintf(port_str, sizeof(port_str) , "%d", opts.port);


	xgetaddrinfo(opts.hostname, port_str, &hosthints, &hostres);

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
		fprintf(stderr, "Can't create server socket: %s\n",
				strerror(errno));
		exit(EXIT_FAILNET);
	}

	/* fetch mtu for socket */
	net_stat.mtu = get_mtu(fd);


	if (opts.reuse) {
		int on = 1;
		ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		if (ret < 0) {
			fprintf(stderr, "Can't set socket option SO_REUSEADDR: %s",
					strerror(errno));
			exit(EXIT_FAILNET);
		}
	}

	/* XXX: setsockopt: IP_TOS, TCP_NODELAY, TCP_CORK */


	ret = bind(fd, hostres->ai_addr, hostres->ai_addrlen);
	if (ret < 0) {
		fprintf(stderr, "Can't bind() myself: %s\n", strerror(errno));
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
		if (ret < 0) {
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

/* Creates our client socket and initialize
** options
*/
static int
instigate_cs(void)
{
	int fd = 0;


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
static void
server_mode(void)
{
	int connected_fd, file_fd;

	if (opts.verbose)
		fprintf(stdout, " + Server Mode (Filename: %s)\n", opts.filename);

	/* check if the transmitted file is present and readable */
	file_fd = open_file();

	do {

		/* block and wait for client */
		connected_fd = instigate_ss();


		/* depend on the io_call we must handle our input
		 ** file a little bit different
		 **/
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
				fprintf(stderr, "Programmed Failure(%s:%d)!\n", __FILE__, __LINE__);
				exit(EXIT_FAILMISC);
				break;
		}

	} while (0); /* XXX: Further improvement is to allow user to use endless loops */

}


/* *** Main Client Routine ***
**
** o initialize client socket
** o allocate receive buffer
** o receive routine
** o write file, print diagnostic info and exit
*/
static void
client_mode(void)
{
	int cfd;

	if (opts.verbose)
		fprintf(stdout, " + Client Mode (Hostname: %s)\n", opts.hostname);

	cfd = instigate_cs();
}

/* TODO: s/fprintf/snprintf/ and separate output
** for different workmode  --HGN
*/
static void
print_analyse(void)
{
	fprintf(stdout, "%s statistic:\n"
			"Network Data:\n"
			"MTU: %d\n"
			"IO Operations:\n"
			"Read Calls: %d\n",
			opts.me, net_stat.mtu, net_stat.read_call_cnt);
}


int
main(int argc, char *argv[])
{
	int ret = EXIT_OK;

	if (parse_opts(argc, argv)) {
		usage();
		exit(EXIT_FAILOPT);
	}

	/* Branch to final workmode ... */
	switch (opts.workmode) {
		case MODE_SERVER:
			server_mode();
			break;
		case MODE_CLIENT:
			client_mode();
			break;
		default:
			fprintf(stderr, "Programmed Failure(%s:%d)!\n", __FILE__, __LINE__);
			exit(EXIT_FAILMISC);
	}

	if (opts.verbose)
		print_analyse();

	return ret;
}


/* vim:set ts=4 sw=4 tw=78 noet: */
