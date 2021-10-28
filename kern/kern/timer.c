#include <machine/param.h>
#include <kern/timer.h>
#include <kern/lib.h>
#include <inc/pad.h>

struct time_source *the_timesrc;

static PAD_TYPE(struct interval_timer *, JOS_CLINE) inttimer[JOS_NCPU];

void
timer_delay(uint64_t nsec)
{
    if (the_timesrc)
	the_timesrc->delay_nsec(the_timesrc->arg, nsec);
    else
	panic("no timer source");
}

void
timer_interval_register(proc_id_t pid, struct interval_timer *it)
{
    inttimer[pid].val = it;
}

void
timer_interval_time(proc_id_t pid, uint64_t hz)
{
    if (!inttimer[pid].val) {
	if (hz)
	    panic("no hardware timer for CPU %u\n", pid);
	return;
    }
    inttimer[pid].val->interval_time(inttimer[pid].val->arg, pid, hz);
}
