#include <machine/proc.h>
#include <machine/x86.h>
#include <kern/arch.h>
#include <kern/lib.h>
#include <dev/lapic.h>

uint64_t 
karch_get_tsc(void)
{
    return read_tsc();
}

void
arch_pause(void)
{
    nop_pause();
}

void
arch_fp_init(struct Fpregs *fpregs)
{
    // Linux says so.
    memset(fpregs, 0, sizeof(*fpregs));
    fpregs->cwd = 0x37f;
    fpregs->mxcsr = 0x1f80;
}
