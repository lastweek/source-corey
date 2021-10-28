#include <machine/memlayout.h>
#include <machine/atomic.h>
#include <inc/spinlock.h>
#include <inc/setjmp.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/error.h>
#include <inc/queue.h>
#include <inc/assert.h>
#include <inc/copy.h>
#include <inc/threadq.h>
#include <inc/array.h>

#include <string.h>
#include <errno.h>

enum { thread_init_debug = 1 };
enum { thread_alloc_debug = 0 };
enum { processor_debug = 1 };
enum { thread_cache = 1 };
enum { do_halt = 0 };

enum { max_threads = 1024 };
enum { stack_size = 8 * PGSIZE };

/* Processor+AS global map_slot_free_list.  Within an instance of a 
 * libOS, we synchronize free addresses ranges.  This makes migrating
 * threads between Processor+ASs simpler.
 */
struct map_slot {
    void *va;
    void *stack;
    char cached;
    LIST_ENTRY(map_slot) map_list_link;
};

struct proc_slot {
    struct sobj_ref ps_id;
    struct sobj_ref as_id;
    struct sobj_ref sg_id;
};

/* Processor+AS global thread_table */
struct thread_table
{
    struct proc_slot tt_proc[JOS_NCPU];
    thread_mutex_t   tt_proc_mu;
    jos_atomic64_t   tt_thread_count;
    uint64_t	     tt_id_counter;
    struct spinlock  tt_id_counter_lock;

    LIST_HEAD(map_slot_list, map_slot) map_slot_free_list;
    struct spinlock map_slot_lock;
    struct map_slot map_slot[max_threads];
};
static struct thread_table *thread_table = (struct thread_table *) UTHREADTABLE;

/* Processor+AS local thread_context for current thread */
static struct thread_context *local_cur_tc;
/* Processor+AS local thread_queue */
static struct thread_queue *local_queue;
/* Processor+AS local unused (but still not freed) thread_context */
static struct thread_context *local_cache_tc;

int *
__errno_location(void)
{
    // For dietlibc support
    static int x;
    if (local_cur_tc)
        return &local_cur_tc->tc_errno;
    return &x;
}

thread_id_t
thread_id(void)
{
    if (local_cur_tc)
	return local_cur_tc->tc_id;
    return 1;
}

static struct map_slot *
map_slot_alloc(void)
{
    spin_lock(&thread_table->map_slot_lock);
    struct map_slot *slot = LIST_FIRST(&thread_table->map_slot_free_list);
    if (slot)
	LIST_REMOVE(slot, map_list_link);
    spin_unlock(&thread_table->map_slot_lock);
    return slot;
}

static void
map_slot_free(struct map_slot *slot)
{
    slot->cached = thread_cache;
    spin_lock(&thread_table->map_slot_lock);
    LIST_INSERT_HEAD(&thread_table->map_slot_free_list, slot, map_list_link);
    spin_unlock(&thread_table->map_slot_lock);
}

static uint64_t
thread_id_alloc(void)
{
    spin_lock(&thread_table->tt_id_counter_lock);
    uint64_t ret = ++thread_table->tt_id_counter;
    spin_unlock(&thread_table->tt_id_counter_lock);

    if (ret & UINT64(0xFF00000000000000))
	panic("No more thread ids on pid %u\n", core_env->pid);

    uint64_t high = core_env->pid;
    ret |= (high << 56);

    if (thread_alloc_debug)
	cprintf("thread_id_alloc: id %lx cpu %u\n", 
		ret, processor_current_procid());
    return ret;
}

static void
thread_set_name(struct thread_context *tc, const char *name)
{
    strncpy(tc->tc_name, name, name_size - 1);
    tc->tc_name[name_size - 1] = 0;
}

static void
thread_free_unmap(struct thread_context *tc)
{
    map_slot_free(tc->tc_slot);

    if (thread_cache)
	return;
    
    as_unmap(tc->tc_stack_bottom);
    assert(sys_share_unref(tc->tc_stack_sg) == 0);
    struct sobj_ref sg = tc->tc_sg;
    as_unmap(tc);
    assert(sys_share_unref(sg) == 0);
}

static int
thread_alloc_map(struct thread_context **tc)
{
    struct map_slot *slot = map_slot_alloc();
    if (!slot)
	return -E_NO_SPACE;

    if (slot->cached) {
	*tc = slot->va;
	return 0;
    }
	
    
    struct thread_context *new_tc = slot->va;
    struct sobj_ref tc_sg;
    int r = segment_alloc(core_env->sh, sizeof(*new_tc), 
			  &tc_sg, (void **)&new_tc, 
			  SEGMAP_READ | SEGMAP_WRITE, "thread-context", 
			  core_env->pid);
    if (r < 0) {
	map_slot_free(slot);
	return r;
    }
    memset(new_tc, 0, sizeof(*new_tc));

    void *stack_bottom = slot->stack;
    struct sobj_ref stack_sg;
    r = segment_alloc(core_env->sh, stack_size, 
		      &stack_sg, &stack_bottom, 
		      SEGMAP_READ | SEGMAP_WRITE, "thread-stack",
		      core_env->pid);
    if (r < 0) {
	as_unmap(new_tc);
	assert(sys_share_unref(tc_sg) == 0);
	map_slot_free(slot);
	return r;
    }

    new_tc->tc_slot = slot;
    new_tc->tc_sg = tc_sg;
    new_tc->tc_stack_sg = stack_sg;
    new_tc->tc_stack_bottom = stack_bottom;
    *tc = new_tc;

    return 0;
}

static int
thread_bs_proc(const char *name, void (*entry)(uint64_t arg), uint64_t arg)
{
    struct sobj_ref queue_sg;
    local_queue = 0;
    int r = segment_alloc(core_env->sh, sizeof(*local_queue), 
			  &queue_sg, (void *)&local_queue, 
			  0, "thread-queue", core_env->pid);
    if (r < 0)
	return r;

    threadq_init(local_queue);
    
    uint64_t pid = processor_current_procid();
    thread_table->tt_proc[pid].sg_id = queue_sg;
    thread_table->tt_proc[pid].ps_id = processor_current();
    thread_table->tt_proc[pid].as_id = processor_current_as();
    
    r = thread_create(0, name, entry, arg);
    if (r < 0) {
	as_unmap(local_queue);
	sys_share_unref(queue_sg);
	return r;
    }
    return 0;
}

int
thread_init(void (*entry)(uint64_t arg), uint64_t arg)
{
    // Assume only one thread when init is called
    static char init = 0;
    if (init)
        return -E_INVAL;

    uint64_t context_slot_num = (UTHREADEND - UTHREADSTART) / 
	ROUNDUP(sizeof(struct thread_context), PGSIZE);
    uint64_t stack_slot_num = (USTACKEND - USTACKSTART) / 
	(stack_size + PGSIZE);

    if ((max_threads > context_slot_num) || max_threads > stack_slot_num)
	panic("insuffcient space to support %u threads", max_threads);

    if (thread_init_debug)
	cprintf("thread_init: %ld context slots, %ld stack slots, "
		"supporting %u threads\n", 
		context_slot_num, stack_slot_num, max_threads);

    // Init the global thread_table
    struct sobj_ref tt_sg;
    int r = segment_alloc(core_env->sh, sizeof(*thread_table), &tt_sg, 
			  (void **)&thread_table, 0, "thread-table", 
			  core_env->pid);
    
    if (r < 0)
	return r;
    
    memset(thread_table, 0, sizeof(*thread_table));
    thread_mutex_init(&thread_table->tt_proc_mu);
    spin_init(&thread_table->tt_id_counter_lock);
    
    // Setup free list of address ranges for thread contexts and stacks
    spin_init(&thread_table->map_slot_lock);
    LIST_INIT(&thread_table->map_slot_free_list);
    uint64_t tc_va = UTHREADSTART;
    uint64_t tc_bytes = ROUNDUP(sizeof(struct thread_context), PGSIZE);
    uint64_t stack_va = USTACKSTART + PGSIZE;
    uint64_t stack_bytes = stack_size + PGSIZE;
    for (uint64_t i = 0; i < max_threads; 
	 i++, tc_va += tc_bytes, stack_va += stack_bytes) 
    {
	LIST_INSERT_HEAD(&thread_table->map_slot_free_list, 
			 &thread_table->map_slot[i], 
			 map_list_link);
	thread_table->map_slot[i].va = (void *)tc_va;
	thread_table->map_slot[i].stack = (void *)stack_va;
    }

    r = thread_bs_proc("init-thread", entry, arg);
    if (r < 0) {
	as_unmap(thread_table);
	sys_share_unref(tt_sg);
	return r;
    }

    init = 1;
    thread_yield();
    cprintf("thread_init: thread_yield returned!\n");
    return -E_INVAL;
}

void __attribute__((noreturn))
thread_halt(void)
{
    assert(local_cur_tc);
    jos_atomic_dec64(&thread_table->tt_thread_count);

    for (int i = 0; i < local_cur_tc->tc_nonhalt; i++)
	local_cur_tc->tc_onhalt[i]();
    local_cur_tc->tc_nonhalt = 0;

    if (local_cache_tc)
	thread_free_unmap(local_cache_tc);
    local_cache_tc = local_cur_tc;
    local_cur_tc = 0;

    while (jos_atomic_read(&thread_table->tt_thread_count))
	thread_yield();

    if (do_halt) {
	cprintf("thread_halt: no more threads, halting Processor.\n");
	processor_halt();
    } else {
	cprintf("thread_halt: no more threads...\n");
	for (;;);
    }
}

static void __attribute__((noreturn))
thread_entry(void)
{
    local_cur_tc->tc_entry(local_cur_tc->tc_entry_arg);
    thread_halt();
}

int
thread_onhalt(void (*onhalt)(void))
{
    if (local_cur_tc->tc_nonhalt == array_size(local_cur_tc->tc_onhalt))
	return -E_NO_SPACE;

    local_cur_tc->tc_onhalt[local_cur_tc->tc_nonhalt++] = onhalt;
    return 0;
}

int
thread_pfork(const char *name, void (*entry)(uint64_t), uint64_t flag, struct sobj_ref *shares, int nr_shares,
    uint64_t arg, proc_id_t pid)
{
    int64_t r = pforkv(pid, flag, shares, nr_shares);
    if (r < 0)
	return r;
    if (r)
	return 0;

    // forked thread
    local_cur_tc = 0;
    local_queue = 0;
    local_cache_tc = 0;
    
    r = thread_bs_proc(name, entry, arg);
    if (r < 0) {
	cprintf("thread_pfork: thread_bs_proc failed %s\n", e2s(r));
	processor_halt();
    }
    
    thread_yield();
    cprintf("thread_pfork: thread_yield returned!\n");
    return -E_INVAL;
}

int
thread_create(thread_id_t *tid, const char *name, void (*entry)(uint64_t), uint64_t arg)
{
    struct thread_context *tc = 0;
    int r = thread_alloc_map(&tc);
    if (r < 0)
	return r;
    thread_set_name(tc, name);
    tc->tc_id = thread_id_alloc();
    
    void *stacktop = tc->tc_stack_bottom + stack_size;
    // AMD64 ABI requires 16-byte alignment before "call" instruction
    stacktop = ROUNDDOWN(stacktop, 16) - 8;
    // Terminate stack unwinding
    memset(stacktop, 0, 8);
    
    memset(&tc->tc_buf, 0, sizeof(tc->tc_buf));
    tc->tc_buf.jb_rsp = (uint64_t)stacktop;
    tc->tc_buf.jb_rip = (uint64_t)&thread_entry;
    tc->tc_entry = entry;
    tc->tc_entry_arg = arg;
    jos_atomic_inc64(&thread_table->tt_thread_count);
    threadq_push(local_queue, tc);

    if (tid)
	*tid = tc->tc_id;
    return 0;
}

void
thread_yield(void)
{   
    if (!local_queue)
	return;

    struct thread_context *next = threadq_pop(local_queue);
    if (!next)
	return;
   
    if (local_cur_tc) {
        if (jos_setjmp(&local_cur_tc->tc_buf) != 0) {
	    return;
	}
	threadq_push(local_queue, local_cur_tc);
    }

    local_cur_tc = next;
    jos_longjmp(&local_cur_tc->tc_buf, 1);
}

void
thread_wait(volatile uint64_t *addr, uint64_t val, uint64_t nsec)
{
    // XXX bad time, not multicore safe, 
    // should convert nsec to ticks-to-wait, then rely on time 
    // function to wake up
    uint64_t s = time_nsec();
    uint64_t p = s;
    local_cur_tc->tc_wait_addr = addr;
    local_cur_tc->tc_wakeup = 0;

    while (p < nsec) {
	if (p < s)
	    break;
	if (addr && *addr != val)
	    break;
	if (local_cur_tc->tc_wakeup)
	    break;

	thread_yield();
	p = time_nsec();
    }

    local_cur_tc->tc_wait_addr = 0;
    local_cur_tc->tc_wakeup = 0;
}

void
thread_wakeup(volatile uint64_t *addr)
{
    // XXX not multicore safe, slow to check entire queue
    struct thread_context *tc = local_queue->tq_first;
    while (tc) {
	if (tc->tc_wait_addr == addr)
	    tc->tc_wakeup = 1;
	tc = tc->tc_queue_link;
    }
}

void
thread_mutex_init(thread_mutex_t *mu)
{
    jos_atomic_set64(mu, 0);
}

void
thread_mutex_lock(thread_mutex_t *mu)
{
    for (;;) {
	uint64_t cur = jos_atomic_compare_exchange64(mu, 0, 1);
	if (cur == 0)
	    break;
	
	thread_yield();
    }
}

int
thread_mutex_trylock(thread_mutex_t *mu)
{
    uint64_t cur = jos_atomic_compare_exchange64(mu, 0, 1);
    if (cur == 0)
	return 0;

    return -1;
}

void
thread_mutex_unlock(thread_mutex_t *mu)
{
    uint64_t was = jos_atomic_compare_exchange64(mu, 1, 0);
    if (was == 0) {
	cprintf("%p\n", __builtin_return_address(0));
	panic("thread_mutex_unlock: %p not locked", mu);
    }
}
