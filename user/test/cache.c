#include <machine/x86.h>
#include <machine/mmu.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/array.h>
#include <inc/pad.h>
#include <test.h>

#include <string.h>

static proc_id_t cpu[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static struct {
    volatile PAD_TYPE(uint64_t, JOS_CLINE) count[JOS_NCPU];
    char buf[4 * PGSIZE];
} *shared_state;

static struct {
    char buf[4 * PGSIZE];
} private_state;

static void __attribute__((unused, noreturn))
read_test(void)
{
    int pid = processor_current_procid();
    for (;;) {
	memcpy(private_state.buf, shared_state->buf, sizeof(private_state.buf));
	char *a = private_state.buf;
	while ((a = strchr(a, 'a')));
	shared_state->count[pid].val++;
    }
}

static void  __attribute__((unused, noreturn))
write_test(void)
{
    int pid = processor_current_procid();
    for (;;) {
	memset(shared_state->buf, pid, sizeof(private_state.buf));
	shared_state->count[pid].val++;
    }
}

void
cache_test(void)
{
    enum { iters = 100 };
    // Allocate a piece of memory from default node
    int64_t r = segment_alloc(core_env->sh, sizeof(*shared_state), 0,
			      (void **)&shared_state, SEGMAP_SHARED, 
			      "shared-state", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));

#if 0
    uint64_t s = read_tsc();
    while (shared_state->count[0].val < iters) {
	memcpy(private_state.buf, shared_state->buf, sizeof(private_state.buf));
	char *a = private_state.buf;
	while ((a = strchr(a, 'a')));
	shared_state->count[0].val++;
    }
    cprintf("private end: cycles %lu iterations %u\n", read_tsc() - s, iters);
    shared_state->count[0].val = 0;
#endif

    uint64_t s = read_tsc();
    while (shared_state->count[0].val < iters) {
	memset(shared_state->buf, 0xFF, sizeof(private_state.buf));
	shared_state->count[0].val++;
    }
    cprintf("private end: cycles %lu iterations %u\n", read_tsc() - s, iters);
    shared_state->count[0].val = 0;

    for (uint32_t i = 0; i < array_size(cpu); i++) {
	r = pfork(cpu[i]);
	if (r < 0)
	    panic("unable to pfork onto %u: %s", cpu[i], e2s(r));
	if (r == 0)
	    write_test();
	    //read_test();

	while(shared_state->count[cpu[i]].val == 0);
	cprintf("started one on %u\n", cpu[i]);
    }


    s = read_tsc();
    while (shared_state->count[0].val < iters) {
	memset(shared_state->buf, 0xFF, sizeof(private_state.buf));
	shared_state->count[0].val++;
    }
    cprintf("sharead end: cycles %lu iterations %u\n", read_tsc() - s, iters);
    shared_state->count[0].val = 0;

#if 0
    s = read_tsc();
    while (shared_state->count[0].val < iters) {
	memcpy(private_state.buf, shared_state->buf, sizeof(private_state.buf));
	char *a = private_state.buf;
	while ((a = strchr(a, 'a')));
	shared_state->count[0].val++;
    }
    cprintf("shared end: cycles %lu iterations %u\n", read_tsc() - s, iters);
#endif
}
