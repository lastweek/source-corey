#ifndef JOS_INC_DEBUG_H
#define JOS_INC_DEBUG_H

#define debug_print(__exp, __frmt, __args...)			\
    do {							\
        if (__exp)						\
	   cprintf("(%u) %s: " __frmt "\n", core_env->pid,	\
		   __FUNCTION__, ##__args);			\
    } while (0)

#endif
