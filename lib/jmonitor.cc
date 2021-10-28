extern "C" {
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/error.h>
}

#include <malloc.h>
#include <assert.h>

#include <inc/jmonitor.hh>
#include <inc/error.hh>
#include <inc/scopeguard.hh>

enum { debug = 1 };

static void
cleanup_share(struct sobj_ref shref)
{
    assert(sys_share_unref(shref) == 0);
    assert(sys_self_drop_share(shref.object) == 0);
}

void
jmonitor::add_device(struct sobj_ref ref)
{
    if (n_dev_ref_ == size_dev_ref_) {
	uint64_t s = size_dev_ref_ ? 2 * size_dev_ref_ : 4;
	dev_ref_ = (struct sobj_ref *) realloc(dev_ref_, sizeof(ref) * s);
	if (!dev_ref_)
	    throw error(-E_NO_MEM, "realloc failed");
	size_dev_ref_ = s;
    }
    
    dev_ref_[n_dev_ref_++] = ref;
}

void
jmonitor::start(proc_id_t pid)
{
    int64_t sh_id, ps_id, as_id;
    error_check(sh_id = sys_share_alloc(core_env->sh, ~0, "monitor-share", pid));
    struct sobj_ref shref = SOBJ(core_env->sh, sh_id);
    scope_guard<void, struct sobj_ref> sh_cleanup(cleanup_share, shref);
    
    error_check(ps_id = sys_processor_alloc(sh_id, "monitor-ps", pid));
    struct sobj_ref psref = SOBJ(sh_id, ps_id);

    // XXX dummy AT, might want to use the current AT?
    error_check(as_id = sys_at_alloc(sh_id, 0, "monitor-as", pid));
    struct sobj_ref atref = SOBJ(sh_id, as_id);

    for (uint64_t i = 0; i < n_dev_ref_; i++) {
	struct sobj_ref o = dev_ref_[i];
	error_check(sys_share_addref(sh_id, o));
	error_check(sys_processor_set_device(psref, i, SOBJ(sh_id, o.object)));
    }

    if (debug)
	cprintf("jmonitor::start: Processor %ld on CPU %u\n", ps_id, pid);
    
    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = atref;
    uc.uc_mode = ps_mode_mon;
    uc.uc_share[0] = shref;
    error_check(sys_processor_vector(psref, &uc));
}
