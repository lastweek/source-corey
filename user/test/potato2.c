#include <inc/types.h>
#include <inc/lib.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/assert.h>
#include <machine/mmu-x86.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <test.h>

static char *page_buf;
enum { ncons_buf = 64 };
static char cons_buf[ncons_buf];
enum { ccore = 1, cuse_thread };
enum { nmaxcores = 16 };
static int ncores;

static struct {
    uint64_t start;
    union {
	struct {
	    volatile int signal;
	};
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU] __attribute__ ((aligned(JOS_CLINE)));
}  *sync_state;

static inline void
nop_pause()
{
    __asm__ __volatile("pause"::);
}
static inline uint64_t
read_tsc(void)
{
    uint32_t a, d;
    __asm __volatile("rdtsc":"=a"(a), "=d"(d));
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}

/*static int __attribute__((unused))
get_npfs(void)
{
    char fn[128];

    snprintf(fn, sizeof(fn), "/proc/%u/stat", getpid());
    FILE *proc = fopen(fn, "r");
    assert(proc);

    char buf[4096];
    size_t r = fread(buf, 1, sizeof(buf) - 1, proc);
    assert(r > 0);
    buf[r] = 0;

    char *s = buf;
    for (int k = 0; k < 9; k++) {
 s = strchr(s, ' ');
 s++;
    }

    return atoi(s);
}*/

static void
potato_test(uint64_t core)
{
    if (core != 0)
	while (sync_state->cpu[core].signal == 0)
	    nop_pause();
    else
	sync_state->start = read_tsc();
    page_buf[0] = core;
    sync_state->cpu[(core + 1) % ncores].signal = 1;
}

static int
do_test(int use_threads)
{
    void *buf = 0;
    int r =
	segment_alloc(core_env->sh, 2 * PGSIZE, 0, &buf, SEGMAP_SHARED,
		      "pagepass buf", core_env->pid);

    if (r < 0)
	panic("segment_alloc failed");

    sync_state = buf;
    page_buf = buf + PGSIZE;

    memset(sync_state, 0, sizeof(*sync_state));

    for (int64_t i = 1; i < ncores; i++) {
	if (use_threads) {
	    thread_id_t tid;
	    assert(thread_create
		   (&tid, "page pass working core", potato_test, i) == 0);
	} else {
	    pid_t p = pfork(i);
	    assert(p >= 0);
	    if (p == 0) {
		potato_test((uint64_t) i);
		//printf("(%u) %d\n", getpid(), get_npfs());
		processor_halt();
	    }
	}
    }

    uint64_t x = read_tsc();
    while (read_tsc() - x < 1000000000) ;

    cprintf("buf %p, use_pthread %d, ncores %d, usec ", buf, use_threads, ncores);

    potato_test(0);
    while (sync_state->cpu[0].signal == 0) ;
    uint64_t end = read_tsc();

    uint64_t usec = (end - sync_state->start) * 1000000 / core_env->cpufreq;
    cprintf("%lu\n", usec);

    //printf("(%u) %d\n", getpid(), get_npfs());
    return 0;

}

static void
pagepass_cons(void)
{
    struct u_device_list udl;
    echeck(sys_device_list(&udl));

    uint64_t kbd_id = UINT64(~0);
    for (uint64_t i = 0; i < udl.ndev; i++) {
	if (udl.dev[i].type == device_cons) {
	    kbd_id = udl.dev[i].id;
	    break;
	}
    }

    if (kbd_id == UINT64(~0)) {
	cprintf("console_test: no console found\n");
	return;
    }

    int64_t r;
    echeck((r = sys_device_alloc(core_env->sh, kbd_id, core_env->pid)));

    struct sobj_ref sg;
    struct cons_entry *ce = 0;
    echeck(segment_alloc(core_env->sh, PGSIZE, &sg, (void **) &ce,
			 0, "console-seg", core_env->pid));
    memset(ce, 0, PGSIZE);
    echeck(sys_device_buf(SOBJ(core_env->sh, r), sg, 0, devio_in));

    uint32_t j = 0, npara = ccore;
    uint32_t use_thread = 0;
    for (uint32_t i = 0;; i = (i + 1) % (PGSIZE / sizeof(*ce))) {
	if (j == 0) {
	    if (npara == ccore)
		cprintf("Enter number of cores[1-15]:");
	    else
		cprintf("Use threads[1 for yes, 0 for no]:");
	    cflush();
	}
	while (!ce[i].status) ;

	cprintf("%c", ce[i].code);
	cflush();
	if (ce[i].code == '\r')
	    cprintf("\n");

	if (ce[i].code == '\r' || ce[i].code == '\n') {
	    if (npara == ccore) {
		ncores = atoi(cons_buf);
		if (!ncores || ncores > 15) {
		    cprintf("Invalid ncores %u\n", ncores);
		    cflush();
		} else
		    npara++;
		cons_buf[0] = 0;
	    } else if (npara == cuse_thread) {
		use_thread = atoi(cons_buf);
		do_test(use_thread);
		npara = ccore;
		cons_buf[0] = 0;
	    } 
	    j = 0;
	}
	else {
	    cons_buf[j++] = ce[i].code;
	    cons_buf[j] = 0;
	    if (j >= ncons_buf)
	        j = 0;
	} 
    }
}

void
pagepass_test(void);
void
pagepass_test(void)
{
    pagepass_cons();
}
