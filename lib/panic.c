#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>

void
_panic(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    
    // Print the panic message
    cprintf("libos panic at %s:%d: ", file, line);
    vcprintf(fmt, ap);
    cprintf("\n");
    
    // XXX backtrace

    processor_halt();
    cprintf("_panic: did not halt!\n");
    while (1) ;
}

