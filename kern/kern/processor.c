#include <machine/proc.h>
#include <kern/arch.h>
#include <kern/processor.h>
#include <kern/lockmacro.h>
#include <kern/kobj.h>
#include <kern/timer.h>
#include <inc/error.h>
#include <inc/copy.h>
#include <inc/pad.h>

enum { vector_debug = 0 };

static PAD_TYPE(struct Processor*, JOS_CLINE) processors[JOS_NCPU];
static PAD_TYPE(jos_atomic_t, JOS_CLINE) processor_free[JOS_NCPU];

static int
alloc_processor(proc_id_t pid)
{
    if (pid >= ncpu)
	return -E_INVAL;
    if (jos_atomic_compare_exchange(&processor_free[pid].val, 0, 1) == 0)
	return 0;
    return -E_BUSY;
}

static void
free_processor(proc_id_t pid)
{
    assert(jos_atomic_read(&processor_free[pid].val) == 1);
    jos_atomic_set(&processor_free[pid].val, 0);
}

int
processor_alloc(struct Processor **ps, proc_id_t pid)
{
    int r = alloc_processor(pid);
    if (r < 0)
	return r;

    struct kobject *ko;
    r = kobject_alloc(kobj_processor, &ko, pid);
    if (r < 0) {
	free_processor(pid);
        return r;
    }

    struct Processor *p = &ko->ps;
    sharemap_init(&p->ps_sharemap, pid, p, ~0);
    p->ps_pid = pid;
    *ps = p;
    return 0;
}

int
processor_vector(struct Processor *ps, const struct u_context *uc)
{
    int r;
    assert_locked(&ps->ps_ko);

    if (ps->ps_running)
        return -E_BUSY;

    struct kobject *ko;

    for (int i = 0; i < u_context_nshare && uc->uc_share[i].share; i++) {
	r = processor_co_obj(processor_sched(), uc->uc_share[i], 
			     &ko, kobj_share);
	if (r < 0)
	    return r;
	r = processor_add_share(ps, &ko->sh);
	if (r < 0)
	    return r;
    }

    // Make sure using AS ref. from target processor
    r = processor_co_obj(ps, uc->uc_at, &ko, kobj_address_tree);
    if (r < 0)
	return r;   
    struct Address_tree *at = &ko->at;
    
    r = processor_arch_vector(ps, uc);
    if (r < 0)
	return r;

    at_attach(at, ps);
    ps->ps_at = at;
    ps->ps_atref = uc->uc_at;
    ps->ps_mode = uc->uc_mode;
    ps->ps_running = 1;
    ps->ps_halt = 0;
    
    debug(vector_debug, "pid %d (%ld.%s)",
	  ps->ps_pid, ps->ps_ko.ko_id, ps->ps_ko.ko_name);

    processors[ps->ps_pid].val = ps;
    return 0;
}

int
processor_halt(struct Processor *ps)
{
    lock_kobj(ps);

    if (ps != processor_sched()) {
	ps->ps_halt = 1;
	arch_halt_mp(ps->ps_pid);
	unlock_kobj(ps);
	return 0;
    }

    at_set_current(0);
    at_detach(ps->ps_at, ps);
    ps->ps_at = 0;
    ps->ps_atref = SOBJ(0, 0);

    timer_interval_time(ps->ps_pid, 0);
    sharemap_clear(&ps->ps_sharemap);
    unlock_kobj(ps);

    processors[ps->ps_pid].val = 0;
    ps->ps_running = 0;
    
    return 1;
}

int
processor_pagefault(struct Processor *ps, void *va, uint32_t reqflags)
{
    return at_pagefault(ps->ps_at, va, reqflags);
}

struct Processor *
processor_sched(void)
{
    return processors[arch_cpu()].val;
}

void
processor_set_interval(struct Processor *ps, uint64_t hz)
{
    assert_locked(&ps->ps_ko);
    
    ps->ps_interval_hz = hz;
    if (ps->ps_running)
	timer_interval_time(ps->ps_pid, ps->ps_interval_hz);
}

int
processor_utrap(struct Processor *ps, uint32_t trapno, int precise, uint64_t arg)
{
    assert_locked(&ps->ps_ko);
    assert(ps == processor_sched());

    if (!ps->ps_running)
	return -E_INVAL;
    
    if (processor_arch_is_masked(ps)) {
	if (precise)
	    return -E_BUSY;
	else
	    return processor_arch_pending(ps, trapno);
    }

    return processor_arch_utrap(ps, trapno, arg);
}

static void __attribute__((noreturn))
processor_monitor_run(struct Processor *ps)
{
    struct Device *dv[NDEVICES_PER_MONITOR];

    uint64_t n = 0;
    for (uint32_t i = 0; i < NDEVICES_PER_MONITOR; i++) {
	if (ps->ps_monitor_obj[i].object) {
	    int r = processor_co_obj(ps, ps->ps_monitor_obj[i], 
				     (struct kobject **)&dv[n], kobj_device);
	    if (r < 0) {
		cprintf("processor_monitor_run: failed to co %ld.%ld\n",
			ps->ps_monitor_obj[i].share, 
			ps->ps_monitor_obj[i].object);
		continue;
	    }
	    // pin the device
	    kobject_incref(&dv[n]->dv_ko);
	    n++;
	}
    }

    for (;;) {
	for (uint64_t i = 0; i < n; i++)
	    device_poll(dv[i]);
	
	// If we ever have a driver that co's or rm's objects
	// we need to run these at some regular interval.
	share_action_scan();
	kobject_gc_scan();
    }

    for (uint32_t i = 0; i < n; i++)
	locked_void_call(kobject_incref, &dv[i]->dv_ko);
}

void
processor_run(void)
{
    struct Processor *ps;

    share_action_scan();
    kobject_gc_scan();

    while (!(ps = processor_sched()))
        arch_pause();

    if (ps->ps_mode == ps_mode_mon)
	processor_monitor_run(ps);

    at_set_current(ps->ps_at);
    processor_arch_run(ps);
}

int
processor_add_share(struct Processor *ps, struct Share *sh)
{
    return sharemap_add(&ps->ps_sharemap, sh);
}

int
processor_remove_share(struct Processor *ps, kobject_id_t id)
{
    return sharemap_remove(&ps->ps_sharemap, id);
}

int
processor_get_share(struct Processor *ps, kobject_id_t id, struct Share **sh)
{
    return sharemap_get(&ps->ps_sharemap, id, sh);
}

int
processor_import_obj(struct Processor *ps, kobject_id_t sh_id, 
		     struct kobject *ko)
{
    struct Share *sh;
    int r = processor_get_share(ps, sh_id, &sh);
    if (r < 0)
	return r;
    return share_import_obj(sh, ko);
}

int
processor_co_obj(struct Processor *ps, struct sobj_ref oref, 
		 struct kobject **ko, uint8_t type)
{
    return sharemap_co_obj(&ps->ps_sharemap, oref, ko, type);
}

int
processor_remove_obj(struct Processor *ps, struct sobj_ref oref)
{
    struct Share *sh;
    int r = processor_get_share(ps, oref.share, &sh);
    if (r < 0)
	return r;
    return share_remove_obj(sh, oref.object);
}

int
processor_set_device(struct Processor *ps, uint64_t i, struct sobj_ref o)
{
    assert_locked(&ps->ps_ko);
    if (i > array_size(ps->ps_monitor_obj))
	return -E_NO_SPACE;
    ps->ps_monitor_obj[i] = o;
    return 0;
}

void
processor_enable_fp(struct Processor *ps)
{
    if (ps->ps_fp_enabled)
	return;

    arch_fp_init(&ps->ps_fpreg);
    ps->ps_fp_enabled = 1;
    return;
}

void
processor_scope_cb(struct Processor *ps, kobject_id_t sh, 
		   struct Processor *scope_ps)
{
}

void 
processor_remove_cb(struct Processor *ps, kobject_id_t id)
{
}

void
processor_gc_cb(struct Processor *ps)
{
    if (ps->ps_running) {
	processor_halt(ps);
	while (ps->ps_running)
	    arch_pause();
    }

    sharemap_free(&ps->ps_sharemap);
    free_processor(ps->ps_pid);
}
