#include <machine/memlayout.h>
#include <inc/setjmp.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/copy.h>
#include <inc/utrap.h>
#include <inc/error.h>
#include <inc/fd.h>
#include <inc/array.h>

#include <string.h>
#include <malloc.h>

enum { pfork_debug = 0 };

int64_t
pforkv(proc_id_t pid, uint64_t flags, struct sobj_ref *shares, uint32_t n)
{
    static_assert(u_context_nshare >= 1);
    int64_t r;

    int share_heap = flags & PFORK_SHARE_HEAP;
    page_sharing_mode copy_mode = flags & PFORK_COR ? page_shared_cor : page_shared_cow;
    struct sobj_ref atref = processor_current_as();
        
    struct u_address_tree uat;
    uat.size = 32;
    uat.ents = malloc(uat.size * sizeof(uat.ents[0]));
    if (!uat.ents)
	return -E_NO_MEM;

    while ((r = sys_at_get(atref, &uat)) == -E_NO_SPACE) {
	uat.size *= 2;
	void *p = realloc(uat.ents, uat.size * sizeof(uat.ents[0]));
	if (!p) {
	    free(uat.ents);
	    return -E_NO_MEM;
	}
	uat.ents = p;
    }
    if (r < 0)
	panic("sys_at_get failed: %s", e2s(r));
    
    if (pfork_debug) {
	cprintf("pfork: copy from:\n");
	as_print_uas(&uat);
    }

    int64_t sh_id;
    if ((sh_id = sys_share_alloc(core_env->sh, ~0, "fork-share", pid)) < 0) {
	free(uat.ents);
	return sh_id;
    }

    int64_t ps2_id, at2_id; 
    if ((ps2_id = sys_processor_alloc(sh_id, "fork-processor", pid)) < 0) {
	free(uat.ents);
	sys_share_unref(SOBJ(core_env->sh, sh_id));
	sys_self_drop_share(sh_id);
        return ps2_id;
    }
    if ((at2_id = sys_at_alloc(sh_id, 0, "fork-as", pid)) < 0) {
	free(uat.ents);
	sys_share_unref(SOBJ(core_env->sh, sh_id));
	sys_self_drop_share(sh_id);
        return at2_id;
    }

    r = sys_segment_copy(sh_id, core_env->mtab, "mount-table", page_excl, pid);
    if (r < 0)
	goto done;
    struct sobj_ref new_mtab = SOBJ(sh_id, r);

    // Prepare a setjmp buffer for the new thread, before we copy our stack!
    struct jos_jmp_buf jb;
    if (jos_setjmp(&jb) != 0) {
	core_env->sh = sh_id;
	core_env->psref = SOBJ(sh_id, ps2_id);
	core_env->pid = pid;
	if (!share_heap)
	    free(uat.ents);

	core_env->mtab = new_mtab;

	// init the interval timer on the new CPU
	extern uint64_t int_hz;
	if (int_hz)
	    time_init(int_hz);

	return 0;
    }

    void *fd_base = (void *) UFDBASE;
    void *fd_end = ((char *) fd_base) + MAXFD * PGSIZE;

    uint64_t i = 0;
    for (; i < uat.nent; i++) {
	if ((uat.ents[i].flags & SEGMAP_SHARED) ||
	    (share_heap && (uat.ents[i].flags & SEGMAP_HEAP)))
	{
	    r = sys_share_addref(sh_id, uat.ents[i].object);
	    if (r < 0)
		goto done;
	    uat.ents[i].object.share = sh_id;

	    void *va = uat.ents[i].va;
	    if (va >= fd_base && va < fd_end) {
		assert(uat.ents[i].type == address_mapping_segment);
		struct Fd *fd = (struct Fd *) va;
		jos_atomic_inc(&fd->fd_ref);
	    }
	    
	    continue;
	}
	if (uat.ents[i].type != address_mapping_segment) {
	    char name[JOS_KOBJ_NAME_LEN];
	    name[0] = '\0';
	    sys_obj_get_name(uat.ents[i].object, &name[0]);

	    panic("Bad u_address_mapping:\n"
		  "type: %u\n"
		  "object: %lu.%lu (%s)\n"
		  "kslot: %u\n"
		  "flags: %x\n"
		  "va: %p\n"
		  "start_page: %lu\n"
		  "num_pages: %lu",
		  uat.ents[i].type, 
		  uat.ents[i].object.share, uat.ents[i].object.object, name,
		  uat.ents[i].kslot, uat.ents[i].flags,
		  uat.ents[i].va,
		  uat.ents[i].start_page, uat.ents[i].num_pages);
	}

	r = sys_segment_copy(sh_id, uat.ents[i].object, 
			     "worker-seg", copy_mode, pid);
	if (r < 0)
	    goto done;
	uat.ents[i].object = SOBJ(sh_id, r);
    }

    if (pfork_debug) {
	cprintf("pfork: copied as:\n");
	as_print_uas(&uat);
    }

    r = sys_at_set(SOBJ(sh_id, at2_id), &uat);
    if (r < 0)
        goto done;

    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = SOBJ(sh_id, at2_id);
    uc.uc_entry = (void *) &jos_longjmp;
    uc.uc_stack = 0;
    uc.uc_arg[0] = (uint64_t)&jb;
    uc.uc_arg[1] = 1;
    uc.uc_share[0] = SOBJ(core_env->sh, sh_id);

    r = fs_mount_pfork(new_mtab, &uc.uc_share[1], array_size(uc.uc_share) - 1);
    if (r < 0)
	goto done;

    uint64_t used = 1 + r;
    if (used + n > array_size(uc.uc_share)) {
	r = -E_NO_SPACE;
	goto done;
    }

    for (uint32_t k = 0; k < n; k++)
	uc.uc_share[k + used] = shares[k];

    if ((r = sys_processor_vector(SOBJ(sh_id, ps2_id), &uc)) < 0)
        goto done;
    
    r = ps2_id;
    
 done:
    free(uat.ents);
    sys_share_unref(SOBJ(core_env->sh, sh_id));
    sys_self_drop_share(sh_id);        

    return r;
}

int64_t 
pfork(proc_id_t pid)
{
    return pforkv(pid, 0, 0, 0);
}
