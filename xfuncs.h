#include <sys/types.h>
#include <sys/socket.h>

void xgetaddrinfo(const char *node, const char *service, struct addrinfo *hints, struct addrinfo **res);

void xsetsockopt(int s, int level, int optname, const void *optval, socklen_t optlen, const char *str);

