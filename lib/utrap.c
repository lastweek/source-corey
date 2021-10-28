#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/utrap.h>
#include <inc/memlayout.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>

static void (*handler) (struct UTrapframe *);

void
utrap_set_handler(void (*fn) (struct UTrapframe *))
{
    handler = fn;
}

void __attribute__((noreturn, JOS_UTRAP_GCCATTR))
utrap_entry(struct UTrapframe *utf)
{
    if (handler) {
	handler(utf);
    } else {
	cprintf("utrap_entry: unhandled trap num %d\n", utf->utf_trap_num);
	processor_halt();
    }

    utrap_ret(utf);
}

int
utrap_init(void)
{
    void *va = 0;
    int r = segment_alloc(core_env->sh, PGSIZE, 0, &va, 0, 
			  "utrap-seg", core_env->pid);
    if (r < 0) {
	cprintf("utrap_init: cannot alloc segment: %s\n", e2s(r));
	return r;
    }

    r = as_set_utrap(&utrap_entry_asm, va, va + PGSIZE);
    if (r < 0) {
	cprintf("utrap_init: cannot set trap entry: %s\n", e2s(r));
	return r;
    }

    return 0;
}
