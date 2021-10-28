#ifndef JOS_INC_ARCH_H
#define JOS_INC_ARCH_H

#ifdef JOS_ARCH_amd64
#include <machine/x86.h>

#define arch_pause nop_pause
#define arch_read_tsc read_tsc

#else
#error unknown architecture
#endif

#endif
