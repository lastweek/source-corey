#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/assert.h>

#include <stdlib.h>
#include <unistd.h>

libc_hidden_proto(abort)
libc_hidden_proto(_exit)

void __attribute__((noreturn))
abort(void)
{
    // uclibc abort is dependent on a bunch of signal code
    cprintf("abort:\n");
    print_backtrace();
    processor_halt();
}

extern void __thread_doexit(int doexit);
void 
__thread_doexit(int doexit)
{
    cprintf("__thread_doexit: not implemented\n");
}

void 
_exit(int status)
{
    panic("not implemented");
}

libc_hidden_def(abort)
libc_hidden_def(_exit)
