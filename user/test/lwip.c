#include <test.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/jnic.h>
#include <inc/prof.h>

#include <lwipinc/lsocket.h>
#include <lwipinc/lwipinit.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <arch/sys_arch.h>

#include <string.h>
#include <errno.h>

enum { port = 9999 };
enum { irq_affinity = 1};

struct jnic the_nic;
uint64_t lwip_ready;

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

void __attribute__((noreturn))
lwip_sock_test(void)
{
    find_nic();

    cyg_profile_init();

    time_init(100);
    echeck(thread_create(0, "lwip-thread", lwip_thread, 0));
    thread_wait(&lwip_ready, 0, UINT64(~0));
        
    int s = lsocket(PF_INET, SOCK_STREAM, 0);
    if (s < 0) 
	panic("unable to create socket: %s", strerror(errno));
    
    struct sockaddr_in sa;
    bzero(&sa, sizeof(sa));;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (lbind(s, (struct sockaddr*) &sa, sizeof(sa)) < 0) 
	panic("unable to bind: %s", strerror(errno));
    
    if (llisten(s, 5) < 0) 
	panic("unable to listen: %s", strerror(errno));

    cyg_profile_reset();

    cprintf("lwip_sock_test: accept/close loop..\n");
    for (uint64_t i = 0; ; i++) {
	int t = laccept(s, 0, 0);
	if (t < 0) {
	    cprintf("lwip_sock_test: accept failed %s\n", strerror(errno));
	    continue;
	}
	lclose(t);

	if ((i % 101) == 0)
	    cyg_profile_print();
    }
}
