#ifndef JOS_MACHINE_MCS_H
#define JOS_MACHINE_MCS_H

#include <machine/x86.h>
#include <inc/pad.h>

#ifdef JOS_KERNEL
#include <kern/lib.h>
#else
#include <string.h>
#endif

struct qnode {
    volatile struct qnode *next;
    volatile char locked;
};

struct mcslock {
    struct qnode *v;
    PAD(struct qnode) nodes[JOS_NCPU];
};

static __inline__ struct qnode *
fetch_and_store(struct mcslock *ml, volatile struct qnode *n) 
{
    struct qnode *out;
    __asm__ volatile(
    		"movq %2, %%r10\n\t"
		"lock; xchgq %1, %%r10\n\t"
		"movq %%r10, %0"
		: "=a" (out), "+m" (ml->v)
		: "q" (n)
		: "%r10", "memory", "cc");
    return out;
}

static __inline__ uint64_t
cmp_and_swap(struct mcslock *ml, volatile struct qnode *n, uint64_t newval)
{
    struct qnode *out;
    __asm__ volatile(
    		"lock; cmpxchgq %2, %1"
		: "=a" (out), "+m" (ml->v)
		: "q" (newval), "0"(n)
		: "cc");
    return out == n;
}

static __inline__ void
mcs_init(struct mcslock *ml)
{
    memset(ml, 0, sizeof(*ml));
}

static __inline__ void 
mcs_lock(struct mcslock *ml)
{
    volatile struct qnode *mynode = &ml->nodes[arch_cpu()].v;
    mynode->next = 0;

    struct qnode *predecessor = fetch_and_store(ml, mynode);
    if (predecessor) {
	mynode->locked = 1;
	predecessor->next = mynode;
	while (mynode->locked)
	    nop_pause();
    }
}

static __inline__ void
mcs_unlock(struct mcslock *ml)
{
    volatile struct qnode *mynode = &ml->nodes[arch_cpu()].v;
    if (!mynode->next) {
        if (cmp_and_swap(ml, mynode, 0))
	    return;
	while (!mynode->next)
	    nop_pause();
    }
    mynode->next->locked = 0;
}

struct mcsrwlock {
    struct mcslock ml;
    jos_atomic_t x;
};

#define MCSRW_BIAS      0x01000000
#define MCSRW_BIAS_STR "0x01000000"

static __inline__ void
mcsrw_init(struct mcsrwlock *rwl)
{
    memset(rwl, 0, sizeof(*rwl));
    jos_atomic_set(&rwl->x, MCSRW_BIAS);
}

static __inline__ void
mcsrw_read_lock(struct mcsrwlock *rwl)
{
    __asm __volatile("3: lock; subl $1, (%0) \n\t"
		     "jns 1f \n\t"
		     "lock; incl (%0) \n\t"
		     "2: rep \n\t"
		     "nop \n\t"
		     "cmpl $1, (%0) \n\t"
		     "js 2b \n\t"
		     "jmp 3b \n\t"
		     "1: \n\t"
		     : 
		     : "r" (&rwl->x.counter)
		     : "memory", "cc" );
}

static __inline__ void
mcsrw_read_unlock(struct mcsrwlock *rwl)
{
    jos_atomic_inc(&rwl->x);
}

static __inline__ void
mcsrw_write_lock(struct mcsrwlock *rwl)
{
    char z;
    mcs_lock(&rwl->ml);
    __asm __volatile("lock; subl %2, (%1) \n\t"
		     "setz %0 \n\t"
		     : "=d" (z)
		     : "r" (&rwl->x.counter), "i" (MCSRW_BIAS)
		     : "memory", "cc" );
    if (z)
	return;

    while (rwl->x.counter != 0)
	nop_pause();
}

static __inline__ void
mcsrw_write_unlock(struct mcsrwlock *rwl)
{
    __asm __volatile("lock; addl $" MCSRW_BIAS_STR ", %0"
		     : "=m" (rwl->x.counter) : : "memory", "cc" );
    mcs_unlock(&rwl->ml);
}

#endif
