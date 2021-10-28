#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/syscall.h>
#include <inc/array.h>
#include <test.h>

#include <malloc.h>
#include <errno.h>
#include <string.h>

#include <defconfig.h>
#include <cusconfig.h>

int
main(void)
{
    cprintf("test OS\n");

    uint32_t i = 0;
    for (; run_custom_tests && i < array_size(custom_tests); i++) {
	cprintf("%03u test: %s\n", i, custom_tests[i].name);
	custom_tests[i].func();
    }
	
    for (; run_default_tests && i < array_size(tests); i++) {
	cprintf("%03u test: %s\n", i, tests[i].name);
	tests[i].func();
    };

    cprintf("test OS all done!\n");
}
