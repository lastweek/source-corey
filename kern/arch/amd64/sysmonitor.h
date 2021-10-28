#ifndef JOS_ARCH_MONITOR_H
#define JOS_ARCH_MONITOR_H

#include <machine/x86.h>

static __inline __attribute__((always_inline, no_instrument_function))
void
monitor_wait(volatile uint64_t *va, uint64_t value)
{
    while (*va != value) ;
    return;
    
    // it is faster on the AMD 4x4 to spinwait
#if 0
    // monitor wait loop specified in AMD Arch. Manual Vol. 3
    while (*va != value) {
	monitor((uintptr_t)va, 0, 0);
	if (*va != value)
	    mwait(0, 0);
    }
#endif
}

#endif
