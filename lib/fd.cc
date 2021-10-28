extern "C" {
#include <machine/memlayout.h>
#include <inc/lib.h>
#include <inc/fd.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/syscall.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
}

enum { debug = 0 };
enum { fd_missing_debug = 0 };

libc_hidden_proto(connect)
libc_hidden_proto(write)
libc_hidden_proto(read)
libc_hidden_proto(dup2)
libc_hidden_proto(close)
libc_hidden_proto(lseek)
libc_hidden_proto(bind)
libc_hidden_proto(listen)
libc_hidden_proto(accept)

// Bottom of file descriptor area
#define FDTABLE  (UFDBASE)
// Return the 'struct Fd*' for file descriptor index i
#define INDEX2FD(i) ((struct Fd*) (FDTABLE + (i)*PGSIZE))

// Check for null function pointers before invoking a device method
#define DEV_CALL(dev, fn, ...)						      \
    ({									      \
	__typeof__(dev->dev_##fn (__VA_ARGS__)) __r;			      \
	if (!dev->dev_##fn) {						      \
	    if (fd_missing_debug)					      \
		cprintf("Missing op: %s for type %s\n", #fn, dev->dev_name);  \
	    __set_errno(EOPNOTSUPP);					      \
	    __r = -1;							      \
	} else {							      \
	    __r = dev->dev_##fn (__VA_ARGS__);				      \
	}								      \
	__r;								      \
    })

// Try to get the Fd object mapped by fdnum
#define FD_PARSE_FD(fdnum, __fd, __seg)					      \
    struct Fd * __fd;							      \
    ({									      \
	if (fd_lookup(fdnum, &__fd, __seg) < 0 )			      \
	{								      \
	    __set_errno(EBADF);						      \
	    return -1;							      \
	}								      \
    })
	
// Try to get the device on which the fd object is created
#define FD_PARSE_DEV(__fd, __dev)					      \
    struct Dev * __dev;							      \
    ({									      \
	if (dev_lookup(__fd->fd_dev_id, &__dev) < 0)			      \
	{								      \
	    __set_errno(EBADF);						      \
	    return -1;							      \
	}								      \
    })

#define FD_PARSE(fdnum, __fd, __dev)					      \
    FD_PARSE_FD(fdnum, __fd, 0);					      \
    FD_PARSE_DEV(__fd, __dev);

// Call a method on a file descriptor number
#define FD_CALL(fdnum, fn, ...)						      \
    ({									      \
	FD_PARSE(fdnum, __fd, __dev);					      \
	DEV_CALL(__dev, fn, __fd, ##__VA_ARGS__);			      \
    })

static struct Dev *devlist[] = {
    &devcons,
    &devsock,
    &devmfs,
    0
};

int
dev_lookup(uint8_t dev_id, struct Dev **dev)
{
    for (int i = 0; devlist[i]; i++)
        if (devlist[i]->dev_id == dev_id) {
            *dev = devlist[i];
            return 0;
        }

    cprintf("dev_lookup: unknown device type %u\n", dev_id);
    return -E_INVAL;
}


int
fd2num(struct Fd *fd)
{
    return ((uintptr_t) fd - FDTABLE) / PGSIZE;
}

int
fd_alloc(struct Fd **fd_store, const char *name)
{
    int r;
    struct Fd *fd;
    static_assert(sizeof(*fd) <= PGSIZE);

    int i;
    for (i = 0; i < MAXFD; i++) {
        fd = INDEX2FD(i);
        r = fd_lookup(i, 0, 0);
        if (r < 0)
            break;
    }

    *fd_store = 0;
    if (i == MAXFD)
        return -E_NO_SPACE;

    r = segment_alloc(core_env->sh, PGSIZE, 0, (void **) &fd,
                      SEGMAP_SHARED, name, core_env->pid);
    if (r < 0)
        return r;

    *fd_store = fd;
    return 0;
}

int
fd_free(struct Fd *fd)
{
    struct u_address_mapping uam;
    int r = as_lookup(fd, &uam);
    if (r < 0)
	return r;
    if (r == 0)
	return -E_NOT_FOUND;

    assert(as_unmap(fd) == 0);
    assert(sys_share_unref(uam.object) == 0);
    return 0;

}

int
fd_lookup(int fdnum, struct Fd **fd_store, struct sobj_ref *objp)
{
    if (fdnum < 0 || fdnum >= MAXFD) {
        if (debug)
            cprintf("[%lx] bad fd %d\n", thread_id(), fdnum);
        return -E_INVAL;
    }

    struct Fd *fd = INDEX2FD(fdnum);

    struct u_address_mapping uam;
    int r = as_lookup(fd, &uam);
    if (r == 0)
        return -E_NOT_FOUND;

    if (fd_store)
        *fd_store = fd;
    if (objp)
        *objp = uam.object;
    return 0;
}

int
dup2(int oldfdnum, int newfdnum) __THROW
{
    struct sobj_ref fd_seg;
    FD_PARSE_FD(oldfdnum, oldfd, &fd_seg);

    close(newfdnum);
    struct Fd *newfd = INDEX2FD(newfdnum);

    int r = as_map(fd_seg, 0, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_SHARED,
		   (void **) &newfd, 0);
    if (r < 0) {
        __set_errno(EINVAL);
        return -1;
    }

    r = sys_share_addref(core_env->sh, fd_seg);
    if (r < 0) {
	    as_unmap(newfd);
	    __set_errno(ENOMEM);
	    return -1;
    }

    jos_atomic_inc(&oldfd->fd_ref);

    return newfdnum;
}

off_t
lseek(int fdnum, off_t offset, int whence) __THROW
{
    return FD_CALL(fdnum, lseek, offset, whence);
}

int
close(int fdnum)
{
    FD_PARSE_FD(fdnum, fd, 0);
    if (jos_atomic_dec_and_test(&fd->fd_ref)) {
	FD_PARSE_DEV(fd, dev);
	int r = DEV_CALL(dev, close, fd);
	if (r < 0)
	    return r;
    }
    return fd_free(fd);
}

ssize_t
write(int fdnum, const void *buf, size_t len)
{
    FD_PARSE(fdnum, fd, dev);
    ssize_t r = DEV_CALL(dev, write, fd, buf, len, fd->fd_offset);
    if (r >= 0)
	fd->fd_offset += r;
    return r;
}

ssize_t
read(int fdnum, void *buf, size_t n)
{
    FD_PARSE(fdnum, fd, dev);
    ssize_t r = DEV_CALL(dev, read, fd, buf, n, fd->fd_offset);
    if (r >= 0)
	fd->fd_offset += r;
    return r;
}

int
connect(int fdnum, const struct sockaddr *addr, socklen_t addrlen)
{
    return FD_CALL(fdnum, connect, addr, addrlen);
}

int
bind(int fdnum, const struct sockaddr *my_addr, socklen_t addrlen) __THROW
{
    return FD_CALL(fdnum, bind, my_addr, addrlen);
}

int
listen(int fdnum, int backlog) __THROW
{
    return FD_CALL(fdnum, listen, backlog);
}

int
accept(int fdnum, struct sockaddr *addr, socklen_t * addrlen)
{
    return FD_CALL(fdnum, accept, addr, addrlen);
}

libc_hidden_def(connect)
libc_hidden_def(write)
libc_hidden_def(read)
libc_hidden_def(dup2)
libc_hidden_def(close)
libc_hidden_def(lseek)
libc_hidden_def(bind)
libc_hidden_def(listen)
libc_hidden_def(accept)
