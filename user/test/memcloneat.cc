extern "C" {
#include <machine/x86.h>
#include <machine/memlayout.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/sysprof.h>

#include <test.h>
}
#include <inc/jmonitor.hh>
#include <inc/error.hh>
#include <stdlib.h>

enum { include_malloc = 1 };
enum { max_core_count = 2 };

#if 1
enum { alloc_size = 100 * 1024 * 1024 };
static proc_id_t pcpu[] = {
    0,  4,  8,  12,
    1,  5,  9,  13,
    2,  6,  10, 14,
    3,  7,  11, 15
};
#endif

#if 1
#define read_l3_miss() sysprof_rdpmc(0)
#define read_latcnt() sysprof_rdpmc(1)
#define read_cmdcnt() sysprof_rdpmc(2)
#else
static uint64_t __hack;
#define read_l3_miss() ++__hack
#endif


static struct {
    uint64_t volatile result[JOS_NCPU];
    uint64_t volatile miss[JOS_NCPU];
    uint64_t volatile cmdlat[JOS_NCPU];
    uint32_t volatile channel[2];
}  *shared_state;

static struct {
    uint64_t atid;
    void *va;
    struct sobj_ref sgref;
} memclone_data[JOS_NCPU];

static void
print_time(uint64_t cycle)
{
    //cprintf("%lu(us)\n", cycle * 1000000 / cpu_freq);
}

static void
print_average_time(uint64_t cycle)
{
    cprintf("average time : %lu(ms)\n", cycle * 1000 / core_env->cpufreq);
}

static void *
xmalloc(int bytes)
{
    int64_t id;
    error_check(id = sys_segment_alloc(core_env->sh, bytes, "seg", core_env->pid));
    struct sobj_ref sgref = SOBJ(core_env->sh, id);
    memclone_data[core_env->pid].sgref = sgref;
            
    struct u_address_mapping mapping ;
    memset(&mapping, 0, sizeof(mapping));
    
    mapping.type = address_mapping_segment;
    mapping.object = sgref;
    mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
    mapping.kslot = 0;
    mapping.va = 0;
    mapping.start_page = 0;
    mapping.num_pages = alloc_size / PGSIZE;

    struct sobj_ref atref = SOBJ(core_env->sh, memclone_data[core_env->pid].atid);

    error_check(sys_at_set_slot(atref, &mapping));    
    return memclone_data[core_env->pid].va;
}

static void
xfree(void *va)
{
    // XXX
#if 0
    error_check(as_unmap(va));
    error_check(sys_share_unref(memclone_data[core_env->pid].sgref));
#endif
}

static void
do_test(uint32_t core_count)
{
    unsigned long long total_cycle = 0;
    unsigned long long average_cycle;

    void *array;
    uint32_t *pos = NULL;
    uintptr_t endpos = 0;
    bool is_child = false;
    uint32_t proc_id = 0;
   
    shared_state->channel[0] = 0;
    shared_state->channel[1] = 0;

    sysprof_prog_l3miss(0);
    sysprof_prog_latcnt(1);
    sysprof_prog_cmdcnt(2);
    
    for (uint32_t i = 0; i < JOS_NCPU; i++) {
	shared_state->result[i] = 0;
    }

    // setup interior address trees
    uint64_t map_va = ULINKSTART;
    for (uint32_t i = 0; i < core_count; i++) {
	int64_t atid;
	echeck(atid = sys_at_alloc(core_env->sh, 1, "at-test", pcpu[i]));
	memclone_data[pcpu[i]].atid = atid;
	echeck(at_map_interior(SOBJ(core_env->sh, atid), 
			       SEGMAP_READ | SEGMAP_WRITE | SEGMAP_SHARED, 
			       (void *)map_va));
	memclone_data[pcpu[i]].va = (void *)map_va;
	map_va += PISIZE;
    }

    for (uint32_t i = 1; i < core_count; i++) {
	shared_state->channel[0] = i;
	int r = pforkv(pcpu[i], 0, NULL, 0);
	if (r < 0)
	    panic("unable to pfork into core %u: %s", i, e2s(r));
	if (r) {
	    // parent
	    while (shared_state->channel[0])
		nop_pause();
	    continue;
	} else {
	    // child
	    is_child = true;

	    sysprof_prog_l3miss(0);
	    sysprof_prog_latcnt(1);
	    sysprof_prog_cmdcnt(2);
	    
	    if (!include_malloc) {
		array = xmalloc(alloc_size);
		pos = (uint32_t *) array;
		endpos = (uintptr_t) array + alloc_size;
	    }

	    proc_id = shared_state->channel[0];
	    shared_state->channel[0] = 0;
	    break;
	}
    }

    if (is_child) {
	while (!shared_state->channel[1])
	    nop_pause();
    } else {
	if (!include_malloc) {
	    array = xmalloc(alloc_size);
	    pos = (uint32_t *) array;
	    endpos = (uintptr_t) array + alloc_size;
	}
	shared_state->channel[1] = 1;
    }
    
    uint64_t cycle0, cycle1;
    uint64_t miss0, miss1;
    uint64_t cmdcnt0, cmdcnt1;
    uint64_t latcnt0, latcnt1;

    // time begin
    cycle0 = read_tsc();
    miss0 = read_l3_miss();
    latcnt0 = read_latcnt();
    cmdcnt0 = read_cmdcnt();
    if (include_malloc) {
	array = xmalloc(alloc_size);
	
	pos = (uint32_t *) array;
	endpos = (uintptr_t) array + alloc_size;
    }

    while ((uintptr_t) pos != endpos) {
	*pos = 1;
	pos = (uint32_t *) ((uintptr_t) pos + PGSIZE);
    }
    cycle1 = read_tsc();
    miss1 = read_l3_miss();
    latcnt1 = read_latcnt();
    cmdcnt1 = read_cmdcnt();

    print_time(cycle1 - cycle0);
    shared_state->miss[proc_id] = miss1 - miss0;
    if (cmdcnt1 - cmdcnt0)
	shared_state->cmdlat[proc_id] = (latcnt1 - latcnt0) / (cmdcnt1 - cmdcnt0);
    else 
	shared_state->cmdlat[proc_id] = 0;
    shared_state->result[proc_id] = cycle1 - cycle0;

    if (!is_child) {
	for (uint32_t i = 1; i < core_count; i++)
	    while (!shared_state->result[i])
		nop_pause();

	uint64_t max_cycle = 0;
	total_cycle = 0;
	uint64_t total_miss = 0;
	uint64_t cmdlat = 0;

	for (uint32_t i = 0; i < JOS_NCPU; i++) {
	    total_cycle += shared_state->result[i];
	    total_miss += shared_state->miss[i];
	    cmdlat += shared_state->cmdlat[i];
	    if (shared_state->result[i] > max_cycle)
		max_cycle = shared_state->result[i];
	}

	uint64_t cycles_pf = total_cycle / (core_count * (alloc_size / PGSIZE));

	average_cycle = total_cycle / core_count;
	print_average_time(average_cycle);
	uint64_t miss_per_pf = total_miss / (core_count * (alloc_size / PGSIZE));
	cprintf("%u max: %lu(us)\n", core_count, max_cycle * 1000 / core_env->cpufreq);
	cprintf("miss/pf %lu\n", miss_per_pf);
	cprintf("cycles/pf %lu\n", cycles_pf);
	cprintf("cmdlat %ld\n", shared_state->cmdlat[0]);
	
	xfree(array);

    } else {
	// gc memory and address trees via processor_halt
	processor_halt();
    }
}

void __attribute__((noreturn))
memcloneat_test(void)
{
    cprintf("memclone: %u MB, %u max CPUs, CPU freq %lu MHz\n", 
	    alloc_size / (1024 * 1024), max_core_count, core_env->cpufreq / 1000000);

    int64_t r = segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			      (void **) &shared_state, SEGMAP_SHARED,
			      "memclone-share-seg", core_env->pid);
    if (r < 0)
	panic("segment_alloc failed: %s\n", e2s(r));

    sysprof_init();

    do_test(max_core_count);

    for (;;) {}

    // XXX
#if 0
    for (int i = 1; i <= max_core_count; i++) {
	memset(shared_state, 0, sizeof(*shared_state));
	do_test(i);
	for (int j = 0; j < 10000000; j++)
	    nop_pause();
    }
#endif
}
