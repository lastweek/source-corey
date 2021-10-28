#include <machine/param.h>
#include <machine/x86.h>
#include <inc/spinlock.h>
#include <inc/lib.h>
#include <inc/lifo.h>
#include <inc/array.h>
#include <inc/queue.h>

#include <test.h>

#include <string.h>

enum { iters = 10000 };
enum { core_count = 16 };

struct lifo_elem {
    uint64_t a;
    LIFO_ENTRY(lifo_elem) lifo_link;
};

struct list_elem {
    uint64_t a;
    LIST_ENTRY(list_elem) list_link;
};

static struct {
    volatile char start;
    volatile char done;

    struct spinlock lock;

    LIFO_HEAD(lifo_head, lifo_elem) lifo_head;
    struct lifo_elem elem[core_count];
    
    LIST_HEAD(list_head, list_elem) list_head;
    struct list_elem list_elem[core_count];
    
    union {
	struct {
	    volatile uint64_t count;
	};
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU] __attribute__((aligned(JOS_CLINE)));
} *shared;

static void __attribute__((unused))
do_test(int cpu)
{
    if (core_env->pid != 0)
	while (!shared->start);
    else
	shared->start = 1;
    
    while (!shared->done) {
	spin_lock(&shared->lock);
	++shared->cpu[core_env->pid].count;
	spin_unlock(&shared->lock);
	nop_pause();
	if (core_env->pid == 0 && shared->cpu[core_env->pid].count == iters)
	    shared->done = 1;
    }
}

static void  __attribute__((unused))
do_test_lifo(int cpu)
{
    if (core_env->pid != 0)
	while (!shared->start);
    else
	shared->start = 1;
    
    while (!shared->done) {
	struct lifo_elem *e = LIFO_POP(&shared->lifo_head, lifo_link);
	++shared->cpu[core_env->pid].count;	
	LIFO_PUSH(&shared->lifo_head, e, lifo_link);
	if (core_env->pid == 0 && shared->cpu[core_env->pid].count == iters)
	    shared->done = 1;
    }
}

static void  __attribute__((unused))
do_test_dumb_lifo(int cpu)
{
    if (core_env->pid != 0)
	while (!shared->start);
    else
	shared->start = 1;
    
    while (!shared->done) {
	spin_lock(&shared->lock);
	struct list_elem *e = LIST_FIRST(&shared->list_head);
	LIST_REMOVE(e, list_link);
	spin_unlock(&shared->lock);
	++shared->cpu[core_env->pid].count;
	spin_lock(&shared->lock);
	LIST_INSERT_HEAD(&shared->list_head, e, list_link);
	spin_unlock(&shared->lock);
	if (core_env->pid == 0 && shared->cpu[core_env->pid].count == iters)
	    shared->done = 1;
    }
}

void
lock_test(void)
{
    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));
    memset(shared, 0, sizeof(*shared));

    LIFO_INIT(&shared->lifo_head);
    for (uint32_t i = 0; i < array_size(shared->elem); i++) {
	shared->elem[i].a = i;
	LIFO_PUSH(&shared->lifo_head, &shared->elem[i], lifo_link);
    }

    LIST_INIT(&shared->list_head);
    for (uint32_t i = 0; i < array_size(shared->elem); i++) {
	shared->elem[i].a = i;
	LIST_INSERT_HEAD(&shared->list_head, &shared->list_elem[i], list_link);
    }
    
    for (int i = 1; i < core_count; i++) {
	int64_t r = pfork(i);
	assert(r >= 0);
	if (r == 0) {
	    do_test_lifo(i);
	    processor_halt();
	}
    }

    // make sure other processors have started
    uint64_t x = read_tsc();
    while (read_tsc() - x < 1000000);
    
    uint64_t s = read_tsc();
    do_test(0);
    uint64_t e = read_tsc();

    uint64_t usec = (e - s) * 1000000 / core_env->cpufreq;
    
    uint64_t total = 0;
    for (int i = 0; i < JOS_NCPU; i++)
	total += shared->cpu[i].count;
    
    cprintf("%u completed: %lu in %lu usec %lu per/sec\n", 
	    core_count, total, usec, (total * 1000000) / usec);
}
