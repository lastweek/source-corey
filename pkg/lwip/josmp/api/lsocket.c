#include <inc/lib.h>
#include <lwipinc/lsocket.h>
#include <arch/sys_arch.h>

/* The lwip core lock serializes threads in lwip.  Whenever a
 * function is going to block in the lwip code, the lock is
 * released.  Currently, this isn't necessary for josmp, because
 * we use cooperative threading; however we may change in the
 * future.
 */

#define lwip_call(name, args...)		\
 ({						\
     lwip_core_lock();				\
     int __r = lwip_##name(args);		\
     lwip_core_unlock();			\
     __r;					\
 })

int 
laccept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    return lwip_call(accept, s, addr, addrlen);
}

int 
lbind(int s, struct sockaddr *name, socklen_t namelen)
{
    return lwip_call(bind, s, name, namelen);
}

int 
lshutdown(int s, int how)
{
    return lwip_call(shutdown, s, how);
}

int 
lgetpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    return lwip_call(getpeername, s, name, namelen);
}

int 
lgetsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    return lwip_call(getsockname, s, name, namelen);
}

int 
lgetsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    return lwip_call(getsockopt, s, level, optname, optval, optlen);
}

int 
lsetsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    return lwip_call(setsockopt, s, level, optname, optval, optlen);
}

int 
lclose(int s)
{
    return lwip_call(close, s);
}

int
lconnect(int s, struct sockaddr *name, socklen_t namelen)
{
    return lwip_call(connect, s, name, namelen);
}

int 
llisten(int s, int backlog)
{
    return lwip_call(listen, s, backlog);
}

int
lrecv(int s, void *mem, int len, unsigned int flags)
{
    return lwip_call(recv, s, mem, len, flags);
}

int 
lread(int s, void *mem, int len)
{
    return lwip_call(read, s, mem, len);
}

int 
lrecvfrom(int s, void *mem, int len, unsigned int flags,
	  struct sockaddr *from, socklen_t *fromlen)
{
    return lwip_call(recvfrom, s, mem, len, flags, from, fromlen);
}

int 
lsend(int s, void *dataptr, int size, unsigned int flags)
{
    return lwip_call(send, s, dataptr, size, flags);
}

int 
lsendto(int s, void *dataptr, int size, unsigned int flags,
	struct sockaddr *to, socklen_t tolen)
{
    return lwip_call(sendto, s, dataptr, size, flags, to, tolen);
}

int
lsocket(int domain, int type, int protocol)
{
    return lwip_call(socket, domain, type, protocol);
}

int
lwrite(int s, void *dataptr, int size)
{
    return lwip_call(write, s, dataptr, size);
}

int
lselect(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset,
	struct timeval *timeout)
{
    return lwip_call(select, maxfdp1, readset, writeset, exceptset, timeout);
}

int 
lioctl(int s, long cmd, void *argp)
{
    return lwip_call(ioctl, s, cmd, argp);
}
