#include <kern/arch.h>
#include <kern/uaccess.h>
#include <inc/setjmp.h>
#include <inc/error.h>

// NB:
//   Cannot access user data when holding a lock required to 
//   handle a page fault.
// 
//   Locks acquired nested in a uaccess_start should be released
//   before the user data is accessed.

int
uaccess_start(void)
{
    struct cpu *c = &cpus[arch_cpu()];
    assert(!c->uaccess_start);
    c->uaccess_start = 1;

    // XXX Rely on gcc to optimize the tail call
    return jos_setjmp(&c->uaccess_buf);
}

void
uaccess_stop(void)
{
    struct cpu *c = &cpus[arch_cpu()];
    assert(c->uaccess_start);
    c->uaccess_start = 0;
}

void
uaccess_error(void)
{
    struct cpu *c = &cpus[arch_cpu()];
    assert(c->uaccess_start);
    c->uaccess_start = 0;    
    jos_longjmp(&c->uaccess_buf, -E_INVAL);
}

int
uaccess_enabled(void)
{
    struct cpu *c = &cpus[arch_cpu()];
    return c->uaccess_start;
}
