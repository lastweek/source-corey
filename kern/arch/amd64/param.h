#ifndef JOS_MACHINE_PARAM_H
#define JOS_MACHINE_PARAM_H

#include <kern/param.h>

#define JOS_ARCH_BITS	64
#define JOS_ARCH_ENDIAN	JOS_LITTLE_ENDIAN
#define JOS_ARCH_RETADD	1
#define JOS_CLINE	64

#define JOS_NCPU	16
#define JOS_NNODE	JOS_NCPU
#define JOS_MAX_IRQS	32

#define JOS_ATOMIC_LOCK "lock ;"

#define JOS_AMD64_OFFSET_HACK 1

#endif
