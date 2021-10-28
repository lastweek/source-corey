extern "C" {
#include <machine/x86.h>
#include <test.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/jnic.h>
#include <inc/prof.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <lwipinc/lwipinit.h>
#include <fcntl.h>
#include "rawsock_perf_test.h"

#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
}

#include <inc/error.hh>
#include <inc/errno.hh>

static struct jnic the_nic;
static uint64_t lwip_ready;

static void
find_nic(void)
{
    struct u_device_list udl;
    echeck(sys_device_list(&udl));

    for (uint64_t i = 0; i < udl.ndev ; i++) {
	if (udl.dev[i].type == device_nic) {
	    int64_t dv;
	    dv = sys_device_alloc(core_env->sh, udl.dev[i].id, core_env->pid);
	    if (dv < 0)
		panic("cannot allocate device: %s", e2s(dv));

	    struct u_device_conf udc;
	    udc.type = device_conf_irq;
	    udc.irq.irq_pid = irq_affinity;
	    udc.irq.enable = 1;
	    int r = sys_device_conf(SOBJ(core_env->sh, dv), &udc);
	    if (r < 0)
		panic("unable to configure nic: %s", e2s(r));

	    r = jnic_real_setup(&the_nic, SOBJ(core_env->sh, dv));
	    if (r < 0)
		panic("unable to init jnic: %s", e2s(r));
	    return;
	}
    }
    panic("no nics detected");
}

static void
lwip_cb(uint32_t ip, void *x)
{
    thread_wakeup(&lwip_ready);
}

static void __attribute__((noreturn))
lwip_thread(uint64_t x)
{
    lib_lwip_init(lwip_cb, 0, &the_nic, 0, 0, 0);
}

void
lwip_init(void)
{
    find_nic();
    time_init(100);
    echeck(thread_create(0, "lwip-thread", lwip_thread, 0));
    thread_wait(&lwip_ready, 0, UINT64(~0));
}

#define xsocket		socket
#define xbind 		bind
#define xlisten		listen
#define xconnect	connect
#define xaccept		accept
#define xread		read
#define xwrite		write
#define xclose		close

DEF_TEST_SOCKET_CLOSE(socket, close)
DEF_TEST_BIND_LISTEN(bind, listen)
DEF_TEST_CONNECT(connect)
DEF_TEST_AS_SERVER(accept, read, write)

void __attribute__((noreturn))
sock_fd_perf_test(void)
{
    lwip_init();
    test_socket_close();
    test_raw_socket_close();
    test_bind_listen();
    test_raw_bind_listen();

    test_connect();
    test_as_server();
    test_raw_connect();
    test_raw_as_server();
    while (1);
}
