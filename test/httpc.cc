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

static char debug = 0;
static char warn = 1;
static char just_read = 0;
static char loose_count = 0;

static int clients = 1;
static int clientconns = 10;
static unsigned int key_limit = 1000;
static const char *path = "/";
static int filesize = 1024;


char *sync_server_name = NULL;
char *sync_server_ip = NULL;
uint16_t sync_server_port = 0;

static char *request_template = ""
    "HTTP/1.0\r\nUser-Agent: "
    "TestClient\r\nHost: %s:%d\r\n"
    "\r\n";
static char *request;
static int request_len;
static volatile uint32_t conn_num = 0;
static int sync_sock;
static uint32_t running_time = 5;  // seconds

#define errno_check(expr)                                               \
    do {                                                                \
        int64_t __r = (expr);                                           \
        if (__r < 0) {                                                  \
            fprintf(stderr, "%s:%u: %s - %s\n",                         \
                                  __FILE__, __LINE__, #expr,            \
                                  strerror(errno));                     \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)

enum { to_init, to_connect, first_read, to_read };
struct sock_slot {
    int sock;
    int state;
};

static void
inc_count(void)
{
    __asm__ __volatile__("lock ; incl %0"
			 :"=m" (conn_num)
			 :"m" (conn_num));
}

static void
handle_readable(int fd, struct sock_slot *ss, int n)
{
    int slot = -1;
    for (int i = 0; i < n; i++) {
	if (ss[i].sock == fd) {
	    slot = i;
	    break;
	}
    }
    assert(slot >= 0);

    int r;
    char buf[4 * 4096];
    if (ss[slot].state == first_read || ss[slot].state == to_read ) {
	r = read(ss[slot].sock, buf, sizeof(buf));
	if (r == 0) {
	    if (ss[slot].state != first_read)
		inc_count();
	    close(ss[slot].sock);
	    ss[slot].state = to_init;
	    return;
	} else if (r < 0) {
	    if (warn)
		fprintf(stderr, "read error: %s\n", strerror(errno));
	    if (ss[slot].state != first_read && loose_count)
		inc_count();
	    close(ss[slot].sock);
	    ss[slot].state = to_init;
	    return;
	}

	buf[r] = '\0';
	if (ss[slot].state == first_read && warn)
	    if (memcmp("HTTP/1.1 200", buf, strlen("HTTP/1.1 200")) &&
		memcmp("HTTP/1.0 200", buf, strlen("HTTP/1.0 200")))
	    {
		fprintf(stderr, "%s\n", buf);
	    }
	if (debug)
	    fprintf(stderr, "%s", buf);
	ss[slot].state = to_read;
    } else {
	fprintf(stderr, "unknown read state: %d\n", ss[slot].state);
	exit(EXIT_FAILURE);
    }
}

static void
handle_writeable(int fd, struct sock_slot *ss, int n)
{
    int slot = -1;
    for (int i = 0; i < n; i++) {
	if (ss[i].sock == fd) {
	    slot = i;
	    break;
	}
    }
    assert(slot >= 0);

    char getbuf[32];
  
    if (ss[slot].state == to_connect) {
	int e;
	socklen_t len = sizeof(e);
	errno_check(getsockopt(ss[slot].sock, SOL_SOCKET, SO_ERROR, 
			       &e, &len));
	if (e != 0) {
	    fprintf(stderr, "unable to connect: %s\n", strerror(e));
	    return;
	}

	if (!just_read) {
	    unsigned int key = random() % key_limit;
	    if (!key)
		key = 1;
    
	    // Send first part of request msg.
	    int r;
	    sprintf(getbuf, "GET /%u.%u ", key, filesize);
	    int getlen = strlen(getbuf);
	    errno_check(r = write(ss[slot].sock, getbuf, getlen));
	    assert(r == getlen);

	    // Send second part of request msg.
	    errno_check(r = write(ss[slot].sock, request, request_len));
	    assert(r == request_len);
	}
	ss[slot].state = first_read;
    } else {
	fprintf(stderr, "unknown write state: %d\n", ss[slot].state);
	exit(EXIT_FAILURE);
    }
}

static void
do_sock_events(struct sock_slot *ss, struct sockaddr_in *addr, int n)
{
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    int maxfd = -1;
    for (int i = 0; i < n; i++) {
	if (ss[i].state == to_init) {
	    int s;
	    errno_check(s = socket(PF_INET, SOCK_STREAM, 0));
	    int flags;
	    errno_check(flags = fcntl(s, F_GETFL));
	    flags |= O_NONBLOCK;
	    errno_check(fcntl(s, F_SETFL, flags));
	    int r = connect(s, (struct sockaddr *)addr, sizeof(*addr));
	    if (r != -1 || errno != EINPROGRESS) {
		fprintf(stderr, "unexpected connect result: %d, %s\n", r, 
			strerror(errno));
		exit(EXIT_FAILURE);
	    }
	    ss[i].sock = s;
	    ss[i].state = to_connect;
	} 
	
	if (ss[i].state == to_connect)
	    FD_SET(ss[i].sock, &writefds);
	else if (ss[i].state == first_read || ss[i].state == to_read)
	    FD_SET(ss[i].sock, &readfds);

	if (ss[i].sock > maxfd)
	    maxfd = ss[i].sock;
    }

    int r;
    errno_check(r = select(maxfd + 1, &readfds, &writefds, 0, 0));
    for (int i = 0, j = 0; i <= maxfd && j < r; i++) {
	if (FD_ISSET(i, &readfds))
	{
	    handle_readable(i, ss, n);
	}
	else if (FD_ISSET(i, &writefds))
	{
	    handle_writeable(i, ss, n);
	}
    }
}

static void *
client(void *a)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)a;
    
    struct sock_slot ss[clientconns];
    for (int i = 0; i < clientconns; i++)
	ss[i].state = to_init;

    for (;;)
	do_sock_events(ss, addr, clientconns);
    
    return 0;
}

static void
load_host_file(const char *fn, struct sockaddr_in **addrs, int *n, uint16_t port)
{
    FILE *f = fopen(fn, "r");
    if (!f) {
	fprintf(stderr, "unable to open host file\n");
	exit(EXIT_FAILURE);
    }
    
    char buf[128];
    int i = 0;
    while (fgets(buf, sizeof(buf), f)) {
	int n = strlen(buf);
	if (n == 0) {
	    fprintf(stderr, "0 length host entry\n");
	    exit(EXIT_FAILURE);
	}
	buf[n - 1] = '\0';

	struct sockaddr_in *addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
	assert(addr);
	memset(addr, 0, sizeof(*addr));
	
	struct hostent *hp;
	if(!(hp = gethostbyname(buf))) {
	    fprintf(stderr, "unable to resolve host: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	
	addr->sin_addr = *((struct in_addr*) hp->h_addr_list[0]);
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);

	addrs[i] = addr;
	i++;
    }
    *n = i;
}

static void
load_conf_file(const char *fn)
{
    FILE *f = fopen(fn, "r");
    if (!f) {
	fprintf(stderr, "unable to open configuration file %s\n", fn);
	exit(EXIT_FAILURE);
    }

    fprintf(stderr, "loading configuration file %s\n", fn);

    char *key;
    char *value;
    char buf[256];

    while (!feof(f)) {
	fscanf(f, "%s\n", buf);
	char *ch = strstr(buf, "=");
	*ch = '\0';
	value = ch + 1;
	key = buf;
	if (!strcmp(key, "server_name")) {
	    sync_server_name = (char *)malloc(strlen(value) + 1);
	    strcpy(sync_server_name, value);
	    continue;
	}
	if (!strcmp(key, "server_ip")) {
	    sync_server_ip = (char *)malloc(strlen(value) + 1);
	    strcpy(sync_server_ip, value);
	    continue;
	}
	if (!strcmp(key, "server_port")) {
	    sync_server_port = atoi(value);
	    continue;
	}
	fprintf(stderr, "unknown configuration parameter\n");
	exit(EXIT_FAILURE);
    }

    fclose(f);
}

static void 
client_sync(void) 
{
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    struct sockaddr_in sync_server_addr;
    struct hostent *sync_server_hp = 0;

    if (sync_server_name) {
        if(!(sync_server_hp = gethostbyname(sync_server_name))) {
 	    fprintf(stderr, "unable to resolve sync server: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
	}
    }
    
    memset(&sync_server_addr, 0, sizeof(sync_server_addr));

    if (sync_server_hp) {
        sync_server_addr.sin_addr = 
	    *((struct in_addr*) sync_server_hp->h_addr_list[0]);
    } else if (sync_server_ip) {
	sync_server_addr.sin_addr.s_addr = inet_addr(sync_server_ip);
    } else {
	fprintf(stderr, "unable to identify sync server\n");
	exit(EXIT_FAILURE);
    }
    sync_server_addr.sin_family = AF_INET;
    sync_server_addr.sin_port = htons(sync_server_port);    
    
    errno_check(sync_sock = socket(PF_INET, SOCK_STREAM, 0));
    int r = connect(sync_sock, (struct sockaddr *)&sync_server_addr, sizeof(sync_server_addr));
    if (r != 0) {
        fprintf(stderr, "unexpected connect result: %d, %s\n", r, strerror(errno));
        exit(EXIT_FAILURE);
    }

    uint32_t reg = htonl(1);
    
    //send(s, &reg, sizeof(reg), 0);
    recv(sync_sock, &reg, sizeof(reg), 0);
    running_time = ntohl(reg);
    fprintf(stderr, "begin testing %d\n", running_time);
}

int
main(int ac, char **av)
{
    struct timeval tick, tick1, tick2;

    if (ac < 3) {
	fprintf(stderr, "usage: %s host port [options]\n", av[0]);
	fprintf(stderr, "options:\n");
	fprintf(stderr, " -t running-time : seconds to measure throughput\n");
	fprintf(stderr, " -c num-clients : number of client threads\n");
	fprintf(stderr, " -s conns-per-thread : number sockets per thread\n");
	fprintf(stderr, " -p path : path to request\n");
	fprintf(stderr, " -d : debug output\n");
	fprintf(stderr, " -k key-limit : max key to request\n");
	fprintf(stderr, " -h host-file : file with list of server IPs\n");
	fprintf(stderr, " -f conf-file : file with sync server config\n");
	fprintf(stderr, " -q : silence warnings\n");
	fprintf(stderr, " -r : read mode, do not write\n");
	fprintf(stderr, " -l : loose count\n");
	fprintf(stderr, " -z file size: file size to send to server\n");
	exit(EXIT_FAILURE);
    }

    srandom(getpid() * time(0));

    static const char *host = av[1];
    static uint16_t port = atoi(av[2]);

    int num_hosts_file = 0;
    struct sockaddr_in *hosts[16];
    int sync_mode = 0;
    
    int c;
    while ((c = getopt(ac-2, av+2, "c:t:p:s:dhf:k:qrlz:")) != -1) {
	switch(c) {
	case 'c':
	    clients = atoi(optarg);
	    break;
	case 't':
	    running_time = atoi(optarg);
	    break;
	case 's':
	    clientconns = atoi(optarg);
	    break;
	case 'p':
	    path = strdup(optarg);
	    break;
	case 'd':
	    debug = 1;
	    break;
	case 'q':
	    warn = 0;
	    break;
	case 'r':
	    just_read = 1;
	    break;
	case 'k':
	    key_limit = atoi(optarg);
	    break;
	case 'h':
	    load_host_file(optarg, hosts, &num_hosts_file, port);
	    break;
	case 'f':
	    load_conf_file(optarg);
	    sync_mode = 1;
	    break;
	case 'l':
	    loose_count = 1;
	    break;
	case 'z':
	    filesize = atoi(optarg);
	    break;
	}
    }

    request_len = strlen(request_template) + strlen(path) + strlen(host) + 6;

    request = (char *) malloc(request_len);
    snprintf(request, request_len, request_template, host, port);
    request_len = strlen(request);

    struct hostent *hp;
    
    if(!(hp = gethostbyname(host))) {
	fprintf(stderr, "unable to resolve host: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    struct sockaddr_in def_addr;
    
    memset(&def_addr, 0, sizeof(def_addr));
    def_addr.sin_addr = *((struct in_addr*) hp->h_addr_list[0]);
    def_addr.sin_family = AF_INET;
    def_addr.sin_port = htons(port);

    if (sync_mode)
	client_sync();

    if (num_hosts_file) {
	printf("Connecting to %d hosts with a key limit of %u\n", 
	       num_hosts_file, key_limit);
	
	pthread_t th;
	for (int i = 0; i < clients; i++)
	    errno_check(pthread_create(&th, 0, client, hosts[i % num_hosts_file]));
    } else {
	printf("Connecting to %s:%u with a key limit of %u\n", host, port, key_limit);

	pthread_t th;
	for (int i = 0; i < clients; i++)
	    errno_check(pthread_create(&th, 0, client, &def_addr));
    }

    sleep(running_time); 
    uint32_t conn = conn_num;
    fprintf(stderr, "%d connections\n", conn); 
    if (sync_mode) {
	conn = htonl(conn);
	send(sync_sock, &conn, sizeof(conn), 0);
	close(sync_sock);
    }
}
