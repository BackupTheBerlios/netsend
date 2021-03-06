#include <sys/types.h>
#include <sys/socket.h>

#include "config.h"

#ifdef HAVE_AF_TIPC
#include <linux/tipc.h>

int tipc_socket_bind(void);

ssize_t tipc_write(int fd, const void *buf, size_t count);

int tipc_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int tipc_listen(int sockfd, int);

#else
static inline int tipc_socket_bind(void) { return -1; }
#endif

void tipc_trans_mode(void);

