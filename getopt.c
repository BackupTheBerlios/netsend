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
#include <sched.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "global.h"
#include "xfuncs.h"

/* This is the overall parsing procedure:
 *
 *   parse_opts()
 *		- set some default values (e.g. program name)
 *		- parse the global (protocol independent arguments)
 *		- find the proper transport level protocol and branch
 *		  to this protocol (parse_tcp_opts, parse_tipc_opt, ...)
 *	 parse_XXX_opts()
 *		- set protocol specific default values
 *		- parse protocol specific options
 *		- branch to receive or transmit mode and parse
 *		  even more options, ... ok, ok enough parsing
 *	 dump_opts()
 *		- if the user select -d(ump) netsend will display
 *		  the user selected and default values
 *	 dump_XXX_opts()
 *		- print protocol specific options and default.
 */

extern struct opts opts;
extern struct conf_map_t congestion_map[];
extern struct conf_map_t memadvice_map[];
extern struct conf_map_t io_call_map[];
extern struct socket_options socket_options[];

/* The following array contains the whole cli usage screen.
 * E.g.: help_str[HELP_STR_GLOBAL] points out to the global
 * screen, where help_str[HELP_STR_TCP] only points out
 * to the tcp specific command line usage. There are some helper
 * function which wraps the access functionality to this array.
 * See print_usage() and print_complete_usage() - try to access
 * the array via these functions!
 */
static const char const help_str[][4096] = {
#define	HELP_STR_GLOBAL 0
    "Usage: netsend [OPTIONS] PROTOCOL MODE { COMMAND | HELP }\n"
	" OPTIONS      := { -T FORMAT | -6 | -4 | -n | -d | -r RTTPROBE | -P SCHED-POLICY | -N level\n"
	"                   -m MEM-ADVISORY | -V[version] | -v[erbose] LEVEL | -h[elp] | -a[ll-options] }\n"
	" PROTOCOL     := { tcp | udp | dccp | tipc | sctp | udplite }\n"
	" MODE         := { receive | transmit }\n"
	" FORMAT       := { human | machine }\n"
	" RTTPROBE     := { 10n,10d,10m,10f }\n"
	" MEM-ADVISORY := { normal | sequential | random | willneed | dontneed | noreuse }\n"
	" SCHED-POLICY := { sched_rr | sched_fifo | sched_batch | sched_other } priority\n"
	" LEVEL        := { quitscent | gentle | loudish | stressful }",
#define	HELP_STR_TCP 1
	" TCP-OPTIONS  := { -c CC-ALGORITHM } \n"
	" CC-ALGORITHM := { bic | cubic | highspeed | htcp | hybla | scalable | vegas | westwood | reno }",
#define	HELP_STR_UDP 2
	" UDP-OPTIONS  := [ FIXME ]",
#define	HELP_STR_UDPLITE 3
	" UDPL-OPTIONS := [ FIXME ]",
#define	HELP_STR_SCTP 4
	" SCTP-OPTIONS := [ FIXME ]",
#define	HELP_STR_DCCP 5
	" DCCP-OPTIONS := [ FIXME ]",
#define	HELP_STR_TIPC 6
	" TIPC-OPTIONS := { -t TIPCSOCKTYP }\n"
	" TIPCSOCKTYP  := { sock_rdm | sock_dgram | sock_stream | sock_seqpacket }",

#define	HELP_STR_MAX HELP_STR_TIPC

	/* since here additional help strings are located -
	 * Normally these are additional, smart or quick help strings
	 * for the user.
	 */

#define	HELP_STR_VERBOSE_LEVEL 7
	"LEVEL := { quitscent | gentle | loudish | stressful }",
#define	HELP_STR_SCHED_POLICY 8
	"SCHED-POLICY := { fifo | rr | batch | other }",
#define	HELP_STR_MEM_ADVICE 9
	"MEM-ADVISORY := { normal | sequential | random | willneed | dontneed | noreuse }"
};


/* print_usage is our quick failure routine for cli parsing.
 * You can specify an additional prefix, the mode for what the
 * the function should print the usage screen {all or protocol
 * specific} and last but not least if this function should be
 * the exit function
 */
static void print_usage(const char const *prefix_str,
		unsigned int mode, int should_exit)
{
	if (prefix_str != NULL)
		err_msg("%s%s\n", prefix_str, help_str[mode]);

	else
		err_msg("%s\n", help_str[mode]);

	if (should_exit)
		exit(EXIT_FAILOPT);
}

/* parse_tcp_opt set all tcp default values
 * within optsp and parse all tcp related options
 * ac is the number of arguments from MODE and av is
 * the correspond pointer into the array vector. We parse all tcp
 * related options first and within the switch/case statement we handle
 * transmit | receive specific options.
 */
static int parse_tcp_opt(int ac, char *av[], struct opts *optsp)
{
	int i;

	/* tcp has some default values too, set them here */
	optsp->perform_rtt_probe = 1;
	optsp->protocol = IPPROTO_TCP;
	optsp->socktype = SOCK_STREAM;

	/* this do/while loop parse options in the form '-x'.
	 * After the do/while loop the parse fork into transmit,
	 * receive specific code.
	 */
	do {

#define	FIRST_ARG_INDEX 0

		/* break if we reach the end of the OPTIONS */
		if (!av[FIRST_ARG_INDEX] || av[FIRST_ARG_INDEX][0] != '-')
			break;

		if (!av[FIRST_ARG_INDEX][1] || !isalnum(av[FIRST_ARG_INDEX][1]))
			print_usage(NULL, HELP_STR_TCP, 1);

		/* -c congestion control algorithm */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "c")) ) {

			if (!av[2])
				print_usage(NULL, HELP_STR_TCP, 1);

			for (i = 0; i <= CA_MAX; i++ ) {
				if (!strncasecmp(&av[FIRST_ARG_INDEX + 1][0],
							congestion_map[i].conf_string,
							max(strlen(congestion_map[i].conf_string),
								strlen(&av[FIRST_ARG_INDEX + 1][0]))))
				{
					optsp->congestion = congestion_map[i].conf_code;
					optsp->change_congestion++;
				}
			}

			if (!optsp->change_congestion) /* option error */
				print_usage(NULL, HELP_STR_TCP, 1);

			av += 2; ac -= 2;
			continue;
		}

	} while (1);
#undef FIRST_ARG_INDEX

	/* Now parse all transmit | receive specific code, plus the most
	 * important options: the file- and hostname
	 */
	switch (optsp->workmode) {
		case MODE_TRANSMIT:
			/* sanity check first */
			if (ac <= 1)
				print_usage("tcp transmit mode required file and destination address\n",
						HELP_STR_TCP, 1);

			optsp->infile = xstrdup(av[0]);
			optsp->hostname = xstrdup(av[1]);

			break;
		case MODE_RECEIVE:
			switch (ac) {
				case 0: /* nothing to do */
					break;

				case 2:
					opts.hostname = xstrdup(av[1]);
					/* fallthrough */
				case 1:
					opts.outfile = xstrdup(av[0]);
					break;
				default:
					err_msg("You specify to many arguments!");
					print_usage(NULL, HELP_STR_GLOBAL, 1);
					break;
			};

			break;
		default:
			err_msg_die(EXIT_FAILINT, "Internal, programmed error - unknown tranmit mode: %d\n", optsp->workmode);
	}

	return SUCCESS;
}

static void dump_tcp_opt(struct opts *optsp)
{

	switch (optsp->workmode) {
		case MODE_TRANSMIT:
			fprintf(stdout, "# workmode: transmit\n");
			fprintf(stdout, "# destination host: %s\n", optsp->hostname);
			fprintf(stdout, "# filename: %s\n", optsp->infile);
			break;
		case MODE_RECEIVE:
			fprintf(stdout, "# workmode: receive\n");
			break;
		default:
			err_msg_die(EXIT_FAILMISC, "Programmed Failure");
			break;
	};

	/* print congestion control options */
	fprintf(stdout, "# congestion control: %s\n", optsp->change_congestion ?
			tcp_ca_code2str(optsp->congestion) : "default");

	if (optsp->perform_rtt_probe) {
		fprintf(stdout, "# perform rtt probe: true\n");
	} else {
		fprintf(stdout, "# perform rtt probe: false\n");
	}


}


#ifdef HAVE_AF_TIPC
#include <linux/tipc.h>

static const struct {
	int  socktype;
	const char *sockname;
} socktype_map[] =
{
	{ SOCK_RDM, "SOCK_RDM" },
	{ SOCK_DGRAM, "SOCK_DGRAM" },
	{ SOCK_STREAM, "SOCK_STREAM" },
	{ SOCK_SEQPACKET, "SOCK_SEQPACKET" }
};


static void tipc_print_socktypes(void)
{
	unsigned i;
	for (i=0; i < ARRAY_SIZE(socktype_map); i++)
		fprintf(stderr, "%s\n", socktype_map[i].sockname);
}
#endif /* HAVE_AF_TIPC */


static int parse_tipc_opt(int ac, char *av[],struct opts *optsp)
{
#ifdef HAVE_AF_TIPC
	unsigned i;

	optsp->family = AF_TIPC;
	optsp->protocol = 0;

	while (ac--) {
		for (i=0; i < ARRAY_SIZE(socktype_map); i++) {
			if (strcasecmp(socktype_map[i].sockname, av[ac]) == 0)
				break;
		}
		if (i < ARRAY_SIZE(socktype_map)) {
			optsp->socktype = socktype_map[i].socktype;
		}
	}
	if (optsp->protocol)
		return SUCCESS;

	fputs("You must specify a TIPC socket type. Known values:\n", stderr);
	tipc_print_socktypes();
	exit(EXIT_FAILOPT);
#endif
	return FAILURE;
}

static void dump_tipc_opt(struct opts *optsp)
{
}

static int parse_sctp_opt(int ac, char *av[],struct opts *optsp)
{
	return SUCCESS;
}

static void dump_sctp_opt(struct opts *optsp)
{
}


static int parse_dccp_opt(int ac, char *av[],struct opts *optsp)
{
	return SUCCESS;
}

static void dump_dccp_opt(struct opts *optsp)
{
}


static int parse_udplite_opt(int ac, char *av[],struct opts *optsp)
{
	return SUCCESS;
}

static void dump_udplite_opt(struct opts *optsp)
{
}


static int parse_udp_opt(int ac, char *av[],struct opts *optsp)
{
	return SUCCESS;
}

static void dump_udp_opt(struct opts *optsp)
{
}



struct __protocol_map_t {
	int protocol;
	const char *protoname;
	const char *helptext;
	int (*parse_proto)(int, char *[], struct opts *);
	void (*dump_proto)(struct opts *);
} protocol_map[] = {
	{ 0, "tipc", help_str[HELP_STR_TIPC] , parse_tipc_opt, dump_tipc_opt },
	{ 0, "sctp", help_str[HELP_STR_SCTP] , parse_sctp_opt, dump_sctp_opt },
	{ 0, "dccp", help_str[HELP_STR_DCCP] , parse_dccp_opt, dump_dccp_opt },
	{ 0, "udplite", help_str[HELP_STR_UDPLITE], parse_udplite_opt, dump_udplite_opt },
	{ 0, "udp", help_str[HELP_STR_UDP] , parse_udp_opt, dump_udp_opt },
	{ 0, "tcp", help_str[HELP_STR_TCP] , parse_tcp_opt, dump_tcp_opt },
	{ 0, NULL, NULL, NULL, NULL },
};

#ifndef SCHED_BATCH /* since 2.6.16 */
# define SCHED_BATCH 3
#endif

struct sched_policymap_t
{
	const int  no;
	const char *name;
} sched_policymap[] =
{
	{ SCHED_OTHER, "SCHED_OTHER" },
	{ SCHED_FIFO,  "SCHED_FIFO"  },
	{ SCHED_RR,    "SCHED_RR"    },
	{ SCHED_BATCH, "SCHED_BATCH" },
	{ 0, NULL },
};

struct __short_opts_t {
	const char *name;
	unsigned long bitmask;
	unsigned int min_cmp_length;
} short_opt[] = {
#define	SOPTS_VERSION (1 << 1)
	{ "Version", SOPTS_VERSION, 1 },
#define SOPTS_NUMERIC (1 << 2)
	{ "n", SOPTS_NUMERIC, 1 },
#define	SOPTS_IPV4    (1 << 3)
	{ "4", SOPTS_IPV4, 1 },
#define	SOPTS_IPV6    (1 << 4)
	{ "6", SOPTS_IPV6, 1 },
	{ NULL, ' ', 0 },
};
static void print_complete_usage(void)
{
	int i;

	for (i = 0; i <= HELP_STR_MAX; i++)
		fprintf(stderr, "%s\n", help_str[i]);
}

static int parse_rtt_string(const char *rtt_cmd, struct opts *optsp)
{
	const char *tok = rtt_cmd;
	char *what;

	while (tok) {
		long value = strtol(tok, &what, 10);
		switch (*what) {
		case 'n':
			opts.rtt_probe_opt.iterations = value;
			if (value <= 0 || value > 100) {
				fprintf(stderr, "You want %ld rtt probe iterations - that's not sensible! "
						"Valid range is between 1 and 100 probe iterations\n", value);
				return FAILURE;
			}
			break;
		case 'd':
			opts.rtt_probe_opt.data_size = value;
			if (value <= 0) {
				fprintf(stderr, "%ld not a valid data size for our rtt probe\n", value);
				return FAILURE;
			}
			break;
		case 'm':
			opts.rtt_probe_opt.deviation_filter = value;
			if (value < 0 || value > 50) {
				fprintf(stderr, "%ldms are nonsensical for the filter multiplier (default is %d)\n", value, DEFAULT_RTT_FILTER);
				return FAILURE;
			}
			break;
		case 'f':
			opts.rtt_probe_opt.force_ms = value;
			if (value < 0) {
				fprintf(stderr, "%ldms are nonsensical for a rtt\n", value);
				return FAILURE;
			}
			break;
		default:
			fprintf(stderr, "short rtt option %s in %s not supported: %c not recognized\n", tok, rtt_cmd, *what);
			return FAILURE;
		}
		if (*what) what++;
		if (*what && *what != ',') {
			fprintf(stderr, "rtt options must be comma separated, got %c: %s\n", *what, tok);
			return FAILURE;
		}
		tok = strchr(tok, ',');
		if (tok) tok++;
	}
	return SUCCESS;
}

static void dump_opts(struct opts *optsp)
{

}




/* parse_opts will parse all command line
 * arguments, fill struct opts with default and
 * user selected values and return if no error
 * occurred. In the case of an error parse_opts
 * will exit with exit value EXIT_FAILOPTS
 */
int
parse_opts(int ac, char *av[], struct opts *optsp)
{
	char *tmp;
	int ret, i, dump_defaults = 0;

	/* Zero out opts struct and set program name */
	memset(&opts, 0, sizeof(struct opts));
	if ((tmp = strrchr(av[0], '/')) != NULL) {
		tmp++;
		opts.me = xstrdup(tmp);
	} else {
		opts.me = xstrdup(av[0]);
	}

	/* first of all: set default values in optsp */
	optsp->port = xstrdup(DEFAULT_PORT);
	optsp->buffer_size = 0; /* 0 means that a _autodetection_ takes place */
	optsp->workmode = MODE_NONE;
	optsp->stat_unit = BYTE_UNIT;
	optsp->stat_prefix = STAT_PREFIX_BINARY;
	optsp->family = AF_UNSPEC;


	/* if opts.nice is equal INT_MAX nobody change something - hopefully */
	opts.nice = INT_MAX;


	/* Catch a special case:
	 * 1) the user want all options -a.*
	 * 2) the user want help -h.*
	 */
	if ((ac == 2) && ((av[1][0] == '-') && (&av[1][1] != NULL))) {
		if (av[1][1] == 'a') {
			print_complete_usage(); exit(0);
		} else {
			print_usage(NULL, HELP_STR_GLOBAL, 1);
		}
	}

	/* We need at least 2 arguments: transport protocol
	 * and transport mode (transmit | receive)
	 */
	if (ac < 3)
		print_usage(NULL, HELP_STR_GLOBAL, 1);

	/* Pre-control OPTIONS - if any
	 * The loop differentiate between short OPTIONS like -6
	 * and OPTIONS with an argument like -T { human | machine }.
	 * The first part of the do/while loop check former and the
	 * second half the later ones.
	 */
	do {

#define	FIRST_ARG_INDEX 1

		/* break if we reach the end of the OPTIONS */
		if (!av[FIRST_ARG_INDEX] || av[FIRST_ARG_INDEX][0] != '-')
			break;

		if (!av[FIRST_ARG_INDEX][1] || !isalnum(av[FIRST_ARG_INDEX][1]))
			print_usage(NULL, HELP_STR_GLOBAL, 1);

		/* iterate over _short options_ and match relevant short options */
		for (i = 0; short_opt[i].name; i++) {
			if (!strncmp(&av[FIRST_ARG_INDEX][1], short_opt[i].name, short_opt[i].min_cmp_length)) {
				fprintf(stderr, "Found short option %s\n", short_opt[i].name);
				optsp->short_opts_mask |= short_opt[i].bitmask;
				break;
			}
		}


		 /* look for special options -all-options */
		if (!strncmp(&av[FIRST_ARG_INDEX][1], "all-options", strlen(&av[FIRST_ARG_INDEX][1])) ) {
			print_complete_usage();
			exit(1);
		} else if (!strncmp(&av[FIRST_ARG_INDEX][1], "help", strlen(&av[FIRST_ARG_INDEX][1])) ) {
			print_usage(NULL, HELP_STR_GLOBAL, 0);
			exit(0);
		}

		/* look for "-d" - this means that we should not fire netsend,
		 * instead we parse all options, exit if something isn't proper
		 * and print all declared user options and all default values.
		 * This is an nice option for automated test - but only qualified
		 * as a additional command. For an machine parseable format the -t
		 * machine is the preferred option
		 */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "d")))
			++dump_defaults;

		 /* -T { human | machine } */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "T"))) {
			if (!av[2]) {
				print_usage(NULL, HELP_STR_GLOBAL, 1);
			}
			if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "human")) {
				fprintf(stderr, "HUMAN\n");
				av += 2; ac -= 2;
				continue;
			} else if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "machine")) {
				fprintf(stderr, "MACHINE\n");
				av += 2; ac -= 2;
				continue;
			} else {
				print_usage(NULL, HELP_STR_GLOBAL, 1);
				exit(1);
			}
		}

		/* -m memory advice */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "m")) ) {

			if (!av[FIRST_ARG_INDEX + 1])
				print_usage(NULL, HELP_STR_GLOBAL, 1);

			for (i = 0; i <= MEMADV_MAX; i++ ) {
				if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], memadvice_map[i].conf_string)) {
					opts.mem_advice = memadvice_map[i].conf_code;
					opts.change_mem_advise++;
				}
			}

			if (!optsp->change_mem_advise) /* option error */
				print_usage(NULL, HELP_STR_MEM_ADVICE, 1);

			av += 2; ac -= 2;
			continue;
		}

		/* -N nice-level */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "N")) ) {
			char *endptr;

			if (!av[FIRST_ARG_INDEX + 1]) {
				print_usage(NULL, HELP_STR_GLOBAL, 1);
			}

			if (parse_rtt_string(av[FIRST_ARG_INDEX + 1], optsp) != SUCCESS)
				print_usage(NULL, HELP_STR_GLOBAL, 1);

			av += 2; ac -= 2;
			continue;
		}

		/* -N nice-level */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "N")) ) {
			char *endptr;

			if (!av[FIRST_ARG_INDEX + 1]) {
				print_usage(NULL, HELP_STR_GLOBAL, 1);
			}

			optsp->nice = strtol(&av[FIRST_ARG_INDEX + 1][0], &endptr, 10);

			if ((errno == ERANGE &&
				(optsp->nice == LONG_MAX || optsp->nice == LONG_MIN)) ||
				(errno != 0 && optsp->nice == 0)) {
			}

			if (endptr == &av[FIRST_ARG_INDEX + 1][0]) {
				//FIXME err_msg("No digits were found\n");
				exit(EXIT_FAILOPT);
			}

			av += 2; ac -= 2;
			continue;
		}

		/* -v { quitscent | gentle | loudish | stressful } */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "v")) ) {
			if (!av[FIRST_ARG_INDEX + 1]) {
				print_usage(NULL, HELP_STR_GLOBAL, 1);
			}
			if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "quitscent")) {
				optsp->verbose = 0;
				av += 2; ac -= 2;
				continue;
			} else if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "gentle")) {
				optsp->verbose = 1;
				av += 2; ac -= 2;
				continue;
			} else if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "loudish")) {
				optsp->verbose = 2;
				av += 2; ac -= 2;
				continue;
			} else if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "stressful")) {
				optsp->verbose = 3;
				av += 2; ac -= 2;
				continue;
			} else {
				print_usage("ERROR" , HELP_STR_VERBOSE_LEVEL, 0);
				print_usage(NULL, HELP_STR_GLOBAL, 1);
				exit(1);
			}
		}

		/* scheduler policy: -P { RR | FIFO | BATCH | OTHER } priority */
		if ((!strcmp(&av[FIRST_ARG_INDEX + 1][1], "P")) ) {
			if (!av[FIRST_ARG_INDEX + 1] && !av[FIRST_ARG_INDEX + 2]) {
				print_usage(NULL, HELP_STR_SCHED_POLICY, 1);
			}
			for (i = 0; sched_policymap[i].name; i++) {
				if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], sched_policymap[i].name)) {
					optsp->sched_policy = sched_policymap[i].no;
					optsp->sched_user++;
				}
			}

			if (!optsp->sched_user) /* option error */
				print_usage(NULL, HELP_STR_SCHED_POLICY, 1);

			/* OK - the policy seems fine. Now look for a valid
			 * nice level
			 */


			if (!strcasecmp(&av[FIRST_ARG_INDEX + 2][0], "MAX")) {
				optsp->priority = sched_get_priority_max(opts.sched_policy);
			} else if (!strcasecmp(&av[FIRST_ARG_INDEX + 2][0], "MIN")) {
				optsp->priority = sched_get_priority_min(opts.sched_policy);
			} else {
				optsp->priority = strtol(&av[FIRST_ARG_INDEX + 2][0], (char **)NULL, 10);
				if (!(optsp->priority <= sched_get_priority_max(opts.sched_policy) &&
					  optsp->priority >= sched_get_priority_min(opts.sched_policy))) {
					fprintf(stderr, "Priority not valid!\nValid values are in the range between"
							" %d and %d for %s (you selected %d)\n",
							sched_get_priority_min(opts.sched_policy),
							sched_get_priority_max(opts.sched_policy),
							&av[FIRST_ARG_INDEX + 1][0], optsp->priority);
					print_usage(NULL, HELP_STR_SCHED_POLICY, 1);
				}
			}

			/* FINE - all options are valid until now ... */
			av += 3; ac -= 3;
			continue;
		}

		av++;
		ac--;

	} while (1);

	/* we need at least two arguments:
	 * PROTOCOL and MODE
	 */
	if (ac < 3)
		print_usage(NULL, HELP_STR_GLOBAL, 1);

	/* now we branch to our final, protocol specific parse routine */
	for (i = 0; protocol_map[i].protoname; i++) {
		if (!strcasecmp(protocol_map[i].protoname, av[FIRST_ARG_INDEX])) {
			if (!strncasecmp(av[FIRST_ARG_INDEX + 1], "transmit", strlen(av[2]))) {
				optsp->workmode = MODE_TRANSMIT;
				ret = protocol_map[i].parse_proto(ac - 3, av + 3, optsp);
				if (dump_defaults) {
					dump_opts(optsp);
					protocol_map[i].dump_proto(optsp);
				}
				return ret;
			} else if (!strncasecmp(av[FIRST_ARG_INDEX + 1], "receive", strlen(av[FIRST_ARG_INDEX + 1]))) {
				optsp->workmode = MODE_RECEIVE;
				ret = protocol_map[i].parse_proto(ac - 3, av + 3, optsp);
				if (dump_defaults) {
					dump_opts(optsp);
					protocol_map[i].dump_proto(optsp);
				}
				return ret;
			} else {
				print_usage("MODE isn't permitted:\n", HELP_STR_GLOBAL, 1);
			}
		}
	}

	print_usage("PROTOCOL isn't permitted!\n", HELP_STR_GLOBAL, 1);

	exit(EXIT_FAILOPT);

	return 0;
}

#undef FIRST_ARG_INDEX



/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
