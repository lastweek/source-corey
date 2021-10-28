#include <inc/lib.h>
#include <inc/fd.h>
#include <inc/fs.h>

#include <fcntl.h>
#include <errno.h>

static ssize_t
fs_fd_write(struct Fd *fd, const void *buf, size_t count, off_t offset)
{
    if (fd->fd_omode & O_RDONLY) {
        __set_errno(EACCES);
        return -1;
    }
    if (fd->fd_omode & O_APPEND) {
        struct fs_stat s;
        fs_stat(&fd->fd_fh.fh, &s);
        return fs_pwrite(&fd->fd_fh.fh, buf, count, s.size);
    } else {
        ssize_t s = fs_pwrite(&fd->fd_fh.fh, buf, count, offset);
        if (s < 0) {
            //set errno
            return -1;
        }
        return s;
    }
}

static ssize_t
fs_fd_read(struct Fd *fd, void *buf, size_t count, off_t offset)
{
    if (fd->fd_omode & O_WRONLY) {
        __set_errno(EACCES);
        return -1;
    }
    ssize_t s = fs_pread(&fd->fd_fh.fh, buf, count, offset);
    if (s < 0) {
        //set errno
        return -1;
    }
    return s;
}

static off_t
fs_fd_lseek(struct Fd *fd, off_t offset, int whence)
{
    if (whence == SEEK_SET)
        fd->fd_offset = offset;
    else if (whence == SEEK_CUR)
        fd->fd_offset += offset;
    else if (whence == SEEK_END) {
        struct fs_stat st;
        int r = fs_stat(&fd->fd_fh.fh , &st);
        if (r < 0)
            return r;
        fd->fd_offset = st.size + offset;
    }
    else
        return EINVAL;
    return fd->fd_offset;

}

struct Dev devmfs = {
    .dev_id = 'm',
    .dev_name = "mounted_file_system",
    .dev_read = fs_fd_read,
    .dev_write = fs_fd_write,
    .dev_lseek = fs_fd_lseek,
};
