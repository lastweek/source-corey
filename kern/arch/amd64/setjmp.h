#ifndef JOS_MACHINE_SETJMP_H
#define JOS_MACHINE_SETJMP_H

#include <inc/types.h>

#define JOS_LONGJMP_GCCATTR	

struct jos_jmp_buf {
    uint64_t jb_rip;
    uint64_t jb_rsp;
    uint64_t jb_rbp;
    uint64_t jb_rbx;
    uint64_t jb_r12;
    uint64_t jb_r13;
    uint64_t jb_r14;
    uint64_t jb_r15;
};

#endif
