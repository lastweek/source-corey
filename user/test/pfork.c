#include <machine/x86.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <test.h>

void
pfork_test(void)
{
    int64_t ps_id = pfork(1);
    if (ps_id < 0) {
        cprintf("pfork error: %s\n", e2s(ps_id));
    } else if (ps_id == 0) {
        for (uint64_t i = 0; i < 300000; i++)
            nop_pause();
        cprintf("hello from fork\n");
	processor_halt();
    } else {
        for (uint64_t i = 0; i < 1000000; i++)
            nop_pause();
        cprintf("hello from parent\n");
        for (uint64_t i = 0; i < 1000000; i++)
            nop_pause();
    }
}
