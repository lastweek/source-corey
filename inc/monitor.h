#ifndef JOS_INC_MONITOR_H
#define JOS_INC_MONITOR_H

#include <machine/sysmonitor.h>

typedef enum monitor_call_enum {
    mon_user = 1,
    mon_kern,
    mon_exit,
} monitor_call_t;

struct monitor_call {
    volatile monitor_call_t mc_call;
    volatile uint64_t mc_num;
    volatile uint64_t mc_a1;
    volatile uint64_t mc_a2;
    volatile uint64_t mc_a3;
    volatile uint64_t mc_a4;
    volatile uint64_t mc_a5;
    volatile uint64_t mc_a6;
    volatile uint64_t mc_a7;
    volatile uint64_t mc_ret;
};

#endif
