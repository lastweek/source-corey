#include <machine/compiler.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/pad.h>
#include <inc/arch.h>
#include <inc/sysprof.h>
#include <test.h>

#include <string.h>

enum { share_ops = 100000 };

static struct {
    volatile int ready;
    PAD(volatile uint64_t) cycles[JOS_NCPU];
    PAD(volatile uint64_t) misses[JOS_NCPU];
} stats JSHARED_ATTR ;


static void
share_bang(uint64_t sh_id, uint64_t core)
{
    sysprof_prog_l3miss(0);
    
    if (core == 0)
	stats.ready = 1;
    else
	while (stats.ready != 1)
	    arch_pause();

    uint64_t s = read_tsc();
    uint64_t sl3 = sysprof_rdpmc(0);

    int64_t r = 0;
    echeck(r = sys_segment_alloc(core_env->sh, 0, "foo", core_env->pid));
    struct sobj_ref sgref = SOBJ(core_env->sh, r);


    uint64_t n = 0;
    while (1) {
	//echeck(r = sys_segment_alloc(sh_id, 0, "foo", core_env->pid));
	echeck(sys_share_addref(sh_id, sgref));
	echeck(sys_share_unref(SOBJ(sh_id, r)));
	//sys_share_unref(SOBJ(sh_id, r));
	n++;

	if (n == share_ops) {
	    uint64_t e = read_tsc();
	    uint64_t el3 = sysprof_rdpmc(0);
	    stats.misses[core].v = el3 - sl3;
	    stats.cycles[core].v = e - s;
	    break;
	}
    }

    echeck(sys_share_unref(sgref));
}

void __attribute__((noreturn))
share_perf_test(void)
{
    enum { max_cores = 16 };

    sysprof_init();

    int64_t sh_id;
    echeck(sh_id = sys_share_alloc(core_env->sh, ~0, "foo-share", core_env->pid));
    struct sobj_ref shref = SOBJ(core_env->sh, sh_id);

    cprintf("global share, %d alloc+unref per core:\n", share_ops);
    for (int i = 1; i <= max_cores; i++) {
	memset(&stats, 0, sizeof(stats));

	for (int k = 1; k < i; k++) {
	    int64_t r = 0;
	    echeck(r = pforkv(k, 0, &shref, 1));
	    if (r == 0) {
		share_bang(sh_id, k);
		while (stats.ready == 1)
		    arch_pause();
		processor_halt();
	    }
	}

	share_bang(sh_id, 0);

	uint64_t count = 0;
	uint64_t max = 0;
	uint64_t misses = 0;

	for (int j = 0; j < i; j++) {
	    while (stats.cycles[j].v == 0)
		arch_pause();
	    count += stats.cycles[j].v;
	    misses += stats.misses[j].v;
	    if (stats.cycles[j].v > max)
		max = stats.cycles[j].v;
	}

	stats.ready = 0;

	uint64_t ave = count / i;
	uint64_t per = misses / (i * share_ops);

	ave = time_cycles_to_nsec(ave) / 1000000;
	max = time_cycles_to_nsec(max) / 1000000;

	cprintf(" cores %d, max %ld ms, ave %ld ms, misses/op %ld\n", i, max, ave, per);

	time_delay_cycles(1000000000);
	time_delay_cycles(1000000000);
	time_delay_cycles(1000000000);
    }

    cprintf("per-core shares, %d alloc+unref per core:\n", share_ops);
    for (int i = 1; i <= max_cores; i++) {
	memset(&stats, 0, sizeof(stats));

	for (int k = 1; k < i; k++) {
	    int64_t r = 0;
	    echeck(r = pfork(k));
	    if (r == 0) {
		share_bang(core_env->sh, k);
		processor_halt();
	    }
	}

	share_bang(core_env->sh, 0);

	uint64_t count = 0;
	uint64_t max = 0;
	uint64_t misses = 0;

	for (int j = 0; j < i; j++) {
	    while (stats.cycles[j].v == 0)
		arch_pause();
	    count += stats.cycles[j].v;
	    misses += stats.misses[j].v;
	    if (stats.cycles[j].v > max)
		max = stats.cycles[j].v;
	}

	uint64_t ave = count / i;
	uint64_t per = misses / (i * share_ops);

	ave = time_cycles_to_nsec(ave) / 1000000;
	max = time_cycles_to_nsec(max) / 1000000;

	cprintf(" cores %d, max %ld ms, ave %ld ms, misses/op %ld\n", i, max, ave, per);

	time_delay_cycles(1000000000);
    }

    for (;;) ;
}

void
share_test(void)
{
    int64_t sh_id, sg_id;
    echeck(sh_id = sys_share_alloc(core_env->sh, 1 << kobj_segment, 
				   "foo-share", core_env->pid));
    echeck(sg_id = sys_segment_alloc(sh_id, 128, "foo-segment", core_env->pid));
    echeck(sys_share_addref(sh_id, SOBJ(sh_id, sg_id)));

    echeck(sys_share_unref(SOBJ(sh_id, sg_id)));
    echeck(sys_share_unref(SOBJ(sh_id, sg_id)));
    
    // XXX
    // drop shares
    // drop shares with segments mapped into AS
}
