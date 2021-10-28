#ifndef JOS_HTTPD_LSOCKET_H
#define JOS_HTTPD_LSOCKET_H

// wrapper for lwip socket calls that acquire the lwip_core_lock 
// before the socket call and release it when the call completes

#include <lwip/sockets.h>

int laccept(int s, struct sockaddr *addr, socklen_t *addrlen);
int laccept(int s, struct sockaddr *addr, socklen_t *addrlen);
int lbind(int s, struct sockaddr *name, socklen_t namelen);
int lshutdown(int s, int how);
int lgetpeername (int s, struct sockaddr *name, socklen_t *namelen);
int lgetsockname (int s, struct sockaddr *name, socklen_t *namelen);
int lgetsockopt (int s, int level, int optname, void *optval, socklen_t *optlen);
int lsetsockopt (int s, int level, int optname, const void *optval, socklen_t optlen);
int lclose(int s);
int lconnect(int s, struct sockaddr *name, socklen_t namelen);
int llisten(int s, int backlog);
int lrecv(int s, void *mem, int len, unsigned int flags);
int lread(int s, void *mem, int len);
int lrecvfrom(int s, void *mem, int len, unsigned int flags,
	      struct sockaddr *from, socklen_t *fromlen);
int lsend(int s, void *dataptr, int size, unsigned int flags);
int lsendto(int s, void *dataptr, int size, unsigned int flags,
	    struct sockaddr *to, socklen_t tolen);
int lsocket(int domain, int type, int protocol);
int lwrite(int s, void *dataptr, int size);
int lselect(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset,
	    struct timeval *timeout);
int lioctl(int s, long cmd, void *argp);

#endif
