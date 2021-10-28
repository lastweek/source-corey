#ifndef JOS_INC_THREADQ_H
#define JOS_INC_THREADQ_H

#include <inc/thread.h>

enum { name_size = 32 };

struct thread_context;

/* Special queue for thread.c.  It does not use any indirect pointers
 * so correctness is independent of location in AS.
 */

struct thread_queue
{
    struct thread_context *tq_first;
    struct thread_context *tq_last;
    struct spinlock        tq_lock;
};

struct thread_context 
{
    struct sobj_ref    tc_sg;
    thread_id_t        tc_id;
    char               tc_name[name_size];
    struct jos_jmp_buf tc_buf;
    struct sobj_ref    tc_stack_sg;
    void *             tc_stack_bottom;
    struct map_slot *  tc_slot;
    int                tc_errno;
    uint64_t           tc_entry_arg;
    volatile uint64_t *tc_wait_addr;
    volatile char      tc_wakeup;
    void (*tc_entry)(uint64_t);
    struct thread_context *tc_queue_link;

    void (*tc_onhalt[4])(void);
    int tc_nonhalt;
};

static inline void 
threadq_init(struct thread_queue *tq)
{
    spin_init(&tq->tq_lock);
    tq->tq_first = 0;
    tq->tq_last = 0;
}

static inline void
threadq_push(struct thread_queue *tq, struct thread_context *tc)
{
    spin_lock(&tq->tq_lock);
    tc->tc_queue_link = 0;
    if (!tq->tq_first) {
	tq->tq_first = tc;
	tq->tq_last = tc;
    } else {
	tq->tq_last->tc_queue_link = tc;
	tq->tq_last = tc;
    }
    spin_unlock(&tq->tq_lock);
}

static inline struct thread_context *
threadq_pop(struct thread_queue *tq)
{
    spin_lock(&tq->tq_lock);
    if (!tq->tq_first) {
	spin_unlock(&tq->tq_lock);
	return 0;
    }

    struct thread_context *tc = tq->tq_first;
    tq->tq_first = tc->tc_queue_link;
    tc->tc_queue_link = 0;
    spin_unlock(&tq->tq_lock);
    return tc;
}

#endif
