#ifndef RAWSOCK_PERF_TEST_H
#define RAWSOCK_PERF_TEST_H

enum { iterations = 100 };
enum { connections = 20 };
enum { packets = 100 };
enum { packet_len = 16 };

enum { port = 9999 };
enum { irq_affinity = 1};

#define TEST_BUDDY_IP "172.23.166.18"

#define DEF_TEST_SOCKET_CLOSE(socketfn, closefn)		\
static void							\
test_socket_close(void)						\
{								\
    uint64_t time_socket = 0, time_close = 0, start;		\
    int fd;							\
    for (int i = 0; i < iterations; i++) {			\
	start = read_tsc();					\
        echeck(fd = xsocket(PF_INET, SOCK_STREAM, 0));		\
	time_socket += read_tsc() - start;			\
	start = read_tsc();					\
	echeck(xclose(fd));					\
	time_close += read_tsc() - start;			\
    }								\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #socketfn, time_socket / iterations);		\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #closefn, time_close / iterations);			\
}

#define DEF_TEST_BIND_LISTEN(bindfn, listenfn)			\
static void							\
test_bind_listen(void)						\
{								\
    uint64_t time_bind = 0, time_listen = 0, start;		\
    struct sockaddr_in sa;					\
    bzero(&sa, sizeof(sa));					\
    sa.sin_family = AF_INET;					\
    sa.sin_port = htons(port);					\
    sa.sin_addr.s_addr = htonl(INADDR_ANY);			\
    for (int i = 0; i < iterations; i++) {			\
	int fd;							\
	echeck(fd = xsocket(PF_INET, SOCK_STREAM, 0));		\
	start = read_tsc();					\
	echeck(xbind(fd, (struct sockaddr*) &sa, sizeof(sa)));	\
	time_bind += read_tsc() - start;			\
	start = read_tsc();					\
	echeck(xlisten(fd, 5));					\
	time_listen += read_tsc() - start;			\
	xclose(fd);						\
    }								\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #bindfn, time_bind / iterations);			\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #listenfn, time_listen / iterations);		\
}

/* test connect */
#define DEF_TEST_CONNECT(connectfn)				\
static void							\
test_connect(void)						\
{								\
    uint64_t time_connect = 0, start;				\
    struct sockaddr_in saddr;					\
    memset(&saddr, 0, sizeof(saddr));				\
    saddr.sin_addr.s_addr = inet_addr(TEST_BUDDY_IP);		\
    saddr.sin_family = AF_INET;					\
    saddr.sin_port = htons(port);				\
    int fd;							\
    echeck(fd = xsocket(PF_INET, SOCK_STREAM, 0));		\
    while (xconnect(fd, (sockaddr *)&saddr, sizeof(saddr)));	\
    xclose(fd);							\
    for (int i = 0; i < connections; i++) {			\
	echeck(fd = xsocket(PF_INET, SOCK_STREAM, 0));		\
	start = read_tsc();					\
	echeck(xconnect(fd, (sockaddr *)&saddr, 		\
	       sizeof(saddr)));					\
	time_connect += read_tsc() - start;			\
	echeck(xclose(fd));					\
    }								\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #connectfn, time_connect / connections);		\
}

/* test accept, read, write*/
#define DEF_TEST_AS_SERVER(acceptfn,readfn,writefn)		\
static void							\
test_as_server(void)						\
{								\
    uint64_t time_accept = 0, time_read = 0, 			\
	     time_write = 0, start;				\
    int fd;							\
    echeck(fd = xsocket(PF_INET, SOCK_STREAM, 0));		\
    struct sockaddr_in sa;					\
    bzero(&sa, sizeof(sa));;					\
    sa.sin_family = AF_INET;					\
    sa.sin_port = htons(port);					\
    sa.sin_addr.s_addr = htonl(INADDR_ANY);			\
    echeck(xbind(fd, (struct sockaddr*) &sa, sizeof(sa)));	\
    echeck(xlisten(fd, 5));					\
    int sync_client = xaccept(fd, 0, 0);			\
    xclose(sync_client);					\
    int client;							\
    for (int i = 0; i < connections; i++) {			\
	start = read_tsc();					\
	echeck(client = xaccept(fd, 0, 0));			\
	time_accept += read_tsc() - start;			\
	xclose(client);						\
    }								\
    char buf[packet_len];					\
    client = xaccept(fd, 0, 0);					\
    for (int i = 0; i < packets; i++) {				\
	start = read_tsc();					\
	assert(xread(client, buf, sizeof(buf)) == sizeof(buf));	\
	time_read += read_tsc() - start;			\
	start = read_tsc();					\
	assert(xwrite(client, buf, sizeof(buf)) == sizeof(buf));\
	time_write += read_tsc() - start;			\
    }								\
    echeck(xclose(client));					\
    echeck(xclose(fd));						\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #acceptfn, time_accept / connections);		\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #readfn, time_read / packets);			\
    cprintf("cycles per operations: %s = %lu\n", 		\
	    #writefn, time_write / packets);			\
}

void test_raw_socket_close();
void test_raw_bind_listen();
void test_raw_connect();
void test_raw_as_server();
#endif
