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


void xgetaddrinfo(const char *, const char *, struct addrinfo *, struct addrinfo **);

void xsetsockopt(int, int, int, const void *, socklen_t, const char *);

int xsnprintf(char *, size_t , const char *, ...);

char *tcp_ca_code2str(int);

char *xstrdup(const char *src);

void xfstat(int filedes, struct stat *buf, const char *str);

void xpipe(int filedes[2]);

/* vim:set ts=4 sw=4 sts=4 tw=78 ff=unix noet: */
