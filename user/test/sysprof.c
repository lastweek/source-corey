#include <test.h>
#include <inc/stdio.h>
#include <inc/sysprof.h>
#include <inc/arch.h>

void
sysprof_test(void)
{
    sysprof_init();
    sysprof_prog_l3miss(0);

    uint64_t sl3 = sysprof_rdpmc(0);
    
    uint64_t s = read_tsc();
    while (read_tsc() - s < 10000000);
    
    
    cprintf("l3 miss %ld\n", sysprof_rdpmc(0) - sl3);
}
