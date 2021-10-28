#include <machine/x86.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/queue.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/compiler.h>
#include <stdio.h>
#include <stdlib.h>
#include "mr-thread.h"

enum { max_shares = 8 };
enum { idle = 0, dispatched, running };

struct thread_gunk {
    void *arg;
    void * (*start_routine)(void *);
    volatile thread_id_t real_tid;
    volatile int state;
} __attribute__ ((aligned (JOS_CLINE)));

static struct {
    struct thread_gunk tg[JOS_NCPU];
} shared JSHARED_ATTR;

static int JSHARED_ATTR mr_procs_init = 0;

int
mthread_join(uint64_t faked_tid, void ** thread_return)
{
    uint32_t pid = faked_tid >> 32;
    while (shared.tg[pid].state != idle)
        thread_yield();
    if (thread_return)
        *thread_return = 0;
    return 0;
}

static void
mthread_entry_stub(uint64_t arg)
{
    struct thread_gunk *gunk = (struct thread_gunk *)arg;
    gunk->start_routine(gunk->arg);
    
    //Force streamflow to finalize the local heap
    //free((void *)-1);
    gunk->state = idle;
}

static void __attribute__((noreturn))
mthread_scheduler(uint64_t pid)
{
    struct thread_gunk *gunk = &shared.tg[pid];
       
    for (;;) {
        while (gunk->state != dispatched)
            thread_yield();
        gunk->state = running;
        gunk->start_routine(gunk->arg);
        gunk->state = idle;
    }
}

void
mr_init_processors(uint64_t pfork_flag)
{
    int64_t r;
    uint32_t i;
    
    if (mr_procs_init != 0)
        return;
    mr_procs_init = 1;
    for (i = 0; i < JOS_NCPU; i++)
        shared.tg[i].state = idle;

    struct u_locality_matrix ulm;

    struct sobj_ref *mr_shares;
    uint64_t shcnt;
    
    ummap_get_shref(&mr_shares, &shcnt);

    sys_locality_get(&ulm);
    for (i = 0; i < ulm.ncpu && i < JOS_NCPU; i++) {
        shared.tg[i].state = idle;
        if (i == processor_current_procid())
            continue;
	r = thread_pfork("MR-scheduler", mthread_scheduler, pfork_flag, 
			 mr_shares, shcnt,  i, i);
        if (r < 0)
            panic("should just skip.. %s\n", e2s(r));
    }
}

static void * __attribute__((noreturn))
finalize_proc(void __attribute__((unused)) * args)
{
    processor_halt();
}

void
mr_finalize_processors(void)
{
    struct u_locality_matrix ulm;
    int r;
    uint32_t i;
    
    sys_locality_get(&ulm);
    for (i = 0; i < ulm.ncpu; i++) {
        if (i == processor_current_procid())
            continue;
	r = mthread_create(NULL, i, finalize_proc , NULL);
        if (r < 0)
            panic("should just skip..");
    }
    /* TODO: free segment and runqueue */
    mr_procs_init = 0;
    ummap_finit();
}

/*
 * add cpu assignment support to substitute round-roibin scheduling of CPUs.
 */
int
mthread_create(uint64_t *faked_tid,  uint32_t pid,
               void * (*start_routine)(void *), void *arg)
{
    int64_t r;
    if (!mr_procs_init)
        /*by default: sharing heap and ummap */
        mr_init_processors(PFORK_SHARE_HEAP);
    struct thread_gunk *gunk = &shared.tg[pid];
    gunk->arg = arg;
    gunk->start_routine = start_routine;

    if (pid == processor_current_procid()) {
	gunk->state = running;
        r = thread_create((thread_id_t *)&gunk->real_tid, "MR-thread", mthread_entry_stub,
                          (uint64_t)(uintptr_t)gunk);
    } else {
        r = 0;
        gunk->state = dispatched;
        while (shared.tg[pid].state == dispatched);
    }

    if (r < 0) {
        gunk->state = idle;
        return r;
    }
    if (faked_tid)
        *faked_tid = (UINT64(0x00000000FFFFFFFF) & gunk->real_tid) |
                     ((uint64_t)pid << 32);
    return 0;
}
