#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "proto_tcp.h"
#include "global.h"
#include "xfuncs.h"

bool tcp_get_info(int fd, struct tcp_info *tcp_info)
{
	socklen_t ret_size = sizeof(struct tcp_info);

	if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, tcp_info, &ret_size) < 0)
		return false;

	return sizeof(*tcp_info) == ((size_t) ret_size);
}

void tcp_print_info(struct tcp_info *tcp_info)
{
	fprintf(stderr, "\ntcp info:\n"
		 "\tretransmits:   %d\n"
		 "\tprobes:        %d\n"
		 "\tbackoff:       %d\n",
		 tcp_info->tcpi_retransmits, tcp_info->tcpi_probes,
		 tcp_info->tcpi_backoff);
	fputs("\toptions:       ", stderr);
	/* see netinet/tcp.h for definition */
	if (tcp_info->tcpi_options & TCPI_OPT_TIMESTAMPS)
		fputs("TIMESTAMPS ", stderr);
	if (tcp_info->tcpi_options & TCPI_OPT_SACK)
		fputs("SACK ", stderr);
	if (tcp_info->tcpi_options & TCPI_OPT_WSCALE)
		fputs("WSCALE ", stderr);
	if (tcp_info->tcpi_options & TCPI_OPT_ECN)
		fputs("ECN", stderr);
	fprintf(stderr, "\n"
		"\tsnd_wscale:    %d\n"
		"\trcv_wscale:    %d\n"
		"\trto:           %d\n"
		"\tato:           %d\n"
		"\tsnd_mss:       %d\n"
		"\trcv_mss:       %d\n"
		"\tunacked:       %d\n",
		tcp_info->tcpi_snd_wscale, tcp_info->tcpi_rcv_wscale,
		tcp_info->tcpi_rto, tcp_info->tcpi_ato,
		tcp_info->tcpi_snd_mss, tcp_info->tcpi_rcv_mss,
		tcp_info->tcpi_unacked);
}


void tcp_setsockopt_md5sig(int fd, const struct sockaddr *sa)
{
	static const char key[] = "netsend";
	struct tcp_md5sig sig = { .tcpm_keylen = sizeof(key) };

	memcpy(sig.tcpm_key, key, sizeof(key));

	memcpy(&sig.tcpm_addr, (const struct sockaddr_storage *) sa,
			min(sizeof(sig.tcpm_addr), sizeof(*sa)));

	xsetsockopt(fd, IPPROTO_TCP, TCP_MD5SIG, &sig, sizeof(sig), "TCP_MD5SIG");
}

