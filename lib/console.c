#include <inc/syscall.h>
#include <inc/fd.h>
#include <inc/console.h>

#include <errno.h>
#include <fcntl.h>

int
opencons(void)
{
    int r;
    struct Fd* fd;
    
    if ((r = fd_alloc(&fd, "console fd")) < 0) {
	__set_errno(ENOMEM);
	return -1;
    }
    
    fd->fd_dev_id = devcons.dev_id;
    fd->fd_omode = O_RDWR;
    jos_atomic_set(&fd->fd_ref, 1);
    return fd2num(fd);
}

static ssize_t
cons_write(struct Fd *fd, const void *buf, size_t count, off_t offset)
{
    int r = sys_cons_puts((const char *)buf, count);
    if (r < 0) {
	errno = EFAULT;
	return -1;
    }

    return count;
}

struct Dev devcons = 
{
    .dev_id = 'c',
    .dev_name = "cons",
    .dev_write = cons_write,
};
