#ifndef JOS_TEST
#define JOSMP 1
#define LINUX 0
#else
#define JOSMP 0
#define LINUX 1
#endif

enum { num_ops = 10000 };
enum { max_op = 1024 * 1024 };
enum { ncores = 4 };
enum { buf_size = 512 * 1024};

#if JOSMP
#include <machine/x86.h>
#include <machine/mmu.h>
#include <inc/sysprof.h>
#include <inc/lib.h>
#include <inc/pad.h>
#include <test.h>
#include <stdio.h>

#define read_l3miss() sysprof_rdpmc(0)
#define read_latcnt() sysprof_rdpmc(1)
#define read_cmdcnt() sysprof_rdpmc(2)

#endif

#if 0
static unsigned int pcpu[] = {
    0,  4,  8,  12,
    1,  5,  9,  13,
    2,  6,  10, 14,
    3,  7,  11, 15
};
#endif

static unsigned int pcpu[] = {
    0,  1,  2,  3,
    4,  5,  6,  7,
    8,  9,  10, 11,
    12, 13, 15, 15
};


#if LINUX
#include <stdint.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <test/pmc.h>

#define echeck(expr)					\
    ({							\
	int64_t __c = (expr);				\
	if (__c < 0)  				        \
            assert(0);					\
	__c;						\
    })

#define read_l3miss() pmc_l3_miss();
#define read_cmdcnt() pmc_cmdtot();
#define read_latcnt() pmc_cmdlat();

void * u_mmap(void *addr, size_t len, ...);
int ummap_alloc_init(void);

volatile __thread char buf0[buf_size] __attribute__((used));
volatile __thread char buf1[buf_size] __attribute__((used));

#endif

#define NCPU 16

#include <stdlib.h>
#include <string.h>

static struct {
    volatile int signal;

    union {
	uint64_t v;
	char pad[64];
    } cycles[NCPU];

    union {
	uint64_t v;
	char pad[64];
    } misses[NCPU];

    union {
	volatile uint64_t v;
	char pad[64];
    } ready[NCPU];

    union {
	volatile uint64_t v;
	char pad[64];
    } cmdlat[NCPU];

    uint64_t ops[num_ops];
    
} *shared_state;

uint64_t bleh __attribute__((used));

static void  __attribute__((unused))
do_malloc(uint32_t c)
{
#ifdef JOS_USER
    void *buf0 = malloc(buf_size);
    void *buf1 = malloc(buf_size);

    memcpy((void *)buf0, (void *)buf1, buf_size);
#endif

    if (c != 0) {
	for (int i = 0; i < num_ops; i++)
	    bleh += shared_state->ops[i];
	assert(malloc(4096));
	assert(u_mmap(0, 4096));
	shared_state->ready[c].v = 1;

	while (shared_state->signal == 0)
	    nop_pause();
    } else {
	shared_state->signal = 1;
    }

    uint64_t s = read_tsc();
    uint64_t sm = read_l3miss();
    uint64_t latcnt0 = read_latcnt();
    uint64_t cmdcnt0 = read_cmdcnt();
    
    for (int i = 0; i < num_ops; i++) {
	
	memcpy((void *)buf0, (void *)buf1, buf_size);

	//assert(u_mmap(0, shared_state->ops[i]));
	//assert(malloc(shared_state->ops[i]));
	//void *va = malloc(shared_state->ops[i]);
	//memset(va, 0, shared_state->ops[i]);
    }
    uint64_t e = read_tsc();
    uint64_t em = read_l3miss();
    uint64_t latcnt1 = read_latcnt();
    uint64_t cmdcnt1 = read_cmdcnt();
    
    shared_state->misses[c].v = em - sm;
    shared_state->cycles[c].v = e - s;
    if (cmdcnt1 - cmdcnt0)
	shared_state->cmdlat[c].v = (latcnt1 - latcnt0) / (cmdcnt1 - cmdcnt0);
}

static void __attribute__((unused))
do_realloc(uint32_t c)
{
    if (c != 0) {
	for (int i = 0; i < num_ops; i++)
	    bleh += shared_state->ops[i];
	
	while (shared_state->signal == 0)
	    nop_pause();
    } else {
	shared_state->signal = 1;
    }

    uint64_t s = read_tsc();
    uint64_t sm = read_l3miss();
    
    uint64_t size = 4096;
    void *va = malloc(size);
    memset(va, 0, size);
    
    for (int i = 0; i < num_ops; i++) {
	void *ptr = realloc(va, size + shared_state->ops[i]);
	memset(ptr + size, 0, shared_state->ops[i]);
	size += shared_state->ops[i];
    }
    uint64_t e = read_tsc();
    uint64_t em = read_l3miss();

    shared_state->misses[c].v = em - sm;
    shared_state->cycles[c].v = e - s;
}

#if JOSMP
static uint32_t
rnd(uint32_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return *seed & 0x7fffffff;
}

static void
init_shared_state(void)
{
    echeck(segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			 (void **)&shared_state, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));
}

static void
init_cores(uint32_t n)
{   
    sysprof_prog_l3miss(0);
    sysprof_prog_latcnt(1);
    sysprof_prog_cmdcnt(2);

    for (uint32_t i = 1; i < n; i++) {
	int64_t r;
	struct sobj_ref *sh_ref;
	uint64_t nsh_ref;
	ummap_get_shref(&sh_ref, &nsh_ref);

	echeck(r = pforkv(pcpu[i], PFORK_SHARE_HEAP, sh_ref, nsh_ref));
	if (r == 0) {
	    sysprof_prog_l3miss(0);
	    sysprof_prog_latcnt(1);
	    sysprof_prog_cmdcnt(2);
	    
	    do_malloc(pcpu[i]);
	    for (;;) ;
	}
    }

    time_delay_cycles(1000000000);
    time_delay_cycles(1000000000);
    time_delay_cycles(1000000000);
}

void __attribute__((noreturn))
malloc_test(void)
{
    sysprof_init();
    //ummap_alloc_init();
    init_shared_state();
    uint32_t seed = 1;
    for (int i = 0; i < num_ops; i++)
	shared_state->ops[i] = rnd(&seed) % max_op;

    init_cores(ncores);

    shared_state->ready[0].v = 1;
    for (int i = 0; i < ncores; i++)
	while (shared_state->ready[pcpu[i]].v == 0)
	    nop_pause();

    do_malloc(0);

    uint64_t cycles = 0, misses = 0;
    for (int i = 0; i < ncores; i++) {
	while (shared_state->cycles[pcpu[i]].v == 0)
	    nop_pause();

	cprintf("%d, cycles %ld\n", pcpu[i], shared_state->cycles[pcpu[i]].v);

	cycles += shared_state->cycles[pcpu[i]].v;
	misses += shared_state->misses[pcpu[i]].v;
    }

    printf("ave cycles %ld, ave miss %ld, misses/op %ld\n", 
	   cycles / ncores, misses / ncores, misses / (ncores * num_ops));
    printf("cmdlat %ld\n", shared_state->cmdlat[0].v);
    for (;;);
}
#endif

#if LINUX

static void *
thread_enter(void *x)
{
    uint32_t c = (uint32_t)(uintptr_t)x;

    pmc_init(c);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(c, &cpuset);
    assert(sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0);
    
    do_malloc(c);

    shared_state->cycles[pcpu[c]].v = 1;
    shared_state->ready[pcpu[c]].v = 1;
    
    for (;;);
}


int
main(int ac, char **av)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    assert(sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0);
    pmc_init(0);

    ummap_alloc_init();

    void *b = mmap(0, sizeof(*shared_state), PROT_READ | PROT_WRITE, 
		   MAP_ANONYMOUS | MAP_SHARED , 0, 0);
    assert(b);
    shared_state = b;

    uint32_t seed = 1;
    for (int i = 0; i < num_ops; i++)
	shared_state->ops[i] = rnd(&seed) % max_op;

    for (uint32_t i = 1; i < ncores; i++) {
	pthread_t th;
	assert(pthread_create(&th, 0, thread_enter, (void *)(uintptr_t)pcpu[i]) == 0);
    }

    shared_state->ready[0].v = 1;
    for (int i = 0; i < ncores; i++)
	while (shared_state->ready[pcpu[i]].v == 0)
	    nop_pause();

    do_malloc(0);

    uint64_t cycles = 0, misses = 0;
    for (int i = 0; i < ncores; i++) {
	while (shared_state->cycles[pcpu[i]].v == 0)
	    nop_pause();
	cycles += shared_state->cycles[pcpu[i]].v;
	misses += shared_state->misses[pcpu[i]].v;
    }

    printf("ave cycles %ld, ave miss %ld, misses/op %ld\n", 
	   cycles / ncores, misses / ncores, misses / (ncores * num_ops));

    printf("cmdlat %ld\n", shared_state->cmdlat[0].v);
}

#endif
