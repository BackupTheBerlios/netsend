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

/* TODO:
**
** o A proper Largefile support
** o Broadcast/Multicast support to find local server without knowing
**   the server address
** o Option to autotune recv buffer (like in web100 ftp suite)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <netdb.h>
#include <ctype.h>

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/time.h>

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
#define	EXIT_FAILINT  5 /* INTernal error */

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

enum sockopt_val_types {
	SVT_BOOL = 0,
	SVT_INT,
	SVT_ON
};

struct {
	const char *sockopt_name;
	int   level;
	int   option;
	int   sockopt_type;
	int   user_issue;
	int   value;
} socket_options[] = {
  {"SO_KEEPALIVE", SOL_SOCKET,  SO_KEEPALIVE, SVT_BOOL, 0, 0},
#define	CNT_SO_KEEPALIVE 0
  {"SO_REUSEADDR", SOL_SOCKET,  SO_REUSEADDR, SVT_BOOL, 0, 0},
#define	CNT_SO_REUSEADDR 1
  {"SO_BROADCAST", SOL_SOCKET,  SO_BROADCAST, SVT_BOOL, 0, 0},
#define	CNT_SO_BROADCAST 2
  {"TCP_NODELAY",  IPPROTO_TCP, TCP_NODELAY,  SVT_BOOL, 0, 0},
#define	CNT_TCP_NODELAY  3
  {"SO_SNDBUF",    SOL_SOCKET,  SO_SNDBUF,    SVT_INT,  0, 0},
#define	CNT_SO_SNDBUF    4
  {"SO_RCVBUF",    SOL_SOCKET,  SO_RCVBUF,    SVT_INT,  0, 0},
#define	CNT_SO_RCVBUF    5
  {"SO_SNDLOWAT",  SOL_SOCKET,  SO_SNDLOWAT,  SVT_INT,  0, 0},
#define	CNT_SO_SNDLOWAT  6
  {"SO_RCVLOWAT",  SOL_SOCKET,  SO_RCVLOWAT,  SVT_INT,  0, 0},
#define	CNT_SO_RCVLOWAT  7
  {"SO_SNDTIMEO",  SOL_SOCKET,  SO_SNDTIMEO,  SVT_INT,  0, 0},
#define	CNT_SO_SNDTIMEO  8
  {"SO_RCVTIMEO",  SOL_SOCKET,  SO_RCVTIMEO,  SVT_INT,  0, 0},
#define	CNTSO_RCVTIMEO   9
  {NULL, 0, 0, 0, 0, 0}
};


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
	IO_NMAP = 0,
	IO_SENDFILE,
	IO_RW,
	IO_READ,
};
#define	IO_MAX IO_READ

struct conf_map_t io_call_map[] = {
	{ IO_NMAP,		"nmap"		},
	{ IO_SENDFILE,	"sendfile"  },
	{ IO_RW,		"rw"		},
	{ IO_READ,		"read"		},
};


/* Centralize our statistic data */

struct net_stat {
	int mss;
	int keep_alive;
	int read_call_cnt;
};

struct net_stat net_stat;


/* Command-line options */

enum workmode { MODE_SERVER = 0, MODE_CLIENT };

struct opts {
	int            family;
	int            socktype;
	int            reuse;
	int            nodelay;
	int            change_congestion;
	int            congestion;
	int            verbose;
	uint16_t       port;
	char           *me;
	char           *hostname;
	char           *infile;
	char		   *outfile;
	char           *execstring;
	enum workmode  workmode;
	enum io_call   io_call;
	int            protocol;
	int            buffer_size;
	struct timeval starttime;
	struct timeval endtime;
};

struct opts opts;

static void
usage(void)
{
	fprintf(stderr,
			"USAGE: %s [options] ( -t infile  |  -r hostname )\n\n"
			"-m <tcp | udp | dccp>    specify default transfer protocol (default: tcp)\n"
			"-o <outfile>             save file to outfile (standard: STDOUT)\n"
			"-p <port>                set portnumber (default: 6666)\n"
			"-u <sendfile | mmap | rw | read >\n"
			"                         utilize specific function-call for IO operation\n"
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
			"-D                       no delay socket option (disable Nagle Algorithm)\n"
			"-e                       reuse port\n"
			"-6                       prefer ipv6\n"
			"-E <command>             execute command in server-mode and bind STDIN\n"
			"                         and STDOUT to program\n"
			"-v                       display statistic information\n"
			"-V                       print version\n"
			"-h                       print this help screen\n"
			"*****************************************************\n"
			"Bugs, hints or else, feel free and mail to hagen@jauu.net\n"
			"SEE MAN-PAGE FOR FURTHER INFORMATION\n", opts.me);
}

/* Simple malloc wrapper - prevent error checking */
static void *
alloc(size_t size) {

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	return ptr;
}

static void *
salloc(int c, size_t size){

	void *ptr;

	if ( !(ptr = malloc(size))) {
		fprintf(stderr, "Out of mem: %s!\n", strerror(errno));
		exit(EXIT_FAILMEM);
	}
	memset(ptr, c, size);

	return ptr;
}

#define	zalloc(x) salloc(0, x)


/* parse_short_opt is a helper function who
** parse the particular options
*/
static int
parse_short_opt(char **opt_str, int *argc, char **argv[])
{
	int i;

	switch((*opt_str)[1]) {
		case 'r':
			opts.workmode = MODE_CLIENT;
			break;
		case 't':
			opts.workmode = MODE_SERVER;
			break;
		case '6':
			opts.family = PF_INET6;
			break;
		case '4':
			opts.family = PF_INET;
			break;
		case 'v':
			opts.verbose++;
			break;
		case 'm':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			if (!strcasecmp((*argv)[2], "tcp")) {
				opts.protocol    = IPPROTO_TCP;
				opts.socktype    = SOCK_STREAM;
			} else if (!strcasecmp((*argv)[2], "udp")) {
				opts.protocol    = IPPROTO_UDP;
				opts.socktype    = SOCK_DGRAM;
			} else if (!strcasecmp((*argv)[2], "dccp")) {
				opts.protocol    = IPPROTO_DCCP;
				opts.socktype    = SOCK_DCCP;
				fprintf(stderr, "DCCP not supported yet ... exiting\n");
				exit(EXIT_FAILINT);
			} else {
				fprintf(stderr, "Unsupported protocol: %s\n",
						(*argv)[2]);
				exit(EXIT_FAILOPT);
			}
			(*argc)--;
			(*argv)++;
			break;
		case 'c':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			for (i = 0; i <= CA_MAX; i++ ) {
				if (!strncasecmp((*argv)[2],
							congestion_map[i].conf_string,
							max(strlen(congestion_map[i].conf_string),
								strlen((*argv)[2]))))
				{
					opts.congestion = congestion_map[i].conf_code;
					opts.change_congestion++;
					(*argc)--;
					(*argv)++;
					return 0;
				}
			}
			fprintf(stderr, "ERROR: Congestion algorithm %s not supported!\n",
					(*argv)[2]);
			exit(EXIT_FAILOPT);
			break;
		case 'p':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			opts.port = strtol((*argv)[1], (char **)NULL, 10);
			(*argc)--;
			(*argv)++;
			break;
		case 'D':
			socket_options[CNT_TCP_NODELAY].value = 1;
			socket_options[CNT_TCP_NODELAY].user_issue = 1;
			break;
		case 'e':
			socket_options[CNT_SO_REUSEADDR].value = 1;
			socket_options[CNT_SO_REUSEADDR].user_issue = 1;
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case 'V':
			printf("%s -- %s\n", PROGRAMNAME, VERSIONSTRING);
			exit(EXIT_OK);
			break;
		case 'E':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			opts.execstring = alloc(strlen((*argv)[1]) + 1);
			strcpy(opts.outfile, (*argv)[1]);
			(*argc)--;
			(*argv)++;
			break;
		case 'b':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			opts.buffer_size = strtol((*argv)[1], (char **)NULL, 10);
			if (opts.buffer_size <= 0) {
				fprintf(stderr, "Buffer size to small (%d byte)!\n",
						opts.buffer_size);
			}
			(*argc)--;
			(*argv)++;
			break;
		case 'u':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			for (i = 0; i <= IO_MAX; i++ ) {
				if (!strncasecmp((*argv)[2],
							io_call_map[i].conf_string,
							max(strlen(io_call_map[i].conf_string),
								strlen((*argv)[2]))))
				{
					opts.io_call = io_call_map[i].conf_code;
					(*argc)--;
					(*argv)++;
					return 0;
				}
			}
			fprintf(stderr, "ERROR: IO Function %s not supported!\n",
					(*argv)[2]);
			exit(EXIT_FAILOPT);
			break;
			/* Two exception for this socketoption switch:
			 ** o Common used options like SO_REUSEADDR or SO_KEEPALIVE
			 **   can also be selected via a single short option
			 **   (like option "-e" for SO_REUSEADDR)
			 ** o Some socket options like TCP_INFO, TCP_MAXSEG or TCP_CORK
			 **   can't be useful utilized - we don't support these options
			 **   via this interface.
			 */
		case 'o':
			if (((*opt_str)[2])  || ((*argc) <= 3)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			/* parse socket option argument */
			for (i = 0; socket_options[i].sockopt_name; i++) {
				/* found option */
				if (!strcasecmp((*argv)[2], socket_options[i].sockopt_name)) {
					switch (socket_options[i].sockopt_type) {
						case SVT_BOOL:
						case SVT_ON:
							if (!strcasecmp((*argv)[3], "on")) {
								socket_options[i].value = 1;
							} else if (!strcasecmp((*argv)[3], "1")) {
								socket_options[i].value = 1;
							} else if (!strcasecmp((*argv)[3], "off")) {
								socket_options[i].value = 0;
							} else if (!strcasecmp((*argv)[3], "0")) {
								socket_options[i].value = 0;
							} else {
								fprintf(stderr, "ERROR: socketoption %s value %s "
										" not supported!\n", (*argv)[2], (*argv)[3]);
								exit(EXIT_FAILOPT);
							}
							socket_options[i].user_issue++;
							break;
						case SVT_INT:
							/* TODO: add some input checkings here */
							socket_options[i].value =
								strtol((*argv)[2], (char **)NULL, 10);
							socket_options[i].user_issue++;
							break;
						default:
							fprintf(stderr, "ERROR: Programmed Error (%s:%d)\n",
									__FILE__, __LINE__);
							exit(EXIT_FAILMISC);
							break;
					}
					/* Fine, we are done ... */
					break;
				}
			}
			/* If we reach the end of our socket_options struct.
			** We found no matching socketoption because we didn't
			** support this particular option or the user smoke more
			** pot then the programmer - just kidding ... ;-)
			*/
			if (!socket_options[i].sockopt_name) {
				fprintf(stderr, "ERROR: socketoption %s not supported!\n",
						(*argv)[2]);
				exit(EXIT_FAILOPT);
			}
			(*argc) -= 2;
			(*argv) += 2;
			break;
		default:
			fprintf(stderr, "Short option %c not supported!\n", (*opt_str)[1]);
			exit(EXIT_FAILINT);
	}

	return 0;
}

/* Parse our command-line options and set some default options
** Honorable tests adduced that command-lines like e.g.:
** ./netsend -4 -6 -vvvo tcp_nodelay off -o SO_KEEPALIVE 1 \
**           -o SO_RCVBUF 65535  -u nmap -vp 444 -m tcp    \
**			 -c bic ./netsend.c
** are ready to run - I swear! ;-)
*/
static int
parse_opts(int argc, char *argv[])
{
	int ret = 0;
	char *opt_str, *tmp;

	/* Zero out opts struct and set program name */
	memset(&opts, 0, sizeof(struct opts));
	if ((tmp = strrchr(argv[0], '/')) != NULL) {
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
	opts.family      = PF_INET;
	opts.buffer_size = DEFAULT_BUFSIZE;

	/* outer loop runs over argv's ... */
	while (argc > 1) {

		opt_str = argv[1];

		if (opt_str[0] != '-')
			break;

		/* ... inner loop - read command line switches and
		** correspondent arguments (e.g. -s SOCKETOPTION VALUE)
		*/
		while (isalnum(opt_str[1])) {
			parse_short_opt(&opt_str, &argc, &argv);
			opt_str++;
		}

		argc--;
		argv++;
	}

	if (argc <= 1) {
		fprintf(stderr, "ERROR: %s missing!\n",
				(opts.workmode == MODE_SERVER) ? "Filename" : "Hostname");
		usage();
		exit(EXIT_FAILOPT);
	}

	/* OK - parsing the command-line seems fine!
	** Last but not least we drive some consistency checks
	*/
	if (opts.workmode == MODE_SERVER) {
		switch (opts.io_call) { /* only sendfile(), mmap(), ... allowed */
			case IO_SENDFILE:
			case IO_NMAP:
			case IO_RW:
				break;
			default:
				opts.io_call = IO_SENDFILE;
				break;
		}

		opts.infile = malloc(strlen(argv[1]) + 1);
		strcpy(opts.infile, argv[1]);


	} else { /* MODE_CLIENT */
		switch (opts.io_call) { /* read() allowed */
			case IO_READ:
				break;
			default:
				opts.io_call = IO_READ;
		}

		opts.hostname = malloc(strlen(argv[1]) + 1);
		strcpy(opts.hostname, argv[1]);
	}

	return ret;
}

/* get_sock_opts() appoint some socket specific
** values for further use ... (hopefully ;-)
** Values are determined by hand for the possibility
** to change something
** We should call this function after socket creation
** and at the and off our transmit/receive phase
**   --HGN
*/
static int
get_sock_opts(int fd, struct net_stat *ns)
{
	int ret;
	socklen_t len;

	/* NOTE:
	** ipv4/tcp.c:tcp_getsockopt() returns
	** tp->mss_cache_std;
	** if (!val && ((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN)))
	**     val = tp->rx_opt.user_mss;
	*/
	len = sizeof(ns->mss);
	ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &ns->mss, &len);
	if ((ret == -1) || (ns->mss <= 0)) {
		fprintf(stderr, "Can't determine mss for socket (mss: %d): %s "
				"(fall back to 1500 bytes)\n", ns->mss, strerror(errno));
		ns->mss = 1500;
	}

	/* TODO:
	**
	** IP:
	**
	** SO_DEBUG
	** SO_DONTROUTE
	** SO_BROADCAST
	** SO_SNDBUF
	** SO_RCVBUF
	** SO_REUSEADDR
	** SO_KEEPALIVE
	** SO_TYPE
	** SO_ERROR
	** SO_OOBINLINE
	** SO_NO_CHECK
	** SO_PRIORITY
	** SO_LINGER
	** SO_BSDCOMPAT ;-)
	** SO_TIMESTAMP
	** SO_RCVTIMEO
	** SO_SNDTIMEO
	** SO_RCVLOWAT
	** SO_SNDLOWAT
	** SO_PASSCRED
	** SO_PEERCRED
	** SO_PEERNAME
	** SO_ACCEPTCONN
	** SO_PEERSEC
	**
	** TCP:
	**
	** TCP_NODELAY
	** TCP_CORK
	** TCP_KEEPIDLE
	** TCP_KEEPINTVL
	** TCP_KEEPCNT
	** TCP_SYNCNT
	** TCP_LINGER2
	** TCP_DEFER_ACCEPT
	** TCP_WINDOW_CLAMP
	** TCP_QUICKACK
	** TCP_INFO
	*/

	return 0;
}


static int
open_input_file(void)
{
	int fd, ret;
	struct stat stat_buf;

	if (!strncmp(opts.infile, "-", 1)) {
		return STDIN_FILENO;
	}

	/* We don't want to read from a regular file
	** rather we want to execute a program and take
	** this output as our data source.
	*/
	if (opts.execstring) {

		pid_t pid;
		int pipefd[2];

		ret = pipe(pipefd);
		if (ret == -1) {
			fprintf(stderr, "ERROR: Can't create pipe: %s!\n",
					strerror(errno));
			exit(EXIT_FAILMISC);
		}

		switch (pid = fork()) {
			case -1:
				fprintf(stderr, "ERROR: fork: %s\n", strerror(errno));
				exit(EXIT_FAILMISC);
				break;
			case 0:
				close(STDOUT_FILENO);
				close(STDERR_FILENO);
				close(pipefd[0]);
				dup(pipefd[1]);
				dup(pipefd[1]);
				system(opts.execstring);
				exit(0);
				break;
			default:
				close(pipefd[1]);
				return pipefd[0];
				break;
		}

	}

	/* Thats the normal case: we open a regular file and take
	** the content as our source.
	*/
	ret = stat(opts.infile, &stat_buf);
	if (ret == -1) {
		fprintf(stderr, "ERROR: Can't fstat file %s: %s\n", opts.infile,
				strerror(errno));
		exit(EXIT_FAILMISC);
	}

	if (!(stat_buf.st_mode & S_IFREG)) {
		fprintf(stderr, "ERROR: Not an regular file %s\n", opts.infile);
		exit(EXIT_FAILOPT);
	}

	fd = open(opts.infile, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "ERROR: Can't open input file: %s!\n",
				strerror(errno));
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
		fprintf(stderr, "ERROR: Call to getaddrinfo() failed: %s!\n",
				(ret == EAI_SYSTEM) ?  strerror(errno) : gai_strerror(ret));
		exit(EXIT_FAILNET);
	}
}

/*
**
**  ** SERVER ROUTINES **
**
*/

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

	while ((cnt = read(file_fd, buf, buflen)) > 0) {
		char *bufptr;
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
		fprintf(stderr, "ERROR: Can't bind() myself: %s\n", strerror(errno));
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

/*
**
**  ** CLIENT ROUTINES **
**
*/

static ssize_t
cs_read(int file_fd, int connected_fd)
{
	int buflen;
	ssize_t rc;
	char *buf;

	/* user option or default(DEFAULT_BUFSIZE) */
	buflen = opts.buffer_size;

	/* allocate read buffer */
	buf = malloc(buflen);
	if (!buf) {
		fprintf(stderr, "ERROR: Can't allocate %d bytes: %s!\n",
				buflen, strerror(errno));
		exit(EXIT_FAILMEM);
	}

	/* main client loop */
	while ((rc = read(connected_fd, buf, buflen)) > 0) {
		write(file_fd, buf, rc); /* FIXME: to late and to drunken ... */
	}

	return rc;
}

/* Creates our client socket and initialize
** options
*/
static int
instigate_cs(void)
{
	int fd = 0, ret;
	char port_str[6]; /* strlen(UINT16_MAX) + 1  ;-) */
	struct addrinfo  hosthints, *hostres, *addrtmp;


	memset(&hosthints, 0, sizeof(struct addrinfo));

	/* probe our values */
	hosthints.ai_family   = opts.family;
	hosthints.ai_socktype = opts.socktype;
	hosthints.ai_protocol = opts.protocol;


	/* convert int port value to string */
	snprintf(port_str, sizeof(port_str) , "%d", opts.port);

	xgetaddrinfo(opts.hostname, port_str, &hosthints, &hostres);

	addrtmp = hostres;

	do {
		/* We do not want unix domain socket's */
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

	ret = connect(fd, hostres->ai_addr, hostres->ai_addrlen);
	if (ret == -1) {
		fprintf(stderr,"ERROR: Can't connect to %s: %s!\n",
				opts.hostname, strerror(errno));
		exit(EXIT_FAILNET);
	}

	return fd;
}

/* open our outfile */
static int
open_output_file(void)
{
	int fd = 0;

	if (!opts.outfile) {
		return STDOUT_FILENO;
	}

	umask(0);

	fd = open(opts.outfile, O_WRONLY | O_CREAT | O_EXCL,
			  S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd == -1) {
		fprintf(stderr, "ERROR: Can't create outputfile: %s!\n",
				strerror(errno));
		exit(EXIT_FAILOPT);
	}

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
	int file_fd, connected_fd = 0;

	if (VL_GENTLE(opts.verbose))
		fprintf(stdout, "Client Mode (Hostname: %s)\n", opts.hostname);

	file_fd = open_output_file();

	connected_fd = instigate_cs();

	/* take the transmit start time for diff */
	gettimeofday(&opts.starttime, NULL);

	cs_read(file_fd, connected_fd);

	gettimeofday(&opts.endtime, NULL);
}

/* TODO: s/fprintf/snprintf/ and separate output
** for different workmode  --HGN
*/
static void
print_analyse(void)
{
	fprintf(stdout, "Netsend Statistic:\n\n"
			"Network Data:\n"
			"MTU: %d\n"
			"IO Operations:\n"
			"Read Calls: %d\n",
			net_stat.mss, net_stat.read_call_cnt);
}


int
main(int argc, char *argv[])
{
	int ret = EXIT_OK;

	/* shut up gcc: salloc() temporally not used */
	(void) salloc(0, 0);

	if (parse_opts(argc, argv)) {
		usage();
		exit(EXIT_FAILOPT);
	}

	if (VL_GENTLE(opts.verbose))
		fputs(PROGRAMNAME " - " VERSIONSTRING "\n", stderr);


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

	if (VL_LOUDISH(opts.verbose))
		print_analyse();

	return ret;
}


/* vim:set ts=4 sw=4 tw=78 noet: */
