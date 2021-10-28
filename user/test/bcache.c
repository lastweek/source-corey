#include <machine/param.h>
#include <machine/x86.h>
#include <inc/syscall.h>
#include <inc/fs.h>
#include <inc/lib.h>
#include <inc/sysprof.h>
#include <test.h>

#include <string.h>

enum { do_small_read = 1 };
enum { do_small_write = 1 };

enum { max_core_count = 16 };
enum { small_bytes = 4 * 1024 };
enum { small_read_iters = 1000000 };
enum { small_write_iters = 100000 };

enum { test_small_read, test_small_write };
const char *test_str[] = { 
    [test_small_read] = "small read",
    [test_small_write] = "small write",
};

static struct {
    volatile char start;
    volatile char done;
    
    union {
	struct {
	    volatile uint64_t count;
	    struct sysprof_arg parg;
	};
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU] __attribute__((aligned(JOS_CLINE)));
} *shared;

static void
do_read(struct fs_handle file)
{
    char buf[small_bytes];

    if (core_env->pid != 0)
	while (!shared->start);
    else
	shared->start = 1;

    //sysprof_refill(&shared->cpu[core_env->pid].parg);
    while (!shared->done) {
	int64_t r = fs_pread(&file, buf, small_bytes, 0);
	assert(r == small_bytes);
	++shared->cpu[core_env->pid].count;
	if (core_env->pid == 0 && shared->cpu[core_env->pid].count == small_read_iters)
	    shared->done = 1;
    }
    //sysprof_refill_end(&shared->cpu[core_env->pid].parg);
}

static void
do_write(struct fs_handle file)
{
    char buf[small_bytes];

    if (core_env->pid != 0)
	while (!shared->start);
    else
	shared->start = 1;
  
    //sysprof_refill(&shared->cpu[core_env->pid].parg);
    while (!shared->done) {
	int64_t r = fs_pwrite(&file, buf, small_bytes, 0);
	assert(r == small_bytes);
	++shared->cpu[core_env->pid].count;
	if (core_env->pid == 0 && 
	    shared->cpu[core_env->pid].count == small_write_iters)
	    shared->done = 1;
    }
    //sysprof_refill_end(&shared->cpu[core_env->pid].parg);
}

static void
do_op(int test_type, struct fs_handle file)
{
    switch (test_type) {
    case test_small_read:
	return do_read(file);
    case test_small_write:
	return do_write(file);
    default:
	panic("bad test type: %d\n", test_type);
    }
}

static void
do_test(int test_type, int core_count, struct fs_handle file)
{
    for (int i = 1; i < core_count; i++) {
	int64_t r = pfork(i);
	assert(r >= 0);
	if (r == 0) {
	    do_op(test_type, file);
	    processor_halt();
	}
    }
    
    // make sure everyone has started...
    uint64_t x = read_tsc();
    while (read_tsc() - x < 10000);
    
    uint64_t s = read_tsc();
    do_op(test_type, file);
    uint64_t e = read_tsc();
    uint64_t usec = (e - s) * 1000000 / core_env->cpufreq;
    
    uint64_t total = 0;
    for (int i = 0; i < JOS_NCPU; i++)
	total += shared->cpu[i].count;
    
    cprintf("%2u %s: %10lu in %10lu usec %10lu per/sec\n", 
	    core_count, test_str[test_type], total, 
	    usec, (total * 1000000) / usec);
}

static void
print_hw_counter(int core_count)
{
#if 0
    for (int i = 0; i < core_count; i++) {
	cprintf(" %2u ", i);
	sysprof_print(&shared->cpu[i].parg);
    }
#endif

    uint64_t refill_tot = 0;
    for (int i = 0; i < core_count; i++)
	refill_tot += shared->cpu[i].parg.gen_refill.cnt;
    cprintf("total: refill %16lu\n", refill_tot);
}

void
bcache_test(void)
{
    sysprof_init();
    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));

    struct fs_handle dir, file;
    echeck(fs_namei("/x", &dir));
    echeck(fs_create(&dir, "foo", &file));

    char buf[small_bytes];
    char buf2[small_bytes];

    memset(buf, 0xab, sizeof(buf));
    memset(buf2, 0, sizeof(buf));
    echeck(fs_pwrite(&file, buf, sizeof(buf), 0));
    echeck(fs_pread(&file, buf2, sizeof(buf2), 0));
    assert(!memcmp(buf, buf2, sizeof(buf)));

    if (do_small_read) {
	for (int i = 1; i <= max_core_count; i++) {
	    memset(shared, 0, sizeof(*shared));
	    do_test(test_small_read, i, file);
	    // make sure other processors have halted...
	    uint64_t x = read_tsc();
	    while (read_tsc() - x < 1000000000);
	    print_hw_counter(i);
	}
    }

    if (do_small_write) {
	for (int i = 1; i <= max_core_count; i++) {
	    memset(shared, 0, sizeof(*shared));
	    do_test(test_small_write, i, file);
	    // make sure other processors have halted...
	    uint64_t x = read_tsc();
	    while (read_tsc() - x < 1000000000);
	    print_hw_counter(i);
	}
    }
}
