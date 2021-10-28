#ifndef JOS_INC_RWLOCK_H
#define JOS_INC_RWLOCK_H

#include <inc/types.h>
#include <inc/proc.h>
#include <machine/atomic.h>
#include <machine/param.h>
#include <machine/rwlock.h>

// split/separate rw lock

#define SRW_NREADERS 16

struct srwlock
{
    jos_atomic_t write_locked;
    union {
	jos_atomic_t locked;
	char __pad[JOS_CLINE];
    } readers[SRW_NREADERS] __attribute__((aligned(JOS_CLINE)));
};

void srw_read_lock(struct srwlock *l, proc_id_t pid);
void srw_write_lock(struct srwlock *l);
void srw_read_unlock(struct srwlock *l, proc_id_t pid);
void srw_write_unlock(struct srwlock *l);
void srw_init(struct srwlock *l);

#endif
