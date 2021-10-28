#include <inc/lib.h>
#include <inc/lwipwrap.h>

#include <lwip/sockets.h>
#include <lwipinc/lsocket.h>

static void
wrap_to_lwip(struct wrap_sockaddr_in *wsin, struct sockaddr_in *sin)
{
    sin->sin_family = AF_INET;
    sin->sin_port = wsin->sin_port;
    sin->sin_addr.s_addr = wsin->sin_addr;
}

#if 0
static void
lwip_to_wrap(struct sockaddr_in *sin, struct wrap_sockaddr_in *wsin)
{
    wsin->sin_port = sin->sin_port;
    wsin->sin_addr = sin->sin_addr.s_addr;
}
#endif

int
wrap_socket(int domain, int type, int protocol)
{
    return lsocket(domain, type, protocol);
}

int
wrap_connect(int s, struct wrap_sockaddr_in *wsin)
{
    struct sockaddr_in sin;
    wrap_to_lwip(wsin, &sin);
    return lconnect(s, (struct sockaddr *) &sin, sizeof(sin));
}

ssize_t
wrap_read(int s, void *buf, size_t count)
{
    return lread(s, buf, count);
}

ssize_t
wrap_write(int s, const void *buf, size_t count)
{
    return lwrite(s, (void *) buf, count);
}

int
wrap_bind(int s, struct wrap_sockaddr_in *my_addr)
{
    struct sockaddr_in sin;
    wrap_to_lwip(my_addr, &sin);
    return lbind(s, (struct sockaddr *) &sin, sizeof(sin));
}

int
wrap_listen(int s, int backlog)
{
    return llisten(s, backlog);
}

int
wrap_accept(int s, struct sockaddr *addr, uint32_t * addrlen)
{
    return laccept(s, addr, addrlen);
}

int
wrap_close(int s)
{
    return lclose(s);
}
