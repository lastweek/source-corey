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
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <bits/unimpl.h>

libc_hidden_proto(getdtablesize)
libc_hidden_proto(ioctl)
//libc_hidden_proto(lseek)
extern __typeof(open) __libc_open;
libc_hidden_proto(__libc_open)
__typeof(fcntl) __libc_fcntl;
libc_hidden_proto(__libc_fcntl)
libc_hidden_proto(fcntl)
libc_hidden_proto(fstat)

libc_hidden_proto(mkdir)
libc_hidden_proto(creat)

enum { debug = 1 };
enum { dir_type, file_type };

static int
fh_fdalloc(struct fs_handle *fh, int flags, const char *path)
{
    struct Fd *fd;
    int r = fd_alloc(&fd, path);
    if (r < 0) {
        __set_errno(ENFILE);
        return -1;
    }
    fd->fd_dev_id = 'm';
    fd->fd_omode = flags;
    if (flags & O_TRUNC) {
        r = fs_ftruncate(fh, 0);
        if (r < 0) {
            fd_free(fd);
            __set_errno(EACCES);
            return -1;
        }
        fd->fd_offset = 0;
    }
    fd->fd_fh.fh = *fh;
    // how to get the number of arguments after flags?
    //fd->fd_fh.mode = 0;
    jos_atomic_set(&fd->fd_ref, 1);
    return fd2num(fd);
}

static int
open_dir(const char *path, struct fs_handle *h)
{
    struct fs_handle p;
    int r;
    if (!path)
        p = core_env->cwd;
    else {
        r = fs_namei(path, &p);
        if (r < 0) {
            __set_errno(ENOENT);
            if (debug)
                cprintf("fail to find path %s\n", path);
            return -1;
        }
        //check if p is a directory
        struct fs_stat st;
        r = fs_stat(&p, &st);
        if (r < 0) {
            return r;
        }
        if (!S_ISDIR(st.mode)) {
            __set_errno(ENOTDIR);
            return -1;
        }
    }

    *h = p;
    return 0;
}

static int
fh_creat(const char *path, mode_t mode, int type, struct fs_handle *h)
{
    struct fs_handle p;
    size_t len = strlen(path);
    if (path[len - 1] == '/') {
        __set_errno(EINVAL);
        return -1;
    }

    char *pend = strrchr(path, '/');
    char *ppath = 0;
    if (pend) {
        size_t plen = pend - path + 1;
        ppath = (char *) malloc(plen + 1);
        strncpy(ppath, path, plen);
        ppath[plen] = 0;
    }

    int r = open_dir(ppath, &p);
    if (ppath)
        free(ppath);
    if (r < 0)
        return r;

    struct fs_handle o;
    const char *name = pend ? (pend + 1) : path;
    r = fs_dlookup(&p, name, &o);
    if (!r) {
        if (debug)
            cprintf("%s already exsited\n", path);
        __set_errno(EEXIST);
        return -1;
    }
    if (type == file_type) {
        r = fs_create(&p, name, &o);
        if (r < 0) {
            if (debug)
                cprintf("no space for %s\n", path);
            __set_errno(ENOSPC);
            return r;
        }
    }
    else {
        r = fs_mkdir(&p, name, &o);
        if (r < 0) {
            if (debug)
                cprintf("no space for %s\n", path);
            __set_errno(ENOSPC);
            return r;
        }
    }
    if (h)
        *h = o;
    return 0;
}

static int
check_open_flags(int flags)
{
    if ((flags & O_RDONLY) && (flags & (O_APPEND | O_TRUNC))) {
        __set_errno(EINVAL);
        return -1;
    }
    return 0;
}

int
mkdir(const char *path, mode_t mode)
{
    return fh_creat(path, mode, dir_type, 0);
}

int
creat(const char *path, mode_t mode)
{
    struct fs_handle o;
    int r = fh_creat(path, mode, file_type, &o);
    if (r < 0)
        return r;
    return fh_fdalloc(&o, O_TRUNC | O_CREAT | O_RDWR, path);
}

int
getdtablesize(void)
{
    // 0, 1, and 2
    return 3;
}
/*
off_t 
lseek(int fildes, off_t offset, int whence)
{
    set_enosys();
    return -1;
}
*/
int
__libc_open(const char *pn, int flags, ...)
{
    int r;
    r = check_open_flags(flags);
    if (r < 0)
        return r;
    struct fs_handle o;
    r = fs_namei(pn, &o);
    if (r < 0) {
        if (flags & O_CREAT) {
            return creat(pn, 0);
        } else {
            __set_errno(ENOENT);
            return -1;
        }
    }
    return fh_fdalloc(&o, flags, pn);
}

int 
ioctl(int __fd, unsigned long int __request, ...)
{
    set_enosys();
    return -1;
}

int
__libc_fcntl(int fdnum, int cmd, ...)
{
    set_enosys();
    return -1;
}

int
fstat(int fdnum, struct stat *buf)
{
    set_enosys();
    return -1;
}

weak_alias(__libc_fcntl, fcntl);

libc_hidden_def(getdtablesize)
libc_hidden_def(ioctl)
//libc_hidden_def(lseek)
libc_hidden_def(__libc_fcntl)
libc_hidden_weak(fcntl)
libc_hidden_def(fstat)

libc_hidden_def(mkdir)
libc_hidden_def(creat)

libc_hidden_proto(open)
weak_alias(__libc_open, open);
libc_hidden_weak(open)
libc_hidden_def(__libc_open)
