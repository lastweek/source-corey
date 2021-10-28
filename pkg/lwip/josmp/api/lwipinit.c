#include <inc/lib.h>
#include <inc/types.h>
#include <lwipinc/lwipinit.h>

#include <lwip/sockets.h>
#include <lwip/netif.h>
#include <lwip/stats.h>
#include <lwip/sys.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/dhcp.h>
#include <lwip/tcpip.h>
#include <lwip/stats.h>
#include <lwip/netbuf.h>
#include <netif/etharp.h>

#include <jif/jif.h>

#include <stdio.h>

// various netd initialization and threads
static int netd_stats = 0;

struct timer_thread {
    uint64_t nsec;
    void (*func)(void);
    const char *name;
};

static void
lwip_init(struct netif *nif, void *if_state,
	  uint32_t init_addr, uint32_t init_mask, uint32_t init_gw)
{
    struct ip_addr ipaddr, netmask, gateway;
    ipaddr.addr  = init_addr;
    netmask.addr = init_mask;
    gateway.addr = init_gw;

    if (0 == netif_add(nif, &ipaddr, &netmask, &gateway,
		       if_state,
		       jif_init,
		       ip_input))
	panic("lwip_init: error in netif_add\n");

    netif_set_default(nif);
    netif_set_up(nif);
}

static void __attribute__((noreturn))
net_receive(uint64_t arg)
{
    struct netif *nif = (struct netif *) arg;

    for (;;) {
	lwip_core_lock();
	jif_input(nif);
	lwip_core_unlock();
	thread_yield();
    }
}

static void __attribute__((noreturn))
net_timer(uint64_t arg)
{
    struct timer_thread *t = (struct timer_thread *) arg;

    for (;;) {
	uint64_t cur = time_nsec();

	lwip_core_lock();
	t->func();
	lwip_core_unlock();

	thread_wait(0, 0, cur + t->nsec);
    }
}

static void
start_timer(struct timer_thread *t, void (*func)(void), const char *name, int msec)
{
    t->nsec = NSEC_PER_SECOND / 1000 * msec;
    t->func = func;
    t->name = name;
    int r = thread_create(0, name, &net_timer, (uint64_t)t);
    if (r < 0)
	panic("cannot create timer thread: %s", e2s(r));
}

static void
tcpip_init_done(void *arg)
{
    uint64_t *done = arg;
    *done = 1;
    thread_wakeup(done);
}

static const char *
ip_to_string(uint32_t ip)
{
    static char buf[32];
    sprintf(&buf[0], "%u.%u.%u.%u",
		     (ip & 0xFF000000) >> 24,
		     (ip & 0x00FF0000) >> 16,
		     (ip & 0x0000FF00) >> 8,
		     (ip & 0x000000FF));
    return &buf[0];
}

void
lib_lwip_init(void (*cb)(uint32_t, void *), void *cbarg, struct jnic *jnic,
	      uint32_t ipaddr, uint32_t netmask, uint32_t gw)
{
    lwip_core_lock();

    struct netif *nif = 0;
    int r = segment_alloc(core_env->sh, sizeof(*nif), 0, 
			  (void **)&nif, 0, "lwip-nif", core_env->pid);
    if (r < 0)
	panic("unable to allocate nif: %s", e2s(r));

    uint64_t done = 0;
    tcpip_init(&tcpip_init_done, &done);
    lwip_core_unlock();
    thread_wait(&done, 0, UINT64(~0));
    lwip_core_lock();
   
    lwip_init(nif, (void *)jnic, ipaddr, netmask, gw);

    if (ipaddr == 0)
	dhcp_start(nif);

    r = thread_create(0, "lwip-rx-thread", &net_receive, (uint64_t)nif);
    if (r < 0)
	panic("cannot create receiver thread: %s", e2s(r));
    
    struct timer_thread t_arp, t_tcpf, t_tcps, t_dhcpf, t_dhcpc;
    
    start_timer(&t_arp, &etharp_tmr, "arp timer", ARP_TMR_INTERVAL);
    
    start_timer(&t_tcpf, &tcp_fasttmr, "tcp f timer", TCP_FAST_INTERVAL);
    start_timer(&t_tcps, &tcp_slowtmr, "tcp s timer", TCP_SLOW_INTERVAL);
    
    if (ipaddr == 0) {
	start_timer(&t_dhcpf, &dhcp_fine_tmr,	"dhcp f timer",	DHCP_FINE_TIMER_MSECS);
	start_timer(&t_dhcpc, &dhcp_coarse_tmr,	"dhcp c timer",	DHCP_COARSE_TIMER_SECS * 1000);
    } else {
	cprintf("netd: %02x:%02x:%02x:%02x:%02x:%02x" 
		" bound to static %s\n", 
		nif->hwaddr[0],	nif->hwaddr[1], nif->hwaddr[2],
		nif->hwaddr[3], nif->hwaddr[4], nif->hwaddr[5],
		ip_to_string(ntohl(ipaddr)));
	cb(ntohl(ipaddr), cbarg);
    }
    
    int dhcp_state = 0;
    const char *dhcp_states[] = {
	[DHCP_RENEWING] "renewing",
	[DHCP_SELECTING] "selecting",
	[DHCP_CHECKING] "checking",
	[DHCP_BOUND] "bound",
    };

    for (;;) {
	if (ipaddr == 0 && dhcp_state != nif->dhcp->state) {
	    dhcp_state = nif->dhcp->state;
	    cprintf("netd: DHCP state %d (%s)\n", dhcp_state,
		    dhcp_states[dhcp_state] ? : "unknown");

	    if (dhcp_state == DHCP_BOUND) {
		uint32_t ip = ntohl(nif->ip_addr.addr);
		cprintf("netd: %02x:%02x:%02x:%02x:%02x:%02x" 
			" bound to %s\n", 
			nif->hwaddr[0],	nif->hwaddr[1], nif->hwaddr[2],
			nif->hwaddr[3], nif->hwaddr[4], nif->hwaddr[5],
			ip_to_string(ip));
		cb(ip, cbarg);
	    }
	}

	if (netd_stats) {
	    stats_display();
	}

	lwip_core_unlock();
	thread_wait(0, 0, time_nsec() + NSEC_PER_SECOND);
	lwip_core_lock();
    }
}
