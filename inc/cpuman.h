#ifndef JOS_INC_CPUMAN_H
#define JOS_INC_CPUMAN_H
#include <inc/lib.h>
#include <inc/spinlock.h>

#define INVALID_PROCID JOS_NCPU
#define ALLOC_FAILED(pid) (pid == INVALID_PROCID)
#define CHIP_INDEX(cid, offset) ((cid + nchips + offset) % nchips)

/*
 * XXX should use CPUID to get nchips, ncores_pc, ncores_pd at
 * runtime.
 */
enum { nchips = 4 };
enum { ncores_pc = 4 };		// ncores per chip
enum { ncores_pd = 2 };		// ncores per die
enum { numa = 1 };

struct cpu_state {
    uint8_t inited;
    uint8_t state[JOS_NCPU];
    struct spinlock lock;
    uint8_t nhops[nchips][nchips];
};

int32_t cpu_man_init(struct cpu_state *cs);
proc_id_t cpu_man_any(struct cpu_state *cs);
proc_id_t cpu_man_nearby(struct cpu_state *cs, proc_id_t pid);
proc_id_t cpu_man_remote(struct cpu_state *cs, proc_id_t pid, uint32_t nhop);
proc_id_t cpu_man_freedie(struct cpu_state *cs);
proc_id_t cpu_man_freechip(struct cpu_state *cs);
void cpu_man_group(struct cpu_state *cs, struct cpu_state *group,
		   uint32_t ncores);
void cpu_man_group_nearby(struct cpu_state *cs, struct cpu_state *group,
			  uint32_t ncores, proc_id_t pid);
void cpu_man_print(struct cpu_state *cs);
#endif
