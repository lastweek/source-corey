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
enum { max_core_count = 16 };
enum { alloc_size = 100 * 1024 * 1024 };
enum { qemu = 0 };

static struct {
    uint64_t volatile result[JOS_NCPU];
    uint64_t volatile miss[JOS_NCPU];
    uint32_t volatile channel[2];
}  *shared_state;

static void
print_time(uint64_t cycle)
{
    //cprintf("%lu(us)\n", cycle * 1000000 / cpu_freq);
}

static void
print_average_time(uint64_t cycle)
{
    cprintf("average time : %lu(us)\n", cycle * 1000000 / core_env->cpufreq);
}

static void
do_test(uint32_t core_count)
{
    ummap_alloc_init();
    u_mmap(NULL, 1024 * 1024 * 1024);
    unsigned long long total_cycle = 0;
    unsigned long long average_cycle;

    /*void *array;
    uint32_t *pos = NULL;
    uintptr_t endpos = 0;*/
    bool is_child = false;
    uint32_t proc_id = 0;
   
    shared_state->channel[0] = 0;
    shared_state->channel[1] = 0;

    sysprof_prog_l3miss(0);

    for (uint32_t i = 0; i < core_count; i++) {
	shared_state->result[i] = 0;
    }

     struct sobj_ref shs[2];
    shs[0] = *ummap_get_shref();
    for (uint32_t i = 1; i < core_count; i++) {
	shared_state->channel[0] = i;
	int r = pforkv(i, PFORK_SHARE_HEAP, shs, 1);
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

	    /*if (!include_malloc) {
	        array = u_mmap(NULL ,alloc_size);
		pos = (uint32_t *) array;
		endpos = (uintptr_t) array + alloc_size;
	    }*/

	    proc_id = shared_state->channel[0];
	    shared_state->channel[0] = 0;
	    break;
	}
    }

    if (is_child) {
	u_mmap(NULL ,alloc_size);
	while (!shared_state->channel[1])
	    nop_pause();
    } else {
	/*if (!include_malloc) {
	    array = u_mmap(NULL ,alloc_size);
	    pos = (uint32_t *) array;
	    endpos = (uintptr_t) array + alloc_size;
	}*/
	shared_state->channel[1] = 1;
    }
    
    uint64_t cycle0, cycle1;
    uint64_t miss0 = 0, miss1 = 0;
    // time begin
    cycle0 = read_tsc();
    if (!qemu)
        miss0 = sysprof_rdpmc(0);
    /*if (include_malloc) {
	array = u_mmap(NULL ,alloc_size);
	pos = (uint32_t *) array;
	endpos = (uintptr_t) array + alloc_size;
    }
    while ((uintptr_t) pos != endpos) {
	*pos = 1;
	pos = (uint31_t *) ((uintptr_t) pos + PGSIZE);
    }*/
    if (!core_env->pid)
        ummap_prefault(alloc_size + 1024 * 1024 * 1024);
    else
        ummap_prefault(2 * alloc_size);
    cycle1 = read_tsc();
    if (!qemu)
        miss1 = sysprof_rdpmc(0);
    print_time(cycle1 - cycle0);
    shared_state->miss[proc_id] = miss1 - miss0;
    shared_state->result[proc_id] = cycle1 - cycle0;

    if (!is_child) {
	for (uint32_t i = 1; i < core_count; i++)
	    while (!shared_state->result[i])
		nop_pause();

	uint64_t max_cycle = 0;
	total_cycle = 0;
	uint64_t total_miss = 0;

	for (uint32_t i = 0; i < core_count; i++) {
	    total_cycle += shared_state->result[i];
	    total_miss += shared_state->miss[i];
	    if (shared_state->result[i] > max_cycle)
		max_cycle = shared_state->result[i];
	}

	average_cycle = total_cycle / core_count;
	print_average_time(average_cycle);
	uint64_t miss_per_pf = total_miss / (core_count * (alloc_size / PGSIZE));
	cprintf("%u max: %lu(us)\n", core_count, max_cycle * 1000000 / core_env->cpufreq);
	cprintf("miss/pf %lu\n", miss_per_pf);
	
    } else {
	// gc memory and address trees via processor_halt
	processor_halt();
    }
}

void
memcloneat_ydmao_test(void)
{
    cprintf("memclone: a%u MB, %u max CPUs, CPU freq %lu MHz\n", 
	    alloc_size / (1024 * 1024), max_core_count, core_env->cpufreq / 1000000);

    int64_t r = segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			      (void **) &shared_state, SEGMAP_SHARED,
			      "memclone-share-seg", core_env->pid);
    if (r < 0)
	panic("segment_alloc failed: %s\n", e2s(r));

    sysprof_init();

    do_test(max_core_count);

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
