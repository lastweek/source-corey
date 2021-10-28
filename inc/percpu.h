#ifndef JOS_INC_PERCPU_H
#define JOS_INC_PERCPU_H

#include <machine/param.h>
#include <inc/pad.h>

#define PERCPU_TYPE(type)					\
	struct {						\
	       PAD(type) __c[JOS_NCPU];				\
	}

#define	FOREACH_CPU(var, cvar)					\
	for (int __i = 0;					\
	     __i < JOS_NCPU && ((var) = &((cvar)->__c[__i].v)); \
	     __i++)

#ifdef JOS_USER
#define PERCPU_VAL(var) (var)->__c[core_env->pid].v
#endif

#ifdef JOS_KERNEL
#define PERCPU_VAL(var) (var)->__c[arch_cpu()].v
#endif

#endif
