#include <machine/mmu.h>

enum { profile = 0 };

#ifdef JOS_USER
extern "C" {
#include <stdio.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/kdebug.h>
#include <string.h>
#include <stdlib.h>
// lwip
#include <lwip/inet.h>
#include <lwipinc/lsocket.h>
}
#include <jhttpd.hh>

#include <errno.h>
#include <inc/error.hh>

#endif

#ifndef JOS_USER
#define LINUX 1
#include "linuxcompat.h"
#endif

enum { client_debug = 0 };

httpd_get *the_get;

static void
lwrite_all(int s, const char *buf, uint64_t n)
{
    while(n) {
	int r = lsend(s, (void *)buf, n, 0);
	if (r < 0)
	    throw basic_exception("write: %s", strerror(errno));
	if (r == 0)
	    throw basic_exception("write: unable to write");
	n -= r;
	buf += r;
    }
}

httpd_filesum_get::httpd_filesum_get(httpd_filesum *app)
{
	app_ = app;
}

void
httpd_filesum_get::process_request(int s, const char *get)
{
    const char *szstr = strchr(get, '.');
    if (!szstr)
	panic("bad get %s\n", get);

    uint64_t key = atol(get + 1);
    uint64_t fsize = atol(szstr + 1);
    
    uint64_t sum;
    char buf[4096];

    sum = app_->compute(key, fsize);
    snprintf(buf, sizeof(buf),
	     "HTTP/1.0 200 OK\r\n"
	     "Content-Type: text/html\r\n"
	     "\r\n"
	     "%lx", sum);
    lwrite_all(s, buf, strlen(buf));
}

httpd_db_select_get::httpd_db_select_get(httpd_db_select * app) {
	app_ = app;
}

void
httpd_db_select_get::process_request(int s, const char * get)
{
	uint64_t key = atol(get + 1);
	char buf[4096];

	app_->compute(key);
    snprintf(buf, sizeof(buf),
	     "HTTP/1.0 200 OK\r\n"
	     "Content-Type: text/html\r\n"
	     "\r\n"
	     "select");
	lwrite_all(s, buf, strlen(buf));
}

httpd_db_join_get::httpd_db_join_get(httpd_db_join * app) {
	app_ = app;
}

void
httpd_db_join_get::process_request(int s, const char * get)
{
	uint64_t key = atol(get + 1);
	char buf[4096];

	app_->compute(key);
    snprintf(buf, sizeof(buf),
	     "HTTP/1.0 200 OK\r\n"
	     "Content-Type: text/html\r\n"
	     "\r\n"
	     "join");
	lwrite_all(s, buf, strlen(buf));
}

static void
http_client(uint64_t s)
{
    int f = s;
    char buf[4096];
    char *path;
    int n;

    try {
	// XXX make sure to recv entire request
	if ((n = lrecv(f, buf, sizeof(buf) - 1, 0)) < 0)
	    throw basic_exception("http_client: recv error: %s", strerror(errno));
	buf[n] = 0;

	if (n == 0 || strncmp(buf, "GET ", 4))
	    throw basic_exception("bad http request: %s\n", buf);
	
	path = strchr(buf, ' ');
	if (!path)
	    throw basic_exception("invalid path: %s", buf);
	path++;
	
	char *epath = strchr(path, ' ');
	if (!epath)
	    throw basic_exception("invalid path: %s", buf);
	*epath = 0;
	
	if (client_debug)
	    cprintf("path: %s", path);

	the_get->process_request(f, path);
    } catch (basic_exception &e) {
	snprintf(buf, sizeof(buf),
		 "HTTP/1.0 400 Bad Request\r\n"
		 "Content-Type: text/html\r\n"
		 "\r\n"
		 "<h1>Error processing request</h1>\r\n"
		 "%s\r\n", e.what());
	try {
	    lwrite_all(f, buf, strlen(buf));
	} catch (basic_exception &e2) {
	    cprintf("%s\n", e2.what());
	}
    }

    lclose(f);
}

void __attribute__((noreturn))
jhttpd(uint16_t port, httpd_get *get, void (*cb)(void))
{
    the_get = get;

    int s = lsocket(PF_INET, SOCK_STREAM, 0);
    if (s < 0) 
	panic("unable to create socket: %s", strerror(errno));
    
#if LINUX
    int on = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
	panic("unable to setsockopt: %s", strerror(errno));
#endif

    struct sockaddr_in sa;
    bzero(&sa, sizeof(sa));;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (lbind(s, (struct sockaddr*) &sa, sizeof(sa)) < 0) 
	panic("unable to bind: %s", strerror(errno));

    if (llisten(s, 5) < 0) 
	panic("unable to listen: %s", strerror(errno));
    
    cprintf("jhttpd: accepting on port %u\n", port);

    if (cb)
	cb();

    if (profile)
	sys_debug(kdebug_prof, kprof_enable, 0, 0, 0, 0, 0);

    for (uint64_t i = 0; ; i++) {
	int t = laccept(s, 0, 0);
	if (t < 0) {
	    cprintf("jhttpd: accept failed %s\n", strerror(errno));
	    continue;
	}

	int r = thread_create(0, "http-client", http_client, t);
	if (r < 0) {
	    cprintf("httpd: thread_create failed: %s\n", e2s(r));
	    lclose(t);
	}

	if (profile && i % 1000 == 0)
	    sys_debug(kdebug_prof, kprof_print, 0, 0, 0, 0, 0);	    
    }
}

#if LINUX
int
main(int ac, char **av)
{
    if (ac < 2) {
	fprintf(stderr, "usage: %s port\n", av[0]);
	return -1;
    }

    jhttpd(av[1]);
    return 0;
}
#endif
