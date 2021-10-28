#ifndef JOS_MACHINE_PROC_H
#define JOS_MACHINE_PROC_H

#include <machine/types.h>
#include <machine/pmap.h>
#include <machine/setjmp.h>

struct cpu 
{
    uint8_t apicid;            // Local APIC ID
    uint8_t cpuid;	       // Kernel CPU ID
    uint8_t nodeid;
    volatile uint8_t booted;   // Has the CPU completed booting?

    char	       uaccess_start;
    struct jos_jmp_buf uaccess_buf;
};

// bootdata.c
void cpu_init_pmaps(void);

#endif
