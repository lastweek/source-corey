#ifndef JOS_MACHINE_SPINLOCK_H
#define JOS_MACHINE_SPINLOCK_H

#include <inc/types.h>
#include <machine/atomic.h>

struct spinlock 
{
    jos_atomic64_t locked;
};

#define SPINLOCK_ATTR static __inline __attribute__((always_inline, no_instrument_function))

SPINLOCK_ATTR void spin_lock(struct spinlock *s);
SPINLOCK_ATTR void spin_unlock(struct spinlock *s);
SPINLOCK_ATTR int  spin_locked(const struct spinlock *s);
SPINLOCK_ATTR void spin_init(struct spinlock *s);

/*
 * Using Spin-Loops on Intel Pentium 4 Processor and Intel Xeon Processor:
 * http://www.intel.com/cd/ids/developer/asmo-na/eng/17689.htm
 *
 * This implementation also performs well on AMD 10H systems.
 *
 * A more efficient implementation uses lock; decl then jns off the resulting
 * control bits; however, this requires the spinlock to be initialized to one.
 */

void 
spin_lock(struct spinlock *s)
{
    __asm __volatile(
		     "\n1:\t"
		     "movq		$1, %%r10 \n\t"
		     "lock; xchgq	%0, %%r10 \n\t"
		     "cmp		$0, %%r10 \n\t"
		     "je		3f \n\t"
		     "2:\t		pause \n\t"
		     "cmp		$0, %0 \n\t"
		     "jne		2b \n\t"
		     "jmp		1b \n\t"
		     "3:\t\n" 
		     : "=m" (s->locked.counter) : : "%r10", "memory", "cc");
}

void
spin_unlock(struct spinlock *s)
{
    jos_atomic_set64(&s->locked, 0);
}

int
spin_locked(const struct spinlock *s)
{
    return jos_atomic_read(&s->locked);
}

void
spin_init(struct spinlock *s)
{
    jos_atomic_set64(&s->locked, 0);
}

#ifdef JOS_KERNEL

uint32_t arch_cpu(void);

struct reclock
{
    struct spinlock l;
    uint32_t owner;
};

SPINLOCK_ATTR void rec_lock(struct reclock *r);
SPINLOCK_ATTR void rec_unlock(struct reclock *r);
SPINLOCK_ATTR void rec_init(struct reclock *r);

void
rec_lock(struct reclock *r)
{
    if (r->owner == arch_cpu())
	return

    spin_lock(&r->l);
    r->owner = arch_cpu();
}

void
rec_unlock(struct reclock *r)
{
    r->owner = ~0;
    spin_unlock(&r->l);
}

void
rec_init(struct reclock *r)
{
    spin_init(&r->l);
    r->owner = ~0;
}

#endif

#endif
