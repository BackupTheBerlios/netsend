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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "global.h"

extern struct opts opts;
extern struct conf_map_t congestion_map[];
extern struct conf_map_t memadvice_map[];
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];

void
usage(void)
{
	fprintf(stderr,
			"USAGE: %s [options] ( -t <input-file> <hostname>  |  -r <output-file> <multicast>\n\n"
			"    -t <input-file>  <hostname>     _t_ransmit input-file to host hostname\n"
			"    -r <output-file> <multicast>    _r_eceive data and save to outputfile\n"
			"                                    bind local socket to multicast group\n"
			"                                    (output-file and multicast adddr in receive mode are optional)\n\n"
			"-m <tcp | udp | dccp>    specify default transfer protocol (default: tcp)\n"
			"-o <outfile>             save file to outfile (standard: STDOUT)\n"
			"-p <port>                set portnumber (default: 6666)\n"
			"-u <sendfile | mmap | rw | read >\n"
			"                         utilize specific function-call for IO operation\n"
			"                         (this depends on operation mode (-t, -r)\n"
			"-a <advice>              set memory advisory information\n"
			"       normal\n"
			"       sequential\n"
			"       random\n"
			"       willneed\n"
			"       dontneed\n"
			"       noreuse (equal to willneed for -u mmap)\n" /* 2.6.16 treats them the same */
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


/* parse_short_opt is a helper function who
** parse the particular options
*/
static int
parse_short_opt(char **opt_str, int *argc, char **argv[])
{
	int i;

	switch((*opt_str)[1]) {
		case 'r':
			opts.workmode = MODE_RECEIVE;
			break;
		case 't':
			opts.workmode = MODE_TRANSMIT;
			break;
		case '6':
			opts.family = AF_INET6;
			break;
		case '4':
			opts.family = AF_INET;
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
		case 'a':
			if (((*opt_str)[2])  || ((*argc) <= 2)) {
				fprintf(stderr, "option error (%c:%d)\n",
						(*opt_str)[2], (*argc));
				exit(1);
			}
			for (i = 0; i <= MEMADV_MAX; i++ ) {
				if (!strcasecmp((*argv)[2], memadvice_map[i].conf_string)) {
					opts.mem_advice = memadvice_map[i].conf_code;
					(*argc)--;
					(*argv)++;
					return 0;
				}
			}
			fprintf(stderr, "ERROR: Mem Advice %s not supported!\n", (*argv)[2]);
			exit(EXIT_FAILOPT);
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
			/* we allocate room for DEFAULT_PORT at initialize phase
			** now free it and reallocate a proper size
			*/
			free(opts.port);
			opts.port = alloc(strlen((*argv)[2]) + 1);
			sprintf(opts.port, "%s", (*argv)[2]);
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
**           -o SO_RCVBUF 65535  -u mmap -vp 444 -m tcp    \
**			 -c bic ./netsend.c
** are ready to run - I swear! ;-)
*/
int
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
	opts.workmode    = MODE_NONE;
	opts.io_call     = IO_SENDFILE;
	opts.protocol    = IPPROTO_TCP;
	opts.socktype    = SOCK_STREAM;
	opts.family      = AF_UNSPEC;
	opts.buffer_size = DEFAULT_BUFSIZE;

	opts.port = alloc(strlen(DEFAULT_PORT) + 1);
	sprintf(opts.port, "%s", DEFAULT_PORT);

	/* outer loop runs over argv's ... */
	while (argc > 1) {

		opt_str = argv[1];

		if ((opt_str[0] == '-' && !opt_str[1]) ||
		     opt_str[0] != '-') {
			break;
		}

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

	/* OK - parsing the command-line seems fine!
	** Last but not least we drive some consistency checks
	*/
	if (opts.workmode == MODE_TRANSMIT) {

		if (argc <= 2) {
			err_msg("filename and hostname missing!");
			usage();
			exit(EXIT_FAILOPT);
		}

		switch (opts.io_call) { /* only sendfile(), mmap(), ... allowed */
			case IO_SENDFILE:
			case IO_MMAP:
			case IO_RW:
				break;
			default:
				opts.io_call = IO_SENDFILE;
				break;
		}

		opts.infile = malloc(strlen(argv[1]) + 1);
		strcpy(opts.infile, argv[1]);

		opts.hostname = malloc(strlen(argv[2]) + 1);
		strcpy(opts.hostname, argv[2]);

	} else if (opts.workmode == MODE_RECEIVE) { /* MODE_RECEIVE */

		switch (--argc) {
			case 0: /* nothing to do */
				break;

			case 2:
				opts.hostname = malloc(strlen(argv[2]) + 1);
				strcpy(opts.hostname, argv[2]);
			case 1:
				opts.outfile = malloc(strlen(argv[1]) + 1);
				strcpy(opts.outfile, argv[1]);
				break;

			default:
				err_msg("You specify to many arguments!");
				usage();
				exit(EXIT_FAILINT);
				break;

		}

		switch (opts.io_call) { /* read() allowed */
			case IO_READ:
				break;
			default:
				opts.io_call = IO_READ;
		}
	} else {
		err_msg("You must specify your work mode: receive "
				"or transmit (-r | -t)!");
		usage();
		exit(EXIT_FAILOPT);
	}

	return ret;
}

/* vim:set ts=4 sw=4 tw=78 noet: */
