extern "C" {
#include <machine/x86.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/jnic.h>
#include <inc/cpuman.h>

#include <lwipinc/lwipinit.h>

#include <lwip.h>
}
#include <inc/jmonitor.hh>
#include <inc/error.hh>

#include <jhttpd.hh>
#include <app_shared.hh>

#include <stdio.h>

enum { msra_network = 0 };

// These options imply:
//  One system monitor manages num_stacks/num_monitors NICs.
//  One httpd server uses num_apps/num_stacks application cores.
enum { num_stacks = 8 };	// this many net. stack cores
enum { num_monitors = 1 };	// this many monitor cores
enum { num_apps = 8 };		// this many of app cores
enum { monitor_nics = 0 };	// use a system monitor?
enum { int_hz = 100 };		// timer hz for lwip

enum { app_test = 0 };

// db knobs are in app_shared.hh

// filesum app knobs
#if 1
// set mutlisum_hack 1
enum { filesum_files = 8 };
enum { filesum_fsize = 4 * 1024 * 1024 };
#endif

#if 0
// set multisum_hack 8
enum { filesum_files = 8 };
enum { filesum_fsize = 1 * 1024 * 1024 };
#endif

#if 0
// set multisum_hack to 16
enum { filesum_files = 15 };
enum { filesum_fsize = 1 * 1024 * 1024 };
#endif

enum { def_key_limit = 1000 };
enum { port = 8000 };

enum { debug_core_alloc = 1 };	//debug core allocation

app_type_t the_app_type = filesum_app;

static struct {
    uint32_t ip[16];
    int nip;
    volatile uint64_t httpd_ready;
}  *shared_state;

struct sobj_ref nic_share;
struct jnic jnic[16];
uint32_t jnic_num;
uint64_t lwip_ready;

struct sobj_ref * ummap_shref; 

jmonitor monitors[num_monitors];

static httpd_filesum *the_summer;
static httpd_db_select *the_selector;
static httpd_db_join *the_joiner;

struct cpu_state *g_cs = NULL;

// In network-byte order
static struct {
    uint32_t ip;
    uint32_t nm;
    uint32_t gw;
} static_ips[] = {
    { 0x1B4017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x1C4017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x1D4017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x1E4017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x1F4017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x204017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x214017AC, 0x00F8FFFF, 0x014017AC }, 
    { 0x224017AC, 0x00F8FFFF, 0x014017AC }
};

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
		cprintf("alloc_sysmon sys_device_alloc\n");
	    error_check(r = sys_device_alloc(sh, udl.dev[i].id, core_env->pid));
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

static void __attribute__ ((noreturn))
    lwip_thread(uint64_t x)
{
    if (msra_network)
	lib_lwip_init(lwip_cb, 0, &jnic[x], static_ips[x].ip,
		      static_ips[x].nm, static_ips[x].gw);
    else
	lib_lwip_init(lwip_cb, 0, &jnic[x], 0, 0, 0);
}

static void
jhttpd_cb(void)
{
    shared_state->httpd_ready = 1;
}

static const char *
ip_to_string(uint32_t ip)
{
    static char buf[32];
    sprintf(&buf[0], "%u.%u.%u.%u",
	    (ip & 0xFF000000) >> 24,
	    (ip & 0x00FF0000) >> 16,
	    (ip & 0x0000FF00) >> 8, (ip & 0x000000FF));
    return &buf[0];
}

static void __attribute__ ((noreturn))
bootstrap_httpd(uint32_t nic, httpd_get * get)
{
    cprintf("* Init LWIP with interval timer @ %u Hz...\n", int_hz);
    time_init(int_hz);
    int r = thread_create(0, "lwip-bootstrap-thread",
			  lwip_thread, (uint64_t) nic);

    if (r < 0)
	panic("Could not start lwip thread: %s", e2s(r));

    thread_wait(&lwip_ready, 0, UINT64(~0));
    cprintf("* Init LWIP done!\n");

    cprintf("* IPs:\n");
    for (int i = 0; i < shared_state->nip; i++)
	cprintf(" %s\n", ip_to_string(shared_state->ip[i]));

    jhttpd(port, get, &jhttpd_cb);
    panic("jhttpd returned");
}

static httpd_get *
getapp(int app_index)
{
    httpd_get *get = 0;
    switch (the_app_type) {
    case filesum_app:
	assert(the_summer);
	get = new httpd_filesum_get(the_summer);
	break;
    case db_select_app:
	assert(the_selector);
	get = new httpd_db_select_get(the_selector);
	break;
	case db_join_app:
	assert(the_joiner);
	get = new httpd_db_join_get(the_joiner);
	break;
    default:
	panic("bad app: %d\n", the_app_type);
    }
    
    return get;
}

int
main(void)
{
    // Allocate a shared segment so we get nice cprintfs during init
    int64_t r = segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			      (void **) &shared_state, SEGMAP_SHARED,
			      "httpd-shared-seg", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));
    shared_state->httpd_ready = 0;

    r = segment_alloc(core_env->sh, sizeof(*g_cs), 0,
		      (void **) &g_cs, SEGMAP_SHARED,
		      "httpd-shared-cpu-state", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));
    cpu_man_init(g_cs);

    if (app_test)
	app_tester();

    struct cpu_state cs[num_monitors];
    if (monitor_nics) {
	int stack_per_mon = num_stacks / num_monitors;
	for (int i = 0; i < num_monitors - 1; i++)
	    cpu_man_group(g_cs, &cs[i], stack_per_mon + 1);
	//create cpu group containing core_env->pid
	cpu_man_group_nearby(g_cs, &cs[num_monitors - 1], stack_per_mon, core_env->pid);
	if (debug_core_alloc)
	    for (int i = 0; i < num_monitors; i++)
	        cpu_man_print(&cs[i]);
    } 
    struct sobj_ref nic_share;
    int64_t n = jnic_alloc_all(jnic, 16, &nic_share);
    if (n < 0)
	panic("jnic_alloc_all failed: %s\n", e2s(n));

    if (n < num_stacks) {
	cprintf("net_test: %ld is not enough nics, need %u\n", n, num_stacks);
	return 1;
    }
    cprintf("httpd: found %ld nics\n", n);

    if (monitor_nics) {
	int stack_per_mon = num_stacks / num_monitors;
	for (int i = 0; i < num_monitors; i++) {
	    //monitor i monitors mper_stack network stacks starting from i*mper_stacks
	    for (int k = 0; k < stack_per_mon; k++) {
		struct jnic *jn = &jnic[(i * stack_per_mon) + k];
		struct sobj_ref sm_ref = alloc_sysmon(nic_share.object);
		jn->jsm_ref = sm_ref;
		monitors[i].add_device(jn->nic_ref);
		monitors[i].add_device(sm_ref);
	    }
	    monitors[i].start(cpu_man_any(&cs[i]));
	}
    }

    proc_id_t pids[num_apps];
    for (int i = 0; i < num_apps; i++)
	pids[i] = cpu_man_any(g_cs);

    if (the_app_type == filesum_app) {
	cprintf("creating the_summer\n");
	the_summer = new httpd_filesum(pids, num_apps, filesum_fsize,
				       filesum_files);
    } else if (the_app_type == db_select_app) {
	cprintf("creating the_selector\n");
	the_selector = new httpd_db_select(pids, num_apps, dbsel_num_rows, 
					   db_pad_length, db_max_c2_val);
    } else if (the_app_type == db_join_app) {
	cprintf("creating the_joiner\n");
	the_joiner = new httpd_db_join(pids, num_apps);
    }

    for (int i = 0; i < num_stacks - 1; i++) {
	if (!monitor_nics) {
	    struct u_device_conf udc;
	    udc.type = device_conf_irq;
	    udc.irq.irq_pid = core_env->pid;
	    udc.irq.enable = 1;
	    error_check(sys_device_conf(jnic[i].nic_ref, &udc));
	}

	proc_id_t pid;
	if (monitor_nics)
	    pid = cpu_man_any(&cs[i / (num_stacks / num_monitors)]);
	else
	    pid = cpu_man_any(g_cs);

#if 0
	struct sobj_ref shares[2];
	shares[0] = *ummap_shref;
	shares[1] = nic_share;

	error_check(r = pforkv(pid, PFORK_SHARE_HEAP, shares, 2));
#endif

	error_check(r = pforkv(pid, 0, &nic_share, 1));

	//child process using i'th network stack runns on pid
	if (r == 0)
	    bootstrap_httpd(i, getapp(i));
	while (!shared_state->httpd_ready) ;
	shared_state->httpd_ready = 0;
    }

    if (!monitor_nics) {
	struct u_device_conf udc;
	udc.type = device_conf_irq;
	udc.irq.irq_pid = core_env->pid;
	udc.irq.enable = 1;
	error_check(sys_device_conf(jnic[num_stacks - 1].nic_ref, &udc));
    }
    bootstrap_httpd(num_stacks - 1, getapp(num_stacks - 1));
}
