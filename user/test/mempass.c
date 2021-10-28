#include <machine/compiler.h>
#include <machine/mmu.h>
#include <machine/memlayout.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/arch.h>
#include <inc/lib.h>
#include <inc/pad.h>

#include <string.h>

#include <test.h>

enum { num_bufs = 25600 };
enum { iter_count = 5 };

static struct {
    PAD(volatile int) signal[JOS_NCPU];
    PAD(volatile int) halt_signal;
    void *buf;
    uint32_t core_count;
} state JSHARED_ATTR;

static uint64_t nsec_tot;
static uint64_t cycles_tot;

static void
obj_alloc(uint64_t sh, struct sobj_ref *seg_ref, struct sobj_ref *at_ref)
{    
    int64_t sgid, atid;
    echeck(sgid = sys_segment_alloc(sh, PGSIZE * (num_bufs + 1), "seg", core_env->pid));
    *seg_ref = SOBJ(sh, sgid);
    
    echeck(atid = sys_at_alloc(sh, 1, "at-test", core_env->pid));
    *at_ref = SOBJ(sh, atid);
    
    struct u_address_mapping mapping ;
    memset(&mapping, 0, sizeof(mapping));
    
    mapping.type = address_mapping_segment;
    mapping.object = *seg_ref;
    mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
    mapping.kslot = 0;
    mapping.va = 0;
    mapping.start_page = 0;
    mapping.num_pages = num_bufs + 1;

    echeck(sys_at_set_slot(*at_ref, &mapping));    
}

static void
do_test(void)
{
    char *buf = state.buf;
    buf[0] = core_env->pid;
    
    if (core_env->pid != 0)
	while (state.signal[core_env->pid].v == 0)
	    arch_pause();
    
    for (int i = 0; i < num_bufs; i++)
	buf[4096 + (i * 4096)] = core_env->pid;
    state.signal[(core_env->pid +  1) % state.core_count].v = 1;
}

static void
do_one(uint32_t core_count)
{
    //cprintf("mempass: %u max CPUs, CPU freq %lu MHz\n", 
    //core_count, core_env->cpufreq / 1000000);

    memset(&state, 0, sizeof(state));
    state.core_count = core_count;

    int64_t sh;
    echeck(sh = sys_share_alloc(core_env->sh, ~0, "share", core_env->pid));
    
    struct sobj_ref seg_ref, at_ref, sh_ref;
    obj_alloc(sh, &seg_ref, &at_ref);
    sh_ref = SOBJ(core_env->sh, sh);

    uint64_t map_va = ULINKSTART;
    echeck(at_map_interior(at_ref, 
			   SEGMAP_READ | SEGMAP_WRITE | SEGMAP_SHARED, 
			   (void *)map_va));
    
    state.buf = (void*)map_va;
    char *buf = state.buf;
    buf[0] = core_env->pid;
    
    for (uint32_t i = 1; i < core_count; i++) {
	int64_t r;
	echeck(r = pforkv(i, 0, &sh_ref, 1));
	if (r == 0) {
	    do_test();
	    while (state.halt_signal.v == 0)
		arch_pause();
	    processor_halt();
	}
    }
    
    // wait for all cpus to start
    time_delay_cycles(1000000000);

    uint64_t s = arch_read_tsc();
    do_test();

    while (state.signal[core_env->pid].v == 0)
	arch_pause();

    uint64_t e = arch_read_tsc();

    uint64_t nsec = time_cycles_to_nsec(e - s);

    nsec_tot += nsec;
    cycles_tot += e - s;
    
    //cprintf("cores %u, usec %ld\n", core_count, nsec / 1000);
    state.halt_signal.v = 1;
    time_delay_cycles(1000000000);
    time_delay_cycles(1000000000);
    
    as_unmap(state.buf);
    sys_share_unref(sh_ref);

}

void
mempass_test(void)
{
    for (uint32_t i = 1; i <= 16; i++) {
	nsec_tot = 0;
	cycles_tot = 0;
	for (int k = 0; k < iter_count; k++)
	    do_one(i);
	uint64_t ave_nsec = nsec_tot / iter_count;
	cprintf("cores %u, ave cycles, ave_cycles %ld, ave usec %ld\n", 
		i, cycles_tot / iter_count, ave_nsec / 1000);
    }
}
