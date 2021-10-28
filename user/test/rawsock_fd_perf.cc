extern "C" {
#include <machine/x86.h>
#include <test.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/jnic.h>
#include <inc/prof.h>
#include <lwipinc/lwipinit.h>
#include <lwipinc/lsocket.h>
#include <unistd.h>
#include "rawsock_perf_test.h"
}

#include <inc/error.hh>
#include <inc/errno.hh>

#define xsocket		lsocket
#define xbind 		lbind
#define xlisten		llisten
#define xconnect	lconnect
#define xaccept		laccept
#define xread		lread
#define xwrite		lwrite
#define xclose		lclose

DEF_TEST_SOCKET_CLOSE(lsocket, lclose)
DEF_TEST_BIND_LISTEN(lbind, llisten)
DEF_TEST_CONNECT(lconnect)
DEF_TEST_AS_SERVER(laccept, lread, lwrite)

void 
test_raw_socket_close()
{
    test_socket_close();
}

void 
test_raw_bind_listen()
{
    test_bind_listen();
}

void 
test_raw_connect()
{
    test_connect();
}

void 
test_raw_as_server()
{
    test_as_server();
}
