#ifndef JOS_KERN_PROF_H
#define JOS_KERN_PROF_H

#include <machine/types.h>

void prof_trap(uint64_t num, uint64_t time);
void prof_syscall(uint64_t num, uint64_t time);
void prof_set_enable(int enable);
void prof_print(void);
void prof_reset(void);

#endif
