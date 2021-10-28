#include <machine/x86.h>
#include <inc/lib.h>
#include <inc/fd.h>
#include <inc/stdio.h>
#include <inc/lwipwrap.h>

#include <fcntl.h>
#include <errno.h>

libc_hidden_proto(socket)

static int
sock_fdalloc(int s, int type)
{
    struct Fd *fd;
    int r = fd_alloc(&fd, "socket fd");
    if (r < 0) {
	__set_errno(ENOMEM);
	return -1;
    }

    fd->fd_dev_id = devsock.dev_id;
    fd->fd_omode = O_RDWR;
    fd->fd_sock.type = type;
    fd->fd_sock.s = s;
    jos_atomic_set(&fd->fd_ref, 1);
    return fd2num(fd);

}

int
socket(int domain, int type, int protocol)
{
    //uint64_t start = read_tsc();
    if (domain != AF_INET) {
	__set_errno(EAFNOSUPPORT);
	return -1;
    }
    int r = wrap_socket(domain, type, protocol);
    //uint64_t time = read_tsc() - start;
    //cprintf("wrap_socket: %lu cycles\n", time);
    return (r < 0) ? r : sock_fdalloc(r, type);
}

libc_hidden_def(socket)

static void
libc_to_wrap(struct sockaddr_in *sin, struct wrap_sockaddr_in *wsin)
{
    wsin->sin_addr = sin->sin_addr.s_addr;
    wsin->sin_port = sin->sin_port;
}

#if 0
static void
wrap_to_libc(struct wrap_sockaddr_in *wsin, struct sockaddr_in *sin)
{
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = wsin->sin_addr;
    sin->sin_port = wsin->sin_port;
}
#endif

static int
sock_connect(struct Fd *fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!addr) {
	__set_errno(EINVAL);
	return -1;
    }
    if (addr->sa_family != AF_INET) {
	__set_errno(EAFNOSUPPORT);
	return -1;
    }

    struct wrap_sockaddr_in wsin;
    libc_to_wrap((struct sockaddr_in *) addr, &wsin);
    return wrap_connect(fd->fd_sock.s, &wsin);
}

static ssize_t
sock_write(struct Fd *fd, const void *buf, size_t count, off_t offset)
{
    return wrap_write(fd->fd_sock.s, buf, count);
}

static ssize_t
sock_read(struct Fd *fd, void *buf, size_t count, off_t offset)
{
    return wrap_read(fd->fd_sock.s, buf, count);
}

static int
sock_bind(struct Fd *fd, const struct sockaddr *my_addr, socklen_t addrlen)
{
    if (!my_addr) {
	__set_errno(EINVAL);
	return -1;
    }
    if (my_addr->sa_family != AF_INET) {
	__set_errno(EAFNOSUPPORT);
	return -1;
    }
    struct wrap_sockaddr_in wsin;
    libc_to_wrap((struct sockaddr_in *) my_addr, &wsin);
    return wrap_bind(fd->fd_sock.s, &wsin);
}

static int
sock_listen(struct Fd *fd, int backlog)
{
    return wrap_listen(fd->fd_sock.s, backlog);
}

static int
sock_accept(struct Fd *fd, struct sockaddr *addr, socklen_t * addrlen)
{
    struct sockaddr _addr;
    int r = wrap_accept(fd->fd_sock.s, &_addr, addrlen);
    if (r < 0)
	return -1;
    if (addr)
	*addr = _addr;
    return sock_fdalloc(r, fd->fd_sock.type);
}

static int
sock_close(struct Fd *fd)
{
    return wrap_close(fd->fd_sock.s);
}

struct Dev devsock = {
    .dev_id = 's',
    .dev_name = "sock",
    .dev_read = sock_read,
    .dev_write = sock_write,
    .dev_close = sock_close,
    .dev_connect = sock_connect,
    .dev_bind = sock_bind,
    .dev_listen = sock_listen,
    .dev_accept = sock_accept,
};
