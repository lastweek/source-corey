#ifndef JOS_KERN_TIMER_H
#define JOS_KERN_TIMER_H

#include <machine/types.h>
#include <inc/proc.h>

enum {
    time_source_pit,
};

struct time_source {
    int type;
    uint64_t freq_hz;
    void *arg;
    uint64_t (*ticks) (void *);
    void (*delay_nsec) (void *, uint64_t);
};

struct interval_timer {
    void *arg;
    void (*interval_intr) (void *);
    void (*interval_time) (void *, proc_id_t, uint64_t);
};

extern struct time_source *the_timesrc;

void timer_delay(uint64_t nsec);

void timer_interval_register(proc_id_t pid, struct interval_timer *it);
void timer_interval_time(proc_id_t pid, uint64_t hz);

#endif
