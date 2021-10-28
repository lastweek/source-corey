#include <inc/cpuman.h>
#include <inc/assert.h>
#include <inc/spinlock.h>

enum { idle = 0, allocated };

static proc_id_t
cpu_man_alloc(struct cpu_state *cs, proc_id_t pid, proc_id_t nrange)
{
    for (proc_id_t i = pid; i < pid + nrange; i++) {
	if (cs->state[i] == idle) {
	    cs->state[i] = allocated;
	    return i;
	}
    }
    return INVALID_PROCID;
}

int32_t
cpu_man_init(struct cpu_state * cs)
{
    spin_init(&cs->lock);
    for (uint32_t i = 0; i < sizeof(cs->state); i++)
	cs->state[i] = idle;

    cs->state[core_env->pid] = allocated;
    cs->inited = 1;
    if (numa) {
	//do a system call to fill the nhops. Currently, we just
	//fill it manually for AMD
	for (int i = 0; i < nchips; i++)
	    cs->nhops[i][i] = 0;
#define SET_NHOP(a, b, nhop) cs->nhops[a][b] = nhop; \
			     cs->nhops[b][a] = nhop
	SET_NHOP(0, 1, 1);
	SET_NHOP(0, 2, 1);
	SET_NHOP(0, 3, 1);
	SET_NHOP(1, 2, 2);
	SET_NHOP(1, 3, 1);
	SET_NHOP(2, 3, 1);
#undef SET_NHOP
    } else {
	for (int i = 0; i < nchips; i++) {
	    for (int j = 0; j < nchips; j++)
		cs->nhops[i][j] = (i == j) ? 0 : 1;
	}
    }
    return 0;
}

proc_id_t
cpu_man_any(struct cpu_state * cs)
{
    spin_lock(&cs->lock);
    proc_id_t pid = cpu_man_alloc(cs, 0, sizeof(cs->state));
    spin_unlock(&cs->lock);
    return pid;
}

proc_id_t
cpu_man_nearby(struct cpu_state * cs, proc_id_t pid)
{
    spin_lock(&cs->lock);
    //allocate from the same die
    proc_id_t nearby = cpu_man_alloc(cs, pid & -ncores_pd, ncores_pd);
    if (ALLOC_FAILED(nearby)) {
	//allocate from the same chip
	nearby = cpu_man_alloc(cs, pid & -ncores_pc, ncores_pc);
    }
    spin_unlock(&cs->lock);
    //allocate any
    if (ALLOC_FAILED(nearby))
	return cpu_man_any(cs);
    return nearby;
}

proc_id_t
cpu_man_remote(struct cpu_state * cs, proc_id_t pid, uint32_t nhop)
{
    proc_id_t remote = INVALID_PROCID;
    spin_lock(&cs->lock);
    //allocate from chips nhop away
    uint32_t cid = pid / ncores_pc;
    for (uint32_t i = 0; i < nchips; i++)
	if (cs->nhops[cid][i] == nhop) {
	    remote = cpu_man_alloc(cs, i * ncores_pc, ncores_pc);
	    if (!ALLOC_FAILED(remote))
		break;
	}
    spin_unlock(&cs->lock);
    if (ALLOC_FAILED(remote))
	remote = cpu_man_any(cs);
    return remote;
}

proc_id_t
cpu_man_freedie(struct cpu_state * cs)
{
    spin_lock(&cs->lock);
    proc_id_t pid = INVALID_PROCID;
    for (proc_id_t i = 0; i < sizeof(cs->state) / ncores_pd; i++) {
	int32_t free = 1;
	//check whether the i'th die is free
	for (int32_t j = 0; j < ncores_pd; j++) {
	    if (cs->state[i * ncores_pd + j] == allocated) {
		free = 0;
		break;
	    }
	}
	if (free) {
	    cs->state[i * ncores_pd] = allocated;
	    pid = i * ncores_pd;
	    break;
	}
    }
    spin_unlock(&cs->lock);
    if (ALLOC_FAILED(pid))
	pid = cpu_man_any(cs);
    return pid;
}

proc_id_t
cpu_man_freechip(struct cpu_state * cs)
{
    spin_lock(&cs->lock);
    proc_id_t pid = INVALID_PROCID;
    for (proc_id_t i = 0; i < nchips; i++) {
	int32_t free = 1;
	//check whether the i'th chip is free
	for (int32_t j = 0; j < ncores_pc; j++) {
	    if (cs->state[i * ncores_pc + j] == allocated) {
		free = 0;
		break;
	    }
	}
	if (free) {
	    cs->state[i * ncores_pc] = allocated;
	    pid = i * ncores_pc;
	    break;
	}
    }
    spin_unlock(&cs->lock);
    if (ALLOC_FAILED(pid))
	pid = cpu_man_any(cs);
    return pid;
}

void
cpu_man_group(struct cpu_state *cs, struct cpu_state *group, uint32_t ncores)
{
    cpu_man_init(group);
    for (proc_id_t i = 0; i < sizeof(cs->state); i++)
	group->state[i] = allocated;
    proc_id_t first;
    if (ncores <= ncores_pd)
	first = cpu_man_freedie(cs);
    else if (ncores <= ncores_pc)
	first = cpu_man_freechip(cs);
    else
	first = cpu_man_any(cs);
    group->state[first] = idle;
    for (uint32_t i = 1; i < ncores; i++) {
	proc_id_t nearby_pid = cpu_man_nearby(cs, first);
	group->state[nearby_pid] = idle;
    }
}

void
cpu_man_group_nearby(struct cpu_state *cs, struct cpu_state *group,
		     uint32_t ncores, proc_id_t pid)
{
    cpu_man_init(group);
    for (proc_id_t i = 0; i < sizeof(cs->state); i++)
	group->state[i] = allocated;
    for (uint32_t i = 0; i < ncores; i++) {
	proc_id_t nearby_pid = cpu_man_nearby(cs, pid);
	group->state[nearby_pid] = idle;
    }
}

void
cpu_man_print(struct cpu_state *cs)
{
    cprintf("cpu_state containing fowllwing cores: ");
    for (proc_id_t i = 0; i < sizeof(cs->state); i++)
	if (cs->state[i] == idle)
	    cprintf("%u,", i);
    cprintf("\n");
}
