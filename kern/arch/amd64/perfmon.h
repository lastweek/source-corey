#ifndef JOS_MACHINE_PERFMON_H
#define JOS_MACHINE_PERFMON_H

#include <inc/types.h>

void perfmon_init(void);
int  perfmon_set(uint64_t sel, uint64_t val);

#endif
