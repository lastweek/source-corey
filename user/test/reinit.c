#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/arch.h>
#include <test.h>

#include <string.h>

void __attribute__((noreturn))
reinit_test(void)
{
    cprintf("boot_args: %s\n", boot_args);

    uint64_t s = arch_read_tsc();
    while (1000000000 > arch_read_tsc() - s);
    
    int64_t r = pfork(1);
    if (r < 0)
	panic("pfork error: %s", e2s(r));

    if (r) {
	cprintf("parent on 0\n");
	for (;;);
    } else {
	cprintf("child on 1\n");
	for (;;);
    }
}
