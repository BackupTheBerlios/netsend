#include <stddef.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>


void *xmalloc(size_t len);

static inline void *xzalloc(size_t len)
{
	void *p = xmalloc(len);
	if (p)
		memset(p, 0, len);
	return p;
}


void xgetaddrinfo(const char *node, const char *service, struct addrinfo *hints, struct addrinfo **res);

void xsetsockopt(int s, int level, int optname, const void *optval, socklen_t optlen, const char *str);

int xsnprintf(char *str, size_t size, const char *format, ...);

char *xstrdup(const char *src);

void xfstat(int filedes, struct stat *buf, const char *str);

void xpipe(int filedes[2]);

