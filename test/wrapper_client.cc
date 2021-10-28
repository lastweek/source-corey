#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

enum { connections = 20 };
enum { packets = 100 };
enum { packet_len = 16 };
enum { port = 9999 };

#define TEST_SERVER_IP "192.168.1.4"

#define echeck(expr)					\
    ({							\
	int64_t __c = (expr);				\
	if (__c < 0)  				        \
            printf("%s: %ld", #expr,__c); 		\
	__c;						\
    })


static void
as_server(void)
{
    // run as a server
    int fd;
    echeck(fd = socket(PF_INET, SOCK_STREAM, 0));

    struct sockaddr_in sa;
    bzero(&sa, sizeof(sa));;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    echeck(bind(fd, (struct sockaddr*) &sa, sizeof(sa)));
    echeck(listen(fd, 5));
    printf("waiting for the connection\n");
    // sycn with the client
    int sync_sock = accept(fd, 0, 0);
    close(sync_sock);
    printf("synced with the client\n");
    int clients[connections];
    for (int i = 0; i < connections; i++)
	echeck(clients[i] = accept(fd, 0, 0));
    for (int i = 0; i < connections; i++)
	close(clients[i]);
    close(fd);
    printf("all connections accepted\n");
}

static void
as_client(void)
{
    // resolve the server
    struct sockaddr_in def_addr;
    memset(&def_addr, 0, sizeof(def_addr));
    def_addr.sin_addr.s_addr = inet_addr(TEST_SERVER_IP);
    def_addr.sin_family = AF_INET;
    def_addr.sin_port = htons(port);
    printf("connecting to server %s\n", TEST_SERVER_IP);
    // create the socket for accept test
    int fd;
    echeck(fd = socket(PF_INET, SOCK_STREAM, 0));
    // connect for the first time to sycn with server
    while (connect(fd, (const sockaddr *)&def_addr, sizeof(def_addr)));
    close(fd);
    printf("synced with server\n");
    int socks[connections];
    for (int i = 0; i < connections; i++) {
	echeck(socks[i] = socket(PF_INET, SOCK_STREAM, 0));
	if (connect(socks[i], (const sockaddr *)&def_addr, sizeof(def_addr)))
	    printf("%dth connection failed\n");
    }
    for (int i = 0; i < connections; i++)
	close(socks[i]);
    printf("all sockets connected\n");
    char buf[packet_len];
    memset(buf, 0, sizeof(buf));
    // create the connection for read and write test
    echeck(fd = socket(PF_INET, SOCK_STREAM, 0));
    if (connect(fd, (const sockaddr *)&def_addr, sizeof(def_addr))) {
	printf("fail to connect to server for read and write test\n");
	return;
    }
    for (int i = 0; i < packets; i++) {
	// send packets
	assert(write(fd, buf, sizeof(buf)) == sizeof(buf));
	// recv response
	assert(read(fd, buf, sizeof(buf)) == sizeof(buf));
    }
    close(fd);
    printf("all packets sent\n");
}

int
main(int ac, char **av)
{
    as_server();
    as_client();
    as_server();
    as_client();
    return 0;
}
