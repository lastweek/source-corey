#include <test.h>
#include <inc/lib.h>
#include <inc/spinlock.h>
#include <inc/compiler.h>

static volatile uint64_t x1 JSHARED_ATTR;
static volatile uint64_t x2 JSHARED_ATTR = 2;
static struct spinlock *lock;

static uint64_t
read_tsc(void)
{
    uint32_t a, d;
    __asm __volatile("rdtsc":"=a"(a), "=d"(d));
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}

void
elf_test()
{
    int64_t r = segment_alloc(core_env->sh, sizeof(*lock), 0, (void **) &lock,
			      SEGMAP_SHARED, "elf_test lock", core_env->pid);
    if (r < 0) {
	panic("failed to allocate the spinlock\n");
    }
    spin_init(lock);
    x1 = 0;

    proc_id_t pid = pfork(1);
    if (pid == 0) {
	spin_lock(lock);
	x1++;
	x2--;
	spin_unlock(lock);
	while (x1 != 2 && x2) ;
	processor_halt();
    } else {
	spin_lock(lock);
	x1++;
	x2--;
	spin_unlock(lock);
	while (x1 != 2 && x2) ;
    }
    uint64_t t = read_tsc();
    while (read_tsc() - t < 1000000000) ;
    cprintf("elf test done!\n");
}
