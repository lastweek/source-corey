#ifndef JOS_INC_FD_H
#define JOS_INC_FD_H

#include <inc/share.h>
#include <machine/atomic.h>
#include <inc/fs.h>
#include <arpa/inet.h>

#define MAXFD  64

struct Fd;

struct Dev {
    uint8_t dev_id;
    const char *dev_name;

    ssize_t (*dev_read) (struct Fd * fd, void *buf, size_t len,
			    off_t offset);
    ssize_t (*dev_write) (struct Fd * fd, const void *buf, size_t len,
			     off_t offset);
    int (*dev_close) (struct Fd * fd);
    off_t (*dev_lseek) (struct Fd * fd, off_t offset, int whence);
    //socket API
    int (*dev_connect) (struct Fd * fd, const struct sockaddr * addr,
			socklen_t addrlen);
    int (*dev_bind) (struct Fd * fd, const struct sockaddr * my_addr,
		     socklen_t addrlen);
    int (*dev_listen) (struct Fd * fd, int backlog);
    int (*dev_accept) (struct Fd * fd, struct sockaddr * addr,
		       socklen_t * addrlen);
};

struct Fd {
    uint8_t fd_dev_id;
    uint8_t fd_isatty;
    // for files on mounted file system, fd_omode
    // stores the flags parameter of open
    uint32_t fd_omode;
    off_t fd_offset;
    jos_atomic_t fd_ref;

    union {
	struct {
	    int s;
	    int type;
	} fd_sock;
	struct {
	    struct fs_handle fh;
	    // currently we do not support user or group
	    //mode_t mode;
	} fd_fh;
    };

};

extern struct Dev devcons;	/* type 'c' */
extern struct Dev devsock;	/* type 's' */
extern struct Dev devmfs;	/* type 'm' */

int fd_alloc(struct Fd **fd_store, const char *name);
int fd_free(struct Fd *fd);
int fd2num(struct Fd *fd);
int fd_lookup(int fdnum, struct Fd **fd_store, struct sobj_ref *objp);

#endif
