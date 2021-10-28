#include <inc/rwlock.h>
#ifdef JOS_KERNEL
#include <kern/lib.h>
#else
#include <inc/assert.h>
#endif

// split/separate rw lock, poor implementation

void 
srw_read_lock(struct srwlock *l, proc_id_t pid)
{
    jos_atomic_inc(&l->readers[pid % SRW_NREADERS].locked);
    while (jos_atomic_read(&l->write_locked));
}

void 
srw_write_lock(struct srwlock *l)
{
 top:
    for (int i = 0; i < SRW_NREADERS; i++)
	if (jos_atomic_read(&l->readers[i].locked))
	    goto top;

    while (jos_atomic_compare_exchange(&l->write_locked, 0, 1) == 1);

    for (int i = 0; i < SRW_NREADERS; i++)
	if (jos_atomic_read(&l->readers[i].locked)) {
	    jos_atomic_set(&l->write_locked, 0);
	    goto top;
	}
}

void 
srw_read_unlock(struct srwlock *l, proc_id_t pid)
{
    assert(jos_atomic_read(&l->write_locked) <= 1);
    jos_atomic_dec(&l->readers[pid % SRW_NREADERS].locked);
}

void 
srw_write_unlock(struct srwlock *l)
{
    assert(jos_atomic_read(&l->write_locked) == 1);
    jos_atomic_set(&l->write_locked, 0);
}

void 
srw_init(struct srwlock *l)
{
    for (int i = 0; i < SRW_NREADERS; i++)
	jos_atomic_set(&l->readers[i].locked, 0);
    jos_atomic_set(&l->write_locked, 0);
}
