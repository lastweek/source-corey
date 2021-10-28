#include <machine/mmu.h>
#include <inc/syscall.h>
#include <inc/array.h>
#include <inc/lib.h>
#include <inc/jsysmon.h>
#include <inc/error.h>
#include <inc/arch.h>

#include <string.h>

enum { slot_block = 1 };

int 
jsm_next_slot(struct jsm *sm, struct jsm_slot **slot)
{
    int next = sm->next_slot;
    if (slot_block) {
	while (sm->shared_slots->sm_slots[next].flag & SYSMON_USER_INUSE)
	    arch_pause();
    } else {
	if (sm->shared_slots->sm_slots[next].flag & SYSMON_USER_INUSE)
	    return -E_BUSY;
    }

    sm->shared_slots->sm_slots[next].flag |= SYSMON_USER_INUSE;
    *slot = &sm->shared_slots->sm_slots[next];
    sm->next_slot = (next + 1) % array_size(sm->shared_slots->sm_slots);
    return 0;
}

void
jsm_free_slot(struct jsm_slot *slot)
{
    slot->flag &= ~SYSMON_USER_INUSE;
}

// System calls
void 
jsm_call_device_buf(struct jsm_slot *slot, struct sobj_ref devref, 
		    struct sobj_ref sgref, uint64_t offset, 
		    devio_type type)
{
    slot->arg.sc_num = SYS_device_buf;
    slot->arg.a1 = devref.share;
    slot->arg.a2 = devref.object;
    slot->arg.a3 = sgref.share;
    slot->arg.a4 = sgref.object;
    slot->arg.a5 = offset;
    slot->arg.a6 = type;
    slot->flag |= SYSMON_KERN_CALL;
}

void
jsm_call_processor_current(struct jsm_slot *slot)
{
    slot->arg.sc_num = SYS_processor_current;
    slot->flag |= SYSMON_KERN_CALL;
}

int
jsm_setup(struct jsm *sm, struct sobj_ref sm_ref)
{
    static_assert(sizeof(struct jsm_shared_slots) <= PGSIZE);
    memset(sm, 0, sizeof(*sm));
    sm->sm_ref = sm_ref;

    // Allocate the shared slots
    struct sobj_ref slots_seg;
    void *va = 0;
    int r = segment_alloc(core_env->sh, PGSIZE, &slots_seg, &va,
			  SEGMAP_SHARED, "sysmon shared slots", core_env->pid);
    if (r < 0) {
	cprintf("jsm_setup: failed to allocate shared slots: %s\n", e2s(r));
	return r;
    }

    sm->shared_slots = va;
    for (uint32_t i = 0; i < SM_NUM_SLOTS; i++) {
	sm->shared_slots->sm_slots[i].flag = 0;
    }

    // Configure the system monitor to monitor the slots
    struct u_device_conf udc;
    udc.type = device_conf_sysmon;
    udc.sysmon.seg = slots_seg;
    udc.sysmon.offset = 0;
    udc.sysmon.num_slots = SM_NUM_SLOTS;
    r = sys_device_conf(sm_ref, &udc);
    if (r < 0) {
	as_unmap(va);
	sys_share_unref(slots_seg);
    }
    return r;
}

int
jsm_set_nic(struct jsm *jsm, struct sobj_ref nic_ref, struct sobj_ref seg_ref)
{
    // Setup a cached NIC
    struct u_device_conf udc;
    udc.type = device_conf_sysmon_nic;
    udc.sysmon_nic.nic = nic_ref;
    udc.sysmon_nic.seg = seg_ref;
    return sys_device_conf(jsm->sm_ref, &udc);
}
