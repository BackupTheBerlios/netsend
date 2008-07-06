#ifndef NETSEND_PROTO_TCP_H_INCLUDE_
#define NETSEND_PROTO_TCP_H_INCLUDE_

#include <stdbool.h>

#define __USE_MISC 1 /* for struct tcp_info */
#include <netinet/tcp.h>

void tcp_trans_mode(void);

bool tcp_get_info(int fd, struct tcp_info *tcp_info);
void tcp_print_info(struct tcp_info *tcp_info);

#endif /* NETSEND_PROTO_TCP_H_INCLUDE_ */
