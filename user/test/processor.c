#include <machine/mmu.h>
#include <inc/setjmp.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/context.h>
#include <inc/utrap.h>
#include <test.h>
#include <string.h>

enum { self_vector_iters = 1000 };
enum { revector_iters = 100 };
enum { leak_iters = 100 };

static void __attribute__((noreturn, regparm(1)))
vector_stub(struct jos_jmp_buf *jb)
{
    jos_longjmp(jb, 1);
}

static void __attribute__((unused))
processor_self_vector_test(void)
{
    cprintf("Test processor self vector\n");
    
    struct sobj_ref atref = processor_current_as();
    
    volatile int i = 0;
    struct jos_jmp_buf jb;
    if (jos_setjmp(&jb) != 0) {
	i++;
	if ((i % 200) == 0)
	    cprintf(" %d\n", i);
	if (i == self_vector_iters) {
	    cprintf("Test self vector done!\n");
	    return;
	}
    }

    uint8_t stack[128];
    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = atref;
    uc.uc_entry = (void *) &vector_stub;
    uc.uc_stack = (void *) stack;
    uc.uc_arg[0] = (uintptr_t) &jb;
    
    echeck(sys_processor_vector(processor_current(), &uc));    
}

static volatile uint64_t timer_ticks;

static void
utrap_handler(struct UTrapframe *utf)
{
    timer_ticks++;
}

static void __attribute__((unused))
processor_inttimer_test(void)
{
    cprintf("Test Processor interval timer\n");
    utrap_set_handler(utrap_handler);
    echeck(sys_processor_set_interval(processor_current(), 100));
    while (timer_ticks < 100) ;
    echeck(sys_processor_set_interval(processor_current(), 0));
    utrap_set_handler(0);
    cprintf("Test interval timer done!\n");
}

void
processor_test(void)
{
    processor_inttimer_test();
    //processor_self_vector_test();
    pfork_test();
}
