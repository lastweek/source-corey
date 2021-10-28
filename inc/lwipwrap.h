#ifndef JOS_INC_LWIPWRAP_H
#define JOS_INC_LWIPWRAP_H

#include <unistd.h>

// We define our own sockaddr_in because we need to translate
// between libc and lwip sockaddr_in equivalents.
struct wrap_sockaddr_in {
    // These are network-order (big-endian)
    uint16_t sin_port;
    uint32_t sin_addr;
};

struct sockaddr;

int wrap_socket(int domain, int type, int protocol);
int wrap_connect(int s, struct wrap_sockaddr_in *wsin);
ssize_t wrap_read(int s, void *buf, size_t count);
ssize_t wrap_write(int s, const void *buf, size_t count);
int wrap_bind(int s, struct wrap_sockaddr_in *my_addr);
int wrap_listen(int s, int backlog);
int wrap_accept(int s, struct sockaddr *addr, socklen_t * addrlen);
int wrap_close(int s);
#endif
