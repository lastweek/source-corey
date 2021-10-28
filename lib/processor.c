#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/context.h>
#include <inc/stdio.h>
#include <inc/assert.h>

struct sobj_ref
processor_current(void)
{
    return core_env->psref;
}

proc_id_t
processor_current_procid(void)
{
    return core_env->pid;
}

struct sobj_ref
processor_current_as(void)
{
    struct sobj_ref asref;
    assert(sys_self_get_as(&asref) == 0);
    return asref;
}

void
processor_halt(void)
{
    sys_processor_halt(processor_current());
    cprintf("processor_halt: still running?");
    for (;;);
}

uint64_t
processor_ncpu(void)
{
    struct u_locality_matrix ulm;
    assert(sys_locality_get(&ulm) == 0);
    return ulm.ncpu;
}

