#include <stdint.h>
#include <sys/socket.h>

#ifndef TCP_MD5SIG_MAXKEYLEN

#define TCP_MD5SIG 14      /* TCP MD5 Signature (RFC2385) */
#define TCP_MD5SIG_MAXKEYLEN    80
struct tcp_md5sig {
	struct sockaddr_storage tcpm_addr;     /* address associated */
	uint16_t   __tcpm_pad1;                            /* zero */
	uint16_t   tcpm_keylen;                            /* key length */
	uint32_t   __tcpm_pad2;                            /* zero */
	uint8_t    tcpm_key[TCP_MD5SIG_MAXKEYLEN];         /* key (binary) */
};

#endif


