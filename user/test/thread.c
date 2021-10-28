#include <machine/x86.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <test.h>

#include <malloc.h>

static void  __attribute__((unused))
xthread(uint64_t a)
{
    volatile uint64_t *x = (uint64_t *)a;
    *x = 1;
}

void
thread_test(void)
{
    enum { spawn_iters = 1000 };
    volatile uint64_t x;

    cprintf("Test thread spawn&cleanup %d times\n", spawn_iters);
    uint64_t s = read_tsc();
    for (int i = 0; i < spawn_iters; i++) {
	x = 0;
	echeck(thread_create(0, "test test", xthread, (uint64_t)&x));
        while (!x)
            thread_yield();
    }
    
    cprintf("Test thread: %lu cycles per create\n", 
	    (read_tsc() - s) / spawn_iters);
}
