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

#include "debug.h"
#include "global.h"
#include "xfuncs.h"

/* This is the overall parsing procedure:
 *
 *   parse_opts()
 *		- set some default values (e.g. program name)
 *		- parse the global (protocol independent arguments)
 *		- find the proper transport level protocol and branch
 *		  to this protocol (parse_tcp_opts, parse_tipc_opt, ...)
 *		- set values gathered through parsing process (e.g. the cli
 *		  switch -6 turn AF_INET into AF_INET6
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
    "Usage: netsend [OPTIONS] PROTOCOL MODE { COMMAND | HELP } [filename] [hostname]\n"
	" OPTIONS      := { -T FORMAT | -6 | -4 | -n | -d | -r RTTPROBE | -P SCHED-POLICY | -N level\n"
	"                   -m MEM-ADVISORY | -V[version] | -v[erbose] LEVEL | -h[elp] | -a[ll-options] }\n"
	"                   -p PORT -s SETSOCKOPT_OPTNAME _OPTVAL -b READWRITE_BUFSIZE -u SEND-ROUTINE\n"
#if 0
	"                   -P <processing-threads>\n" /* not implemented */
#endif
	" PROTOCOL     := { tcp | udp | dccp | tipc | sctp | udplite }\n"
	" COMMAND      := { UDP-OPTIONS | UDPL-OPTIONS | SCTP-OPTIONS | DCCP-OPTIONS | TIPC-OPTIONS | TCP-OPTIONS }\n"
	" MODE         := { receive | transmit }\n"
	" FORMAT       := { human | machine }\n"
	" SEND-ROUTINE := { mmap | sendfile | splice | rw }\n"
	" RTTPROBE     := { 10n,10d,10m,10f }\n"
	" MEM-ADVISORY := { normal | sequential | random | willneed | dontneed | noreuse }\n"
	" SCHED-POLICY := { sched_rr | sched_fifo | sched_batch | sched_other } priority\n"
	" LEVEL        := { quitscent | gentle | loudish | stressful }",
#define	HELP_STR_TCP 1
	" CC-ALGORITHM := -s TCP_CONGESTION { bic | cubic | highspeed | htcp | hybla | illinois | scalable | vegas | westwood | reno | YeAH }\n"
	" TCP_MD5SIG := -C [ peer-IP-Address ] (receive mode only)",
#define	HELP_STR_UDP 2
	" UDP-OPTIONS  := [ FIXME ]",
#define	HELP_STR_UDPLITE 3
	" UDPL-OPTIONS := [ -C <checksum_coverage> ]",
#define	HELP_STR_SCTP 4
	" SCTP_DISABLE_FRAGMENTS ",
#define	HELP_STR_DCCP 5
	" DCCP-OPTIONS := { }",
#define	HELP_STR_TIPC 6
	" TIPC-OPTIONS := { TIPCSOCKTYPE }\n"
	" TIPCSOCKTYPE := { sock_rdm | sock_dgram | sock_stream | sock_seqpacket }",

#define	HELP_STR_MAX HELP_STR_TIPC

	/* since here additional help strings are located -
	 * Normally these are additional, smart or quick help strings
	 * for the user.
	 */

#define	HELP_STR_VERBOSE_LEVEL 7
	" LEVEL := { quitscent | gentle | loudish | stressful }",
#define	HELP_STR_SCHED_POLICY 8
	" SCHED-POLICY := { fifo | rr | batch | other }",
#define	HELP_STR_MEM_ADVICE 9
	" MEM-ADVISORY := { normal | sequential | random | willneed | dontneed | noreuse }",
#define	HELP_STR_IO_ADVICE 10
	" IO-CALL := { mmap | sendfile | splice | rw }"
};


/*
 * print_usage is our quick failure routine for cli parsing.
 * You can specify an additional prefix and the mode for what the
 * function should print the usage screen {all or protocol specific}
 */
static void print_usage(const char const *prefix_str, unsigned int mode)
{
	if (prefix_str != NULL)
		fprintf(stderr, "%s\n%s\n", prefix_str, help_str[mode]);

	else
		fprintf(stderr, "%s\n", help_str[mode]);
}


static void die_usage(const char const *prefix_str, unsigned int mode)
{
	print_usage(prefix_str, mode);
	exit(EXIT_FAILOPT);
}


static int
parse_yesno(const char *optname, const char *optval)
{
	int ret;

	if (strcmp(optval, "1") == 0)
		return 1;
	if (strcmp(optval, "0") == 0)
		return 0;
	if (strcasecmp(optval, "on") == 0)
		return 1;
	if (strcasecmp(optval, "yes") == 0)
		return 1;
	if (strcasecmp(optval, "no") == 0)
		return 0;
	if (strcasecmp(optval, "off") == 0)
		return 0;

	ret = atoi(optval) != 0;
	err_msg("%s: unrecognized optval \"%s\" (only 0/1 allowed); assuming %d",
								optname, optval, ret);
	return ret;
}

/* return number of characters parsed (ie amount of digits) */
static int scan_int(const char *str, int *val)
{
	char *endptr;
	long num;
	size_t parsed;

	num = strtol(str, &endptr, 0);

	parsed = endptr - str;
	if (parsed) {
		if (num > INT_MAX)
			err_msg("%s > INT_MAX", str);
		if (num < INT_MIN)
			err_msg("%s < INT_MIN", str);
		*val = (int) num;
	}
	return parsed;
}


static const char *setsockopt_optvaltype_tostr(enum sockopt_val_types x)
{
	switch (x) {
	case SVT_BOOL: return "[ 0 | 1 ]";
	case SVT_INT: return "number";
	case SVT_TIMEVAL: return "seconds:microseconds";
	case SVT_STR: return "string";
	}
	return "";
}


static const char *setsockopt_level_tostr(int level)
{
	switch (level) {
	case SOL_SOCKET: return "SOL_SOCKET";
	case IPPROTO_TCP: return "IPPROTO_TCP";
	case IPPROTO_SCTP: return "IPROTO_SCTP";
	case IPPROTO_UDPLITE: return "IPPROTO_UDPLITE";
	}
	return NULL;
}


static void die_print_cong_alg(void)
{
	static const char avail_cg[]="/proc/sys/net/ipv4/tcp_available_congestion_control";
	FILE *f;
	const char *data;
	char buf[4096];

	f = fopen(avail_cg, "r");
	if (!f)
		err_sys_die(EXIT_FAILMISC, "open %s", avail_cg);

	fputs("Known congestion control algorithms on this machine:\n", stderr);
	while ((data = fgets(buf, sizeof(buf), f)))
		fputs(buf, stderr);

	fclose(f);
	exit(EXIT_FAILOPT);
}


static void die_print_setsockopts(void)
{
	unsigned i;

	fputs("Known setsockopt optnames:\n", stderr);
	fputs("level\toptname\t\toptval\n", stderr);
	for (i = 0; socket_options[i].sockopt_name; i++) {
		fprintf(stderr, "%s\t%s\t\t%s\n",
			setsockopt_level_tostr(socket_options[i].level),
			socket_options[i].sockopt_name,
			setsockopt_optvaltype_tostr(socket_options[i].sockopt_type));
	}
	exit(EXIT_FAILOPT);
}


static void parse_setsockopt_name(const char *optname, const char *optval)
{
	unsigned i;

	for (i = 0; socket_options[i].sockopt_name; i++) {
		if (strcasecmp(optname, socket_options[i].sockopt_name))
			continue;

		switch (socket_options[i].sockopt_type) {
		case SVT_BOOL:
			socket_options[i].value = parse_yesno(optname, optval);
			socket_options[i].user_issue = true;
		return;
		case SVT_INT:
			if (scan_int(optval, &socket_options[i].value))
				socket_options[i].user_issue = true;
			else
				err_msg("%s: unrecognized optval \"%s\" "
					"(integer argument required);skipped",
							optval, optname);
		case SVT_TIMEVAL: {
			int seconds, usecs = 0;
			int parsed = scan_int(optval, &seconds);

			if (parsed == 0) {
				err_msg("%s: unrecognized optval \"%s\" "
					"(integer argument required);skipped",
							optval, optname);
				return;
			}
			if (optval[parsed] == ':') {
				parsed = scan_int(&optval[parsed+1], &usecs);
				if (parsed == 0) {
					err_msg("%s: unrecognized optval \"%s\" "
					"(integer argument required after ':');skipped",
							optval, optname);
					return;
				}
			}
			socket_options[i].user_issue = true;
			socket_options[i].tv.tv_sec = seconds;
			socket_options[i].tv.tv_usec = usecs;
		}
		return;
		case SVT_STR:
			socket_options[i].value_ptr = optval;
			socket_options[i].user_issue = true;
		return;
		default:
			err_msg("WARNING: Internal error: unrecognized "
				"sockopt_type (%s %s) (%s:%u)",
				optval, optname, __FILE__, __LINE__);
			return;
		}
	}
	err_msg("Unrecognized sockopt \"%s\" ignored", optname );
}


static void parse_transmit_filename(int ac, char *av[], struct opts *optsp, int helpt)
{
	if (optsp->workmode != MODE_TRANSMIT)
		return;

	if (ac <= 1)
		die_usage("transmit mode requires file and destination address", helpt);

	optsp->infile = av[0];
	optsp->hostname = av[1];
}


static void parse_receive_filename(int ac, char *av[], struct opts *optsp)
{
	if (optsp->workmode != MODE_RECEIVE)
		return;
	switch (ac) {
	case 0: return;
	case 2:
		optsp->hostname = av[1];
		/* fallthrough */
	case 1:
		optsp->outfile = av[0];
		return;
	default:
		err_msg("You specified too many arguments!");
		die_usage(NULL, HELP_STR_GLOBAL);
	};
}


/* parse_tcp_opt set all tcp default values
 * within optsp and parse all tcp related options
 * ac is the number of arguments from MODE and av is
 * the correspond pointer into the array vector. We parse all tcp
 * related options first and within the switch/case statement we handle
 * transmit | receive specific options.
 */
static void parse_tcp_opt(int ac, char *av[], struct opts *optsp)
{
	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_TCP;

	/* tcp has some default values too, set them here */
	optsp->perform_rtt_probe = 1;
	optsp->protocol = IPPROTO_TCP;
	optsp->socktype = SOCK_STREAM;

	while (av[0] && av[0][0] == '-') {
		if (av[0][1] == 'C')
			optsp->tcp_use_md5sig = true;

		if (optsp->workmode == MODE_RECEIVE) {
			/* need peer ip address */
			if (!av[1] || av[1][0] == '-')
				err_msg_die(EXIT_FAILOPT, "Option -C needs an argument (Peer IP Address)");
			optsp->tcp_md5sig_peeraddr = av[1];
			ac--;
			av++;
		}
		ac--;
		av++;
	}

	if (optsp->tcp_use_md5sig)
		msg(GENTLE, "Enabled TCP_MD5SIG option");
	/* Now parse all transmit | receive specific code, plus the most
	 * important options: the file- and hostname
	 */
	switch (optsp->workmode) {
	case MODE_TRANSMIT:
		parse_transmit_filename(ac, av, optsp, HELP_STR_GLOBAL);
		break;
	case MODE_RECEIVE:
		parse_receive_filename(ac, av, optsp);
		break;
	case MODE_NONE: assert(0);
	}
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
#endif /* HAVE_AF_TIPC */


static void parse_tipc_opt(int ac, char *av[], struct opts *optsp)
{
#ifdef HAVE_AF_TIPC
	unsigned i;

	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_TIPC;

	optsp->family = AF_TIPC;
	optsp->socktype = 0;

	switch (optsp->workmode) {
	case MODE_TRANSMIT:
		if (ac <= 1)
			die_usage("TIPC transmit mode requires socket type and input file name",
					HELP_STR_TIPC);
		--ac;
		optsp->infile = av[ac];
		break;
	case MODE_RECEIVE:
		if (ac < 1)
			goto out;
		if (ac >= 2) {
			if (av[ac - 1][0] == '-')
				break;
			if (av[ac - 2][0] == '-')
				break;
			--ac;
			optsp->outfile = av[ac];
		}
		break;
	case MODE_NONE: assert(0);
	}

	while (ac--) {
		for (i=0; i < ARRAY_SIZE(socktype_map); i++) {
			if (strcasecmp(socktype_map[i].sockname, av[ac]) == 0) {
				optsp->socktype = socktype_map[i].socktype;
				break;
			}
		}
		if (optsp->socktype)
			return;
	}
 out:
	die_usage("You must specify a TIPC socket type", HELP_STR_TIPC);
#endif
	exit(EXIT_FAILOPT);
}

static void dump_tipc_opt(struct opts *optsp __attribute__((unused)))
{
}

static void parse_sctp_opt(int ac, char *av[],struct opts *optsp)
{

	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_SCTP;

	optsp->perform_rtt_probe = 1;
	optsp->protocol = IPPROTO_SCTP;
	optsp->socktype = SOCK_STREAM;

	/* this do/while loop parse options in the form '-x'.
	 * After the do/while loop the parse fork into transmit,
	 * receive specific code.
	 */
	for (;;) {
#define	FIRST_ARG_INDEX 0
		/* break if we reach the end of the OPTIONS */
		if (!av[FIRST_ARG_INDEX] || av[FIRST_ARG_INDEX][0] != '-')
			break;

		if (!av[FIRST_ARG_INDEX][1] || !isalnum(av[FIRST_ARG_INDEX][1]))
			die_usage(NULL, HELP_STR_SCTP);
	}
#undef FIRST_ARG_INDEX

	switch (optsp->workmode) {
	case MODE_TRANSMIT:
		parse_transmit_filename(ac, av, optsp, HELP_STR_SCTP);
		break;
	case MODE_RECEIVE:
		parse_receive_filename(ac, av, optsp);
		break;
	case MODE_NONE: assert(0);
	}
}


static void dump_sctp_opt(struct opts *optsp __attribute__((unused)))
{
}


static void parse_dccp_opt(int ac, char *av[],struct opts *optsp)
{

	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_DCCP;

	optsp->protocol = IPPROTO_DCCP;
	optsp->socktype = SOCK_DCCP;

	switch (optsp->workmode) {
	case MODE_RECEIVE:
		parse_receive_filename(ac, av, optsp, HELP_STR_DCCP);
		break;
	case MODE_TRANSMIT:
		parse_transmit_filename(ac, av, optsp, HELP_STR_DCCP);
		break;
	case MODE_NONE:
		assert(0);
	}
}

static void dump_dccp_opt(struct opts *optsp __attribute__((unused)))
{
}


static void parse_udplite_opt(int ac, char *av[],struct opts *optsp)
{
	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_UDPLITE;

	optsp->protocol = IPPROTO_UDPLITE;
	optsp->socktype = SOCK_DGRAM;

	optsp->udplite_checksum_coverage = LONG_MAX;

	/* this do/while loop parse options in the form '-x'.
	 * After the do/while loop the parse fork into transmit,
	 * receive specific code.
	 */
	do {
		char *endptr;

		/* break if we reach the end of the OPTIONS or we see
		 * the special option '-' -> this indicate the special
		 * output file "stdout" (therefore no option ;-) */
		if ((!av[0] || av[0][0] != '-') ||
			  (av[0] && av[0][0] == '-' && !av[0][1]))
			break;

		if (!av[0][1] || !isalnum(av[0][1]))
			die_usage(NULL, HELP_STR_TCP);

		if (av[0][1] == 'C') {
			if (!av[1])
				die_usage("UDPLite option C requires an argument",
						HELP_STR_UDPLITE);

			optsp->udplite_checksum_coverage = strtol(av[1], &endptr, 10);

			if ((errno == ERANGE &&
				(optsp->udplite_checksum_coverage == LONG_MAX ||
				 optsp->udplite_checksum_coverage == LONG_MIN)) ||
				(errno != 0 && optsp->udplite_checksum_coverage == 0)) {
				die_usage("UDPLite option C requires an numeric argument",
						HELP_STR_UDPLITE);
			}

			msg(GENTLE, "parse UDPLite request to cover %ld bytes",
					optsp->udplite_checksum_coverage);

			ac -= 2;
			av += 2;
			continue;
		}

		ac--;
		av++;
	} while (1);


	switch (optsp->workmode) {
	case MODE_RECEIVE:
		parse_receive_filename(ac, av, optsp);
		break;
	case MODE_TRANSMIT:
		parse_transmit_filename(ac, av, optsp, HELP_STR_UDPLITE);
		break;
	case MODE_NONE: assert(0);
	}
}

static void dump_udplite_opt(struct opts *optsp __attribute__((unused)))
{
}


static void parse_udp_opt(int ac, char *av[],struct opts *optsp)
{
	/* memorize protocol */
	optsp->ns_proto = NS_PROTO_UDP;

	optsp->protocol = IPPROTO_UDP;
	optsp->socktype = SOCK_DGRAM;

	switch (optsp->workmode) {
	case MODE_RECEIVE:
		parse_receive_filename(ac, av, optsp);
		break;
	case MODE_TRANSMIT:
		parse_transmit_filename(ac, av, optsp, HELP_STR_UDP);
		break;
	case MODE_NONE: assert(0);
	}
}

static void dump_udp_opt(struct opts *optsp __attribute__((unused)))
{
}



struct __protocol_map_t {
	int protocol;
	const char *protoname;
	const char *helptext;
	void (*parse_proto)(int, char *[], struct opts *);
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
	{ "Version", SOPTS_VERSION, 1 },
	{ "n", SOPTS_NUMERIC, 1 },
	{ "4", SOPTS_IPV4, 1 },
	{ "6", SOPTS_IPV6, 1 },
	{ NULL, ' ', 0 },
};
static void print_complete_usage(void)
{
	int i;

	for (i = 0; i <= HELP_STR_MAX; i++)
		fprintf(stderr, "%s\n", help_str[i]);
}

static int parse_rtt_string(const char *rtt_cmd, struct opts *optsp __attribute__((unused)))
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
				fprintf(stderr,
						"%ldms are nonsensical for the filter multiplier (default is %d)\n",
						value, DEFAULT_RTT_FILTER);
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
			fprintf(stderr, "short rtt option %s in %s not supported: %c not recognized\n",
					tok, rtt_cmd, *what);
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

static void dump_opts(struct opts *optsp __attribute__((unused)))
{

}




/* parse_opts will parse all command line
 * arguments, fill struct opts with default and
 * user selected values and return if no error
 * occurred. In the case of an error parse_opts
 * will exit with exit value EXIT_FAILOPTS
 */
void
parse_opts(int ac, char *av[], struct opts *optsp)
{
	char *tmp;
	int i, dump_defaults = 0;

	/* Zero out opts struct and set program name */
	memset(optsp, 0, sizeof(struct opts));
	if ((tmp = strrchr(av[0], '/')) != NULL) {
		tmp++;
		optsp->me = xstrdup(tmp);
	} else {
		optsp->me = xstrdup(av[0]);
	}

	/* first of all: set default values in optsp */
	optsp->ns_proto = NS_PROTO_UNSPEC;
	optsp->port = xstrdup(DEFAULT_PORT);
	optsp->buffer_size = 0; /* 0 means that a _autodetection_ takes place */
	optsp->workmode = MODE_NONE;
	optsp->stat_unit = BYTE_UNIT;
	optsp->stat_prefix = STAT_PREFIX_BINARY;
	optsp->family = AF_UNSPEC;

	/* per default only one transmit, respective receive
	 * thread will do the whole work */
	optsp->threads = 1;

	/* if opts.nice is equal INT_MAX nobody change something - hopefully */
	optsp->nice = INT_MAX;

	/* Catch a special case:
	 * 1) the user want all options -a.*
	 * 2) the user want help -h.*
	 */
	if ((ac == 2) && ((av[1][0] == '-') && (&av[1][1] != NULL))) {
		if (av[1][1] == 'a') {
			print_complete_usage(); exit(0);
		} else {
			die_usage(NULL, HELP_STR_GLOBAL);
		}
	}

	/* We need at least 2 arguments: transport protocol
	 * and transport mode (transmit | receive)
	 */
	if (ac < 3)
		die_usage(NULL, HELP_STR_GLOBAL);

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
			die_usage(NULL, HELP_STR_GLOBAL);

		if (!strncmp(&av[FIRST_ARG_INDEX][1], "h", 1)) {
			print_usage(NULL, HELP_STR_GLOBAL);
			exit(0);
		}

		/* iterate over _short options_ and match relevant short options */
		for (i = 0; short_opt[i].name; i++) {
			if (!strncmp(&av[FIRST_ARG_INDEX][1], short_opt[i].name, short_opt[i].min_cmp_length)) {
				optsp->short_opts_mask |= short_opt[i].bitmask;
				break;
			}
		}


		 /* look for special options -all-options */
		if (!strncmp(&av[FIRST_ARG_INDEX][1], "all-options", strlen(&av[FIRST_ARG_INDEX][1])) ) {
			print_complete_usage();
			exit(1);
		} else if (!strncmp(&av[FIRST_ARG_INDEX][1], "help", strlen(&av[FIRST_ARG_INDEX][1])) ) {
			print_usage(NULL, HELP_STR_GLOBAL);
			exit(0);
		}

		/* look for "-d" - this means that we should not fire netsend,
		 * instead we parse all options, exit if something isn't proper
		 * and print all declared user options and all default values.
		 * This is an nice option for automated test - but only qualified
		 * as a additional command. For an machine parseable format the -T
		 * machine is the preferred option
		 */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "d")))
			++dump_defaults;

		 /* -T { human | machine } */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "T"))) {
			if (!av[2])
				die_usage(NULL, HELP_STR_GLOBAL);

			if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "human")) {
				optsp->statistics++;
				av += 2; ac -= 2;
				continue;
			} else if (!strcmp(&av[FIRST_ARG_INDEX + 1][0], "machine")) {
				optsp->machine_parseable++;
				av += 2; ac -= 2;
				continue;
			} else {
				die_usage(NULL, HELP_STR_GLOBAL);
			}
		}

		/* -b bufsize: -b readwritebufsize */
		if (av[FIRST_ARG_INDEX][1] == 'b') {
			if (!av[2])
				die_usage(NULL, HELP_STR_GLOBAL);

			if (!scan_int(av[2], &optsp->buffer_size))
				err_msg_die(EXIT_FAILOPT, "-b: writebuffersize must be a number");

			av += 2; ac -= 2;
			continue;
		}

		/* -m memory advice */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "m")) ) {

			if (!av[FIRST_ARG_INDEX + 1])
				die_usage(NULL, HELP_STR_GLOBAL);

			for (i = 0; i <= MEMADV_MAX; i++ ) {
				if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], memadvice_map[i].conf_string)) {
					optsp->mem_advice = memadvice_map[i].conf_code;
					optsp->change_mem_advise++;
				}
			}

			if (!optsp->change_mem_advise) /* option error */
				die_usage(NULL, HELP_STR_MEM_ADVICE);

			av += 2; ac -= 2;
			continue;
		}

		/* -u write-function */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "u")) ) {

			if (!av[FIRST_ARG_INDEX + 1])
				die_usage(NULL, HELP_STR_GLOBAL);

			for (i = 0; i <= IO_MAX; i++ ) {
				if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], io_call_map[i].conf_string)) {
					optsp->io_call = io_call_map[i].conf_code;
					break;
				}
			}

			if (i > IO_MAX) /* option error */
				die_usage(NULL, HELP_STR_IO_ADVICE);

			av += 2; ac -= 2;
			continue;
		}

		/* -r rtt probe */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "r")) ) {
			if (!av[FIRST_ARG_INDEX + 1]) {
				die_usage(NULL, HELP_STR_GLOBAL);
			}

			if (parse_rtt_string(av[FIRST_ARG_INDEX + 1], optsp) != SUCCESS)
				die_usage(NULL, HELP_STR_GLOBAL);

			av += 2; ac -= 2;
			continue;
		}

		/* -P threads */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "P")) ) {
			char *endptr;

			if (!av[FIRST_ARG_INDEX + 1]) {
				die_usage(NULL, HELP_STR_GLOBAL);
			}

			optsp->threads = strtol(&av[FIRST_ARG_INDEX + 1][0], &endptr, 10);

			/* sanity checks */
			if ((errno == ERANGE &&
				(optsp->threads == LONG_MAX || optsp->threads == LONG_MIN)) ||
				(errno != 0 && optsp->threads == 0)) {
			}

			if (endptr == &av[FIRST_ARG_INDEX + 1][0]) {
				//FIXME err_msg("No digits were found\n");
				exit(EXIT_FAILOPT);
			}

			if (optsp->threads <= 0) {
				die_usage("Number of threads must be greater then 0", HELP_STR_GLOBAL);
			}

			av += 2; ac -= 2;
			continue;
		}


		/* -N nice-level */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "N")) ) {
			char *endptr;

			if (!av[FIRST_ARG_INDEX + 1])
				die_usage(NULL, HELP_STR_GLOBAL);

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

		/* -s setsockopt options: -s OPTNAME [ OPTVAL ] */
		if (av[FIRST_ARG_INDEX][1] == 's') {
			const char *optval, *optname = av[FIRST_ARG_INDEX + 1];

			if (!optname || *optname == '-' || !*optname) {
				fputs("need optname after -s\n", stderr);
				die_print_setsockopts();
			}
			optval = av[FIRST_ARG_INDEX + 2];
			if (!optval || !*optval) {
				fputs("need optval after optname\n", stderr);
				if (strcmp(optname, "TCP_CONGESTION") == 0)
					die_print_cong_alg();
				die_print_setsockopts();
			}

			parse_setsockopt_name(optname, optval);

			av += 3; ac -= 3;
			continue;
		}
		/* -p port: set TCP/UDP/DCCP/SCTP port number to use */
		if (av[FIRST_ARG_INDEX][1] == 'p') {
			if (!av[2])
				die_usage(NULL, HELP_STR_GLOBAL);

			optsp->port = strdup(av[2]);

			av += 2; ac -= 2;
			continue;
		}

		/* -v { quitscent | gentle | loudish | stressful } */
		if ((!strcmp(&av[FIRST_ARG_INDEX][1], "v")) ) {
			if (!av[FIRST_ARG_INDEX + 1]) {
				die_usage(NULL, HELP_STR_GLOBAL);
			}
			if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], "quitscent")) {
				optsp->verbose = 0;
				av += 2; ac -= 2;
				continue;
			} else if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], "gentle")) {
				optsp->verbose = 1;
				av += 2; ac -= 2;
				continue;
			} else if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], "loudish")) {
				optsp->verbose = 2;
				av += 2; ac -= 2;
				continue;
			} else if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], "stressful")) {
				optsp->verbose = 3;
				av += 2; ac -= 2;
				continue;
			} else {
				print_usage("ERROR" , HELP_STR_VERBOSE_LEVEL);
				die_usage(NULL, HELP_STR_GLOBAL);
			}
		}

		/* scheduler policy: -P { RR | FIFO | BATCH | OTHER } priority */
		if ((!strcmp(&av[FIRST_ARG_INDEX + 1][1], "P")) ) {
			if (!av[FIRST_ARG_INDEX + 1] && !av[FIRST_ARG_INDEX + 2])
				die_usage(NULL, HELP_STR_SCHED_POLICY);

			for (i = 0; sched_policymap[i].name; i++) {
				if (!strcasecmp(&av[FIRST_ARG_INDEX + 1][0], sched_policymap[i].name)) {
					optsp->sched_policy = sched_policymap[i].no;
					optsp->sched_user++;
				}
			}

			if (!optsp->sched_user) /* option error */
				die_usage(NULL, HELP_STR_SCHED_POLICY);

			/* OK - the policy seems fine. Now look for a valid
			 * nice level
			 */
			if (!strcasecmp(&av[FIRST_ARG_INDEX + 2][0], "MAX")) {
				optsp->priority = sched_get_priority_max(optsp->sched_policy);
			} else if (!strcasecmp(&av[FIRST_ARG_INDEX + 2][0], "MIN")) {
				optsp->priority = sched_get_priority_min(optsp->sched_policy);
			} else {
				optsp->priority = strtol(&av[FIRST_ARG_INDEX + 2][0], (char **)NULL, 10);
				if (!(optsp->priority <= sched_get_priority_max(optsp->sched_policy) &&
					  optsp->priority >= sched_get_priority_min(optsp->sched_policy))) {
					fprintf(stderr, "Priority not valid!\nValid values are in the range between"
							" %d and %d for %s (you selected %d)\n",
							sched_get_priority_min(optsp->sched_policy),
							sched_get_priority_max(optsp->sched_policy),
							&av[FIRST_ARG_INDEX + 1][0], optsp->priority);
					die_usage(NULL, HELP_STR_SCHED_POLICY);
				}
			}

			/* FINE - all options are valid until now ... */
			av += 3; ac -= 3;
			continue;
		}

		av++;
		ac--;

	} while (1);

	/* set values gathered in the parsing process */

	if (optsp->short_opts_mask & SOPTS_IPV4)
		optsp->family = AF_INET;

	/* -6 had precedence */
	if (optsp->short_opts_mask & SOPTS_IPV6)
		optsp->family = AF_INET6;

	/* we need at least two arguments:
	 * PROTOCOL and MODE
	 */
	if (ac < 3)
		die_usage(NULL, HELP_STR_GLOBAL);

	/* now we branch to our final, protocol specific parse routine */
	for (i = 0; protocol_map[i].protoname; i++) {
		if (!strcasecmp(protocol_map[i].protoname, av[FIRST_ARG_INDEX])) {
			if (!strncasecmp(av[FIRST_ARG_INDEX + 1], "transmit", strlen(av[2]))) {
				optsp->workmode = MODE_TRANSMIT;
				protocol_map[i].parse_proto(ac - 3, av + 3, optsp);
				if (dump_defaults) {
					dump_opts(optsp);
					protocol_map[i].dump_proto(optsp);
				}
				return;
			} else if (!strncasecmp(av[FIRST_ARG_INDEX + 1], "receive", strlen(av[FIRST_ARG_INDEX + 1]))) {
				optsp->workmode = MODE_RECEIVE;
				protocol_map[i].parse_proto(ac - 3, av + 3, optsp);
				if (dump_defaults) {
					dump_opts(optsp);
					protocol_map[i].dump_proto(optsp);
				}
				return;
			} else {
				die_usage("MODE isn't permitted:", HELP_STR_GLOBAL);
			}
		}
	}
	die_usage("PROTOCOL isn't permitted!", HELP_STR_GLOBAL);
}

#undef FIRST_ARG_INDEX

/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
