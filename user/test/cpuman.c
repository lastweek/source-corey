#include <inc/cpuman.h>
#include <string.h>
#include <test.h>

#define SAME_DIE(a, b) (a != b && (a & -ncores_pd) == (b & -ncores_pd))
#define SAME_CHIP(a, b) (a != b && (a & -ncores_pc) == (b & -ncores_pc))

static void
test_any(struct cpu_state *cs)
{
    memset(cs, 0, sizeof(struct cpu_state));
    cpu_man_init(cs);
    proc_id_t pid;
    for (int i = 0; i < JOS_NCPU - 1; i++) {
	pid = cpu_man_any(cs);
	if (ALLOC_FAILED(pid) || (pid == default_pid))
	    panic("cpu_alloc_any failed");
    }
    pid = cpu_man_any(cs);
    if (!ALLOC_FAILED(pid))
	panic("cpu_alloc_any should fail");
}

static void
test_nearby(struct cpu_state *cs)
{
    memset(cs, 0, sizeof(struct cpu_state));
    cpu_man_init(cs);
    proc_id_t pid = cpu_man_nearby(cs, default_pid);
    if (!SAME_DIE(pid, default_pid))
	panic
	    ("cpu_man_nearby failed: should return core on the same die %d, %d",
	     pid, default_pid);
    for (int i = 0; i < ncores_pc - ncores_pd; i++) {
	pid = cpu_man_nearby(cs, default_pid);
	if (SAME_DIE(pid, default_pid) || !SAME_CHIP(pid, default_pid))
	    panic
		("cpu_man_nearby failed: should return core on the same chip");
    }
    for (int i = 0; i < JOS_NCPU - ncores_pc; i++) {
	pid = cpu_man_nearby(cs, default_pid);
	if (ALLOC_FAILED(pid) || SAME_CHIP(pid, default_pid))
	    panic
		("cpu_man_nearby failed: should return core on different chips");
    }
    if (!ALLOC_FAILED(cpu_man_nearby(cs, default_pid)))
	panic("cpu_man_nearby failed: should fail to allocate any core");
}

static void
test_remote(struct cpu_state *cs)
{
    memset(cs, 0, sizeof(struct cpu_state));
    cpu_man_init(cs);
    proc_id_t pid;
    for (int i = 0; i < ncores_pc - 1; i++) {
	pid = cpu_man_remote(cs, 0, 0);
	if (ALLOC_FAILED(pid))
	    panic
		("cpu_man_remote failed: should allocate cores on same chip");
    }
    for (int i = 0; i < ncores_pc * 2; i++) {
	pid = cpu_man_remote(cs, 0, 1);
	if (ALLOC_FAILED(pid))
	    panic
		("cpu_man_remote failed: should allocate cores on same chip");
    }
    for (int i = 0; i < ncores_pc; i++) {
	pid = cpu_man_remote(cs, 0, 2);
	if (ALLOC_FAILED(pid))
	    panic
		("cpu_man_remote failed: should allocate cores on same chip");
    }
}

void
cpuman_test()
{
    struct cpu_state *cs;
    int64_t r = segment_alloc(default_share, sizeof(struct cpu_state), 0,
			      (void **) &cs, SEGMAP_SHARED, "cpu state",
			      default_pid);
    if (r < 0)
	panic("fail to allocate cpu state");
    test_any(cs);
    test_nearby(cs);
    test_remote(cs);
}
