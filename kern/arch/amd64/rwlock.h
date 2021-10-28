#ifndef JOS_MACHINE_RWLOCK_H
#define JOS_MACHINE_RWLOCK_H

#include <inc/types.h>
#include <machine/param.h>

/*
 * Copied from Linux:
 *  include/asm-x86/spinlock.h
 *  arch/x86/lib/rwlock_64.S
 */

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

struct rwlock 
{
    unsigned int lock;
};

static inline void 
rw_read_lock(struct rwlock *l)
{
    __asm __volatile("lock; subl $1,(%0)\n\t"
		     "jns 3f\n"
		     "1:\n"
		     "lock; incl (%0)\n"
		     "2: rep\n"
		     "nop\n"
		     "cmpl $1,(%0)\n"
		     "js 2b\n"
		     "lock; decl (%0)\n"
		     "js 1b\n"
		     "3:\n"
		     ::"D" (l) : "memory");
}

static inline void 
rw_write_lock(struct rwlock *l)
{
    __asm __volatile("lock; subl %1,(%0)\n\t"
		     "jz 3f\n"
		     "1:\n"
		     "lock; addl %1,(%0)\n"
		     "2: rep\n"
		     "nop\n"
		     "cmpl %1,(%0)\n"
		     "jne 2b\n"
		     "lock; subl %1,(%0)\n"
		     "jnz  1b\n"
		     "3:\n"
		     ::"D" (l), "i" (RW_LOCK_BIAS) : "memory");
}

static inline void 
rw_read_unlock(struct rwlock *l)
{
    __asm __volatile("lock; incl %0" :"=m" (l->lock) : : "memory");
}

static inline void 
rw_write_unlock(struct rwlock *l)
{
    __asm __volatile("lock; addl $" RW_LOCK_BIAS_STR ",%0"
		     : "=m" (l->lock) : : "memory");
}

static inline int
rw_read_can_lock(struct rwlock *l)
{
    return l->lock > 0;
}

static inline int
rw_write_can_lock(struct rwlock *l)
{
    return l->lock == RW_LOCK_BIAS;
}

static inline void 
rw_init(struct rwlock *l)
{
    l->lock = RW_LOCK_BIAS;
}

#endif
