/*
 * XXX on josmp Intel, make sure sysmonitors and stacks are on same die
 * XXX figure out if we should process more packets on a poll in sysmon
 * XXX any reason to create a new thread per incoming connection in josmp?
 */

#ifndef JOS_TEST
#define JOSMP 1
#define LINUX 0
#else
#define JOSMP 0
#define LINUX 1
#endif

// Linux+josmp common includes
#include <inc/scopeguard.hh>
#include <stdint.h>

enum { send_bytes = 128 }; // server response size
enum { port_base = 8000 };

enum { do_profile = 1 };
enum { do_oprofile = 0 };
enum { profile_thresh = 10000 };

char send_buf[send_bytes];

static void do_net_test(uint32_t core, uint32_t s_addr, uint16_t port);
static void net_conn(int s);

#if JOSMP
extern "C" {
#include <test.h>
#include <inc/jnic.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/sysprof.h>

#include <lwipinc/lwipinit.h>
#include <lwip/sockets.h>
#include <lwipinc/lsocket.h>
#include <inc/cpuman.h>
#include <inc/pad.h>
#include <inc/arch.h>
}

#include <inc/error.hh>
#include <inc/jmonitor.hh>
#include <errno.h>
#include <stdio.h>

enum { num_stacks = 15 };
enum { monitor_nics = 1 };
enum { sysmon_nics = 1 };
enum { num_monitors = 1 };
enum { int_hz = 100 };	   // timer hz for lwip
enum { msra_network = 0 };
enum { debug_core_alloc = 0 };

struct cpu_state *g_cs = NULL;

// In network-byte order
static struct {
    uint32_t ip;
    uint32_t nm;
    uint32_t gw;
} static_ips[] = { 
    {0x1B4017AC, 0x00F8FFFF, 0x014017AC},
    {0x1C4017AC, 0x00F8FFFF, 0x014017AC},
    {0x1D4017AC, 0x00F8FFFF, 0x014017AC},
    {0x1E4017AC, 0x00F8FFFF, 0x014017AC},
    {0x1F4017AC, 0x00F8FFFF, 0x014017AC},
    {0x204017AC, 0x00F8FFFF, 0x014017AC},
    {0x214017AC, 0x00F8FFFF, 0x014017AC},
    {0x224017AC, 0x00F8FFFF, 0x014017AC},
};

struct jnic jnic[16];

jmonitor mon[num_monitors];

static uint64_t lwip_ready;

static struct {
    uint32_t ip[16];
    volatile int nip;
    volatile uint64_t httpd_ready;

    PAD(volatile uint64_t) l3miss[JOS_NCPU];
    PAD(volatile uint64_t) conns[JOS_NCPU];

} *shared_state;

struct sobj_ref
alloc_sysmon(uint64_t sh)
{
    static int last_i = -1;
    static struct u_device_list udl;
    
    if (last_i == -1)
	error_check(sys_device_list(&udl));

    for (uint32_t i = last_i + 1; i < udl.ndev; i++) {
	last_i = i;
	if (udl.dev[i].type == device_sysmon) {
	    int64_t r;
	    error_check(r = sys_device_alloc(sh, 
					     udl.dev[i].id, 
					     core_env->pid));
	    return SOBJ(sh, r);
	}
    }
    
    panic("No more sysmons");
}

static void
lwip_cb(uint32_t ip, void *x)
{
    shared_state->ip[shared_state->nip++] = ip;
    lwip_ready = 1;
    thread_wakeup(&lwip_ready);
}

static void __attribute__((noreturn))
lwip_thread(uint64_t x)
{
    if (msra_network)
	lib_lwip_init(lwip_cb, 0, &jnic[x], static_ips[x].ip, 
		      static_ips[x].nm, static_ips[x].gw);
    else
	lib_lwip_init(lwip_cb, 0, &jnic[x], 0, 0, 0);
}

static void
setup_lwip(uint64_t index)
{
    cprintf("* Init LWIP with interval timer @ %u Hz...\n", int_hz);
    time_init(int_hz);

    int r = thread_create(0, "lwip-bootstrap-thread", 
			  lwip_thread, index);

    if (r < 0)
	panic("Could not start lwip thread: %s", e2s(r));

    thread_wait(&lwip_ready, 0, UINT64(~0));
    cprintf("* Init LWIP done!\n");
}

void
net_test(void)
{
    static_assert((num_stacks % num_monitors) == 0);
    // Allocate a shared segment so we get nice cprintfs during init
    error_check(segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			      (void **)&shared_state, SEGMAP_SHARED, 
			      "shared-seg", core_env->pid));

    cprintf("net_test: %u-byte responses\n", send_bytes);

    int64_t r = segment_alloc(core_env->sh, sizeof(*g_cs), 0,
		      (void **) &g_cs, SEGMAP_SHARED,
		      "httpd-shared-cpu-state", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));
    cpu_man_init(g_cs);

    struct cpu_state cs[num_monitors];
    if (monitor_nics) {
	int stack_per_mon = num_stacks / num_monitors;
	//create the first group nearby core_env->pid
	cpu_man_group_nearby(g_cs, &cs[0], stack_per_mon, core_env->pid);
	for (int i = 1; i < num_monitors; i++)
	    cpu_man_group(g_cs, &cs[i], stack_per_mon + 1);
	if (debug_core_alloc)
	    for (int i = 0; i < num_monitors; i++)
	        cpu_man_print(&cs[i]);
    }
    struct sobj_ref nic_share;
    int64_t n = jnic_alloc_all(jnic, 16, &nic_share);
    if (n < 0)
	panic("jnic_alloc_all failed: %s\n", e2s(n));
    
    if (n < num_stacks) {
	cprintf("net_test: %ld is not enough nics, need %u\n",
		n, num_stacks);
	return;
    }
    
    cprintf("net_test: found %ld nics\n", n);

    if (monitor_nics) {
	int stack_per_mon = num_stacks / num_monitors;
	for (int i = 0; i < num_monitors; i++) {
	    for (int k = 0; k < stack_per_mon; k++) {
		struct jnic *jn = &jnic[(i * stack_per_mon) + k];
		mon[i].add_device(jn->nic_ref);
		if (sysmon_nics) {
		    struct sobj_ref sm_ref = alloc_sysmon(nic_share.object);
		    jn->jsm_ref = sm_ref;
		    mon[i].add_device(sm_ref);
		}
	    }
	    mon[i].start(cpu_man_any(&cs[i]));
	}
    }

    for (uint64_t i = 1; i < num_stacks; i++) {
	if (!monitor_nics) {
	    struct u_device_conf udc;
	    udc.type = device_conf_irq;
	    udc.irq.irq_pid = core_env->pid;
	    udc.irq.enable = 1;
	    error_check(sys_device_conf(jnic[i].nic_ref, &udc));
	}
	proc_id_t pid;
	if (monitor_nics)
	    pid = cpu_man_any(&cs[i /(num_stacks / num_monitors)]);
	else
	    pid = cpu_man_any(g_cs);
	error_check(r = pforkv(pid, 0, &nic_share, 1));
	if (r == 0) {
	    setup_lwip(i);
	    do_net_test(pid, htonl(INADDR_ANY), htons(port_base));
	    processor_halt();
	}
    }

    if (!monitor_nics) {
	struct u_device_conf udc;
	udc.type = device_conf_irq;
	udc.irq.irq_pid = 0;
	udc.irq.enable = 1;
	error_check(sys_device_conf(jnic[0].nic_ref, &udc));
    }
    setup_lwip(0);
    do_net_test(0, htonl(INADDR_ANY), htons(port_base));
}

static void
net_conn_helper(uint64_t t)
{
    try {
	net_conn((int)t);
    } catch (std::exception &e) {
	printf("net_conn error: %s\n", e.what());
    }
}

static int
create_net_conn(int t)
{
    return thread_create(0, "net-conn", net_conn_helper, t);
}

#define xpanic panic
#define xsocket lsocket
#define xbind lbind
#define xlisten llisten
#define xaccept laccept
#define xclose lclose
#define xwrite lwrite

#endif

#if LINUX

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <ifaddrs.h>
#include <sched.h>

#include <test/pad.h>
extern "C" {
#include <test/pmc.h>
}

#define JOS_NCPU 16

static struct shared_state {
    PAD(volatile uint64_t) l3miss[JOS_NCPU];
    PAD(volatile uint64_t) conns[JOS_NCPU];
} *shared_state;


#define xpanic(fmt, args...)					\
	do {							\
		fprintf(stderr, "panic: %s:%u " fmt "\n",	\
			__FILE__, __LINE__, ##args);		\
		exit(-1);					\
	} while (0)
#define xsocket socket
#define xbind bind
#define xlisten listen
#define xaccept accept
#define xclose close
#define xwrite write

#define OPROFILE_START "/home/sbw/oprofile.start"
#define OPROFILE_STOP  "/home/sbw/oprofile.stop"

static void *
net_conn_helper(void *a)
{
    try {
	int t = (int)(intptr_t)a;
	net_conn((int)t);
    } catch (std::exception &e) {
	printf("net_conn error: %s\n", e.what());
    }
}

static int
create_net_conn(int t)
{
    net_conn_helper((void *)t);
    return 0;
}

int
main(int ac, char **av)
{
    int num_cores = sysconf(_SC_NPROCESSORS_CONF);
    uint32_t ips[64];
    int num_ips = 0;

    void *buf;
    buf = mmap(0, sizeof(*shared_state), 
	       PROT_READ | PROT_WRITE, 
	       MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    assert(buf);
    shared_state = (struct shared_state *)buf;
    memset(shared_state, 0, sizeof(*shared_state));
    
    struct ifaddrs *ifa = NULL, *ifp = NULL;
    if (getifaddrs (&ifp) < 0) {
	perror("getifaddrs");
	return -1;
    }

    for (ifa = ifp; ifa && num_ips < num_cores; ifa = ifa->ifa_next) {
	if(!ifa->ifa_addr)
	    continue;

	if (ifa->ifa_addr->sa_family != AF_INET)
	    continue;
	if ((ifa->ifa_flags & IFF_UP) == 0 ||
	    (ifa->ifa_flags & IFF_LOOPBACK) ||
	    (ifa->ifa_flags & (IFF_BROADCAST | IFF_POINTOPOINT)) == 0) 
	{
	    continue;
	}
	
	uint32_t ip = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
	ips[num_ips] = ip;
	num_ips++;
    }

    freeifaddrs (ifp);

    for (int i = 1; i < num_cores; i++) {
	pid_t p = fork();
	if (p < 0)
	    xpanic("fork failed: %s", strerror(errno));
	if (p == 0) {
	    assert(affinity_set(i) == 0);
	    uint32_t port = port_base + i / num_ips;
	    uint32_t ip = ips[i % num_ips];
	    printf("%u starting %u.%u.%u.%u:%u\n",
		   i, 
		   (ip & 0x000000FF), 
		   (ip & 0x0000FF00) >> 8,
		   (ip & 0x00FF0000) >> 16,
		   (ip & 0xFF000000) >> 24,
		   port);
	    do_net_test(i, ips[i % num_ips], htons(port));
	}
    }

    assert(affinity_set(0) == 0);
    uint32_t ip = ips[0];
    uint32_t port = port_base;
    printf("%u starting %u.%u.%u.%u:%u\n",
	   0, 
	   (ip & 0x000000FF), 
	   (ip & 0x0000FF00) >> 8,
	   (ip & 0x00FF0000) >> 16,
	   (ip & 0xFF000000) >> 24,
	   port);
    do_net_test(0, ip, htons(port));
    return 0;
}

#endif

static void 
net_conn(int s)
{
    scope_guard<int, int> close_sock(xclose, s);
    int count = send_bytes;
    char *b = send_buf;
    while (count) {
	int r = xwrite(s, b, count);
	if (r < 0)
	    throw basic_exception("write: %s", strerror(errno));
	if (r == 0)
	    throw basic_exception("write: unable to write");
	count -= r;
	b += r;
    }
}

static void __attribute__((noreturn))
do_net_test(uint32_t core, uint32_t s_addr, uint16_t port)
{
    uint64_t l3miss[JOS_NCPU];
    uint64_t conns[JOS_NCPU];

    int s = xsocket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
	xpanic("unable to create socket: %s", strerror(errno));

    memset(send_buf, 'A' + core, sizeof(send_buf));

    struct sockaddr_in sa;
    bzero(&sa, sizeof(sa));;
    sa.sin_family = AF_INET;
    sa.sin_port = port;
    sa.sin_addr.s_addr = s_addr;

#if LINUX
    int reuse = 1;
    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) 
	xpanic("unable to setsockopt: %s", strerror(errno));
#endif

    if (xbind(s, (struct sockaddr*) &sa, sizeof(sa)) < 0) 
	xpanic("unable to bind: %s", strerror(errno));

    if (xlisten(s, 20) < 0) 
	xpanic("unable to listen: %s", strerror(errno));

#if JOSMP
    if (core == 0) {
	while (shared_state->nip != num_stacks)
	    arch_pause();

	for (int i = 0; i < shared_state->nip; i++)
	    printf("ip %u.%u.%u.%u\n", 
		   (shared_state->ip[i] >> 24) & 0xFF,
		   (shared_state->ip[i] >> 16) & 0xFF,
		   (shared_state->ip[i] >> 8) & 0xFF,
		   shared_state->ip[i] & 0xFF);
    } else
	printf("%u accepting\n", core);
#endif
    

#if JOSMP
    sysprof_init();
    if (do_profile)
	sysprof_prog_l3miss(0);

    if (do_profile && core == 0) {
	// wait a little bit to make sure everyone is ready...
	time_delay_cycles(10000000);
	for (int i = 0; i < JOS_NCPU; i++)
	    l3miss[i] = shared_state->l3miss[i].v;
	for (int i = 0; i < JOS_NCPU; i++)
	    conns[i] = shared_state->conns[i].v;
    }

#endif    

#if LINUX
    if (do_profile)
	pmc_init(core);
#endif

    uint64_t long_sum = 0;
    for (uint64_t cnt = 0;; cnt++) {
	int t = xaccept(s, 0, 0);
	if (t < 0) {
	    printf("accept failed %s\n", strerror(errno));
	    continue;
	}

#if LINUX
	// start oprofile
	if (do_oprofile && core == 0 && cnt == 0)
	    system(OPROFILE_START);
#endif	

	int r = create_net_conn(t);
	if (r < 0) {
	    printf("thread_create failed: %s\n", strerror(errno));
	    xclose(t);
	}

	if (do_profile) {
#if JOSMP
	    shared_state->l3miss[core].v = sysprof_rdpmc(0);
#endif
#if LINUX
	    shared_state->l3miss[core].v = pmc_l3_miss();
#endif

	    shared_state->conns[core].v++;
	    if (core == 1 && ((cnt % 50000) == 0)) {
		uint64_t tconns = 0;
		uint64_t tl3miss = 0;
		for (int i = 0; i < JOS_NCPU; i++) {
		    tconns += shared_state->conns[i].v - conns[i];
		    conns[i] = shared_state->conns[i].v;
		    tl3miss += shared_state->l3miss[i].v - l3miss[i];
		    l3miss[i] = shared_state->l3miss[i].v;
		}

		uint64_t per = tconns ? tl3miss / tconns : 0;
		printf("tconns %ld, tl3miss %ld, tl3miss/tconns %ld\n", 
		       tconns, tl3miss, per);

		long_sum += per;
		if ((cnt % 250000) == 0) {
		    printf(" long miss/conns %ld\n", long_sum / 5);
		    long_sum = 0;
		}
	    }
	}

#if LINUX
	if (do_oprofile && cnt == profile_thresh)
	    system(OPROFILE_STOP);
#endif

    }
}
