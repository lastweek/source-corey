#include <inc/memlayout.h>
#include <inc/lib.h>
#include <inc/segment.h>
#include <inc/checkpoint.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/copy.h>
#include <inc/error.h>
#include <inc/config.h>

#include <string.h>
#include <malloc.h>

enum { boot_uas_nents = 32 };

static struct sobj_ref		cp_boot_sg[boot_uas_nents + 1];
static struct u_segment_mapping cp_boot_ents[boot_uas_nents];
static struct u_address_space	cp_boot_uas = { .size = boot_uas_nents,
						.ents = cp_boot_ents };
static uint64_t cp_share;

void
checkpoint_boot(uint64_t sh_id, uint64_t ps_id)
{
    // Called by _start
    struct u_context uc;
    int r;
    if ((r = sys_processor_stat(SOBJ(sh_id, ps_id), &uc)) < 0)
	panic("unable to state processor: %s\n", e2s(r));
    
    if ((r = sys_as_get(uc.uc_as, &cp_boot_uas)) < 0)
	panic("unable to get as: %s", e2s(r));

    cp_share = sh_id;

    uint64_t i;
    for (i = 0; i < cp_boot_uas.nent; i++) {
	int64_t sg_id = sys_segment_copy(cp_share, cp_boot_uas.ents[i].segment, 
					 "cp-copy", page_excl);
	if (sg_id < 0)
	    panic("unable to copy segment: %s", e2s(sg_id));
	cp_boot_sg[i] = SOBJ(cp_share, sg_id);
    }
    cp_boot_sg[i].object = 0;
    return;
}

int
checkpoint_boot_spawn(proc_id_t pid, void (*entry)(uint64_t), uint64_t arg)
{
    int64_t r = 0;

    int64_t sh_id = sys_share_alloc(default_share, "cp-share", pid);
    if (sh_id < 0)
	return sh_id;

    r = sys_processor_alloc(sh_id, "helper-ps", pid);
    if (r < 0)
	goto done;
    int64_t ps_id = r;
    
    int64_t as_id;
    r = sys_as_alloc(sh_id, "cp-as", pid);
    if (r < 0)
	goto done;
    as_id = r;

    struct u_segment_mapping ents[boot_uas_nents];
    struct u_address_space uas;
    memset(&uas, 0, sizeof(uas));
    uas.size = boot_uas_nents;
    uas.ents = ents;
    uas.nent = cp_boot_uas.nent;
    memcpy(ents, cp_boot_ents, uas.nent * sizeof(ents[0]));

    for (int i = 0; cp_boot_sg[i].object; i++) {
	r = sys_segment_copy(sh_id, cp_boot_sg[i], "cp-copy", page_shared_cor);
	if (r < 0)
	    goto done;
	uas.ents[i].segment = SOBJ(sh_id, r);
    }
 
    r = sys_as_set(SOBJ(sh_id, as_id), &uas);
    if (r < 0)
	goto done;

    r = libconf_init_ap(sh_id);
    if (r < 0)
	goto done;

    extern uint8_t _start_ap[];
    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_as = SOBJ(sh_id, as_id);
    uc.uc_pid = pid;
    uc.uc_entry = (void *) _start_ap;
    uc.uc_stack = (void *) USTACKTOP;
    uc.uc_arg[0] = sh_id;
    uc.uc_arg[1] = ps_id;
    uc.uc_arg[2] = (uintptr_t) entry;
    uc.uc_arg[3] = arg;
    uc.uc_arg[4] = libconf->libconf_sg_id;
    uc.uc_share[0] = SOBJ(default_share, sh_id);
       
    r = sys_processor_vector(SOBJ(sh_id, ps_id), &uc);

 done:
    if (r < 0) {
	sys_share_unref(SOBJ(default_share, sh_id));
	sys_share_drop(sh_id);
    }
    
    return r;
}
