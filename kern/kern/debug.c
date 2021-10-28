#include <kern/lib.h>
#include <kern/debug.h>
#include <kern/prof.h>
#include <machine/perfmon.h>
#include <inc/error.h>

static int
debug_nop(uint64_t *a)
{
    cprintf("debug_nop: %lu %lu %lu %lu %lu %lu\n",
	    a[0], a[1], a[2], a[3], a[4], a[5]);
    return 0;
}

static int
hw_counter(uint64_t *a)
{
    return perfmon_set(a[0], a[1]);
}

static int
debug_prof(uint64_t *a)
{
    switch (a[0]) {
    case kprof_disable:
	prof_set_enable(0);
	break;
    case kprof_enable:
	prof_set_enable(1);
	break;
    case kprof_print:
	prof_print();
	break;
    case kprof_reset:
	prof_reset();
	break;
    default:
	cprintf("debug_prof: unknown 0x%lx\n", a[0]);
	return -E_INVAL;
    }

    return 0;
}

static int (*debug_func[])(uint64_t *) = {
    [kdebug_nop] = debug_nop,
    [kdebug_hw]  = hw_counter,
    [kdebug_prof] = debug_prof,
};

int 
debug_call(kdebug_op_t op, uint64_t a0, uint64_t a1, uint64_t a2, 
	   uint64_t a3, uint64_t a4, uint64_t a5) 
{
    uint64_t args[6] = { a0, a1, a2, a3, a4, a5 };
    
    if (op >= array_size(debug_func) || !debug_func[op]) {
	cprintf("debug_call: bad op %u\n", op);
	return -E_INVAL;
    }
    return debug_func[op](args);
}
