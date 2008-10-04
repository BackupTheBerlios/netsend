#ifndef NETSEND_UDP_PROTO_H
#define NETSEND_UDP_PROTO_H

#include <netinet/in.h>

#ifndef IPPROTO_UDPLITE
# define IPPROTO_UDPLITE 136
#endif

#ifndef UDPLITE_SEND_CSCOV
# define UDPLITE_SEND_CSCOV   10
#endif

#ifndef UDPLITE_RECV_CSCOV
# define UDPLITE_RECV_CSCOV   11
#endif

int udp_listen(int sockfd, int);

void udplite_setsockopt_recv_csov(int connected_fd, uint16_t cov);

struct opts;
void udp_trans_mode(struct opts*);

#endif /* NETSEND_UDP_PROTO_H */
