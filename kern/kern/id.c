#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/id.h>
#include <inc/intmacro.h>
#include <inc/pad.h>

static PAD_TYPE(uint64_t, JOS_CLINE) id_counter[JOS_NCPU];

kobject_id_t
id_alloc(void)
{
    static_assert(JOS_NCPU < 256);
    uint64_t cpu = arch_cpu();
    uint64_t x = ++id_counter[cpu].val;
    if (x & UINT64(0xFF00000000000000))
	panic("No more ids on cpu %lu :(", cpu);
    return x | (cpu << 56);
}
