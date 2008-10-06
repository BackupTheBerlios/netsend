#ifndef NETSEND_UNIX_PROTO_H
#define NETSEND_UNIX_PROTO_H

#include <sys/types.h>
#include <sys/socket.h>

struct opts;
void unix_trans_mode(struct opts*);
int unix_socket_bind(void);
int unix_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int unix_listen(int sockfd, int log);

#endif /* NETSEND_UNIX_PROTO_H */
