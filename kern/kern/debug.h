#ifndef JOS_KERN_DEBUG_H
#define JOS_KERN_DEBUG_H

#include <machine/types.h>
#include <inc/kdebug.h>

int debug_call(kdebug_op_t op, uint64_t a0, uint64_t a1, uint64_t a2, 
	       uint64_t a3, uint64_t a4, uint64_t a5) 
    __attribute__((warn_unused_result));

#endif
