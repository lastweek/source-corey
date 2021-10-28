#ifndef JOS_INC_CONTEXT_H
#define JOS_INC_CONTEXT_H

#include <inc/kobj.h>
#include <inc/proc.h>
#include <inc/share.h>

enum { u_context_narg = 6 };
enum { u_context_nshare = 20 };

typedef enum { ps_mode_reg = 0, ps_mode_vm, ps_mode_mon } ps_mode_t;

struct u_context
{
    struct sobj_ref uc_at;
    ps_mode_t uc_mode;
    void *uc_entry;
    void *uc_stack;
    uint64_t uc_vm_nbytes;

    struct sobj_ref uc_share[u_context_nshare];
    uint64_t uc_arg[u_context_narg];
};

#endif
