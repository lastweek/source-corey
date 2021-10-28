#include <kern/lib.h>
#include <kern/sysmon.h>
#include <kern/device.h>
#include <kern/segment.h>
#include <kern/lockmacro.h>
#include <kern/kobj.h>
#include <kern/syscall.h>
#include <inc/error.h>
#include <inc/copy.h>
#include <inc/jsysmon.h>
#include <inc/syscall_num.h>
#include <inc/error.h>

#define NSYSMON_DEV 16

struct sysmon_cache {
    struct Segment *sg;
    struct Device *dv;
};

struct sysmon_device {
    struct device_handler sm_handler;
    struct jsm_shared_slots *shared_slots;
    uint32_t num_slots;
    int next_slot;
    struct Processor *src;
    // XXX cache is never gc'ed
    struct sysmon_cache nic_cache[2];
};

static struct sysmon_cache *
sysmon_nic_cache(struct sysmon_device *sm, uint64_t dv_id, uint64_t sg_id)
{
    for (uint32_t i = 0; i < array_size(sm->nic_cache); i++) {
	if (!sm->nic_cache[i].sg)
	    continue;

	if (sm->nic_cache[i].sg->sg_ko.ko_id == sg_id &&
	    sm->nic_cache[i].dv->dv_ko.ko_id == dv_id) 
	{
	    return &sm->nic_cache[i];
	}
    }
    return 0;
}

static void
sysmon_poll(void *a)
{
    struct sysmon_device *sm = a;
    struct jsm_slot *slot;
    
    if (!sm->shared_slots)
	return;
	
    int next_slot = sm->next_slot;
    slot = &(sm->shared_slots->sm_slots[next_slot]);
    while (slot->flag & SYSMON_KERN_CALL) {
	slot->flag &= ~SYSMON_KERN_CALL;
	int to_return = slot->flag & SYSMON_KERN_RET;

	struct jsm_arg arg;
	memcpy(&arg, &slot->arg, sizeof(arg));
	if (!to_return)
	    slot->flag &= ~SYSMON_USER_INUSE;

	int64_t ret;
	if (arg.sc_num == SYS_device_buf) {
	    struct sysmon_cache *sc = sysmon_nic_cache(sm, arg.a2, arg.a4);
	    if (sc) {
		ret = device_buf(sc->dv, sc->sg, arg.a5, arg.a6);
		goto executed;
	    }
	}
	
	ret = kern_syscall(sm->src, arg.sc_num, arg.a1, 
			   arg.a2, arg.a3, arg.a4, arg.a5,
			   arg.a6, arg.a7);
	
    executed:
	
	if (to_return) {
	    slot->ret = ret;
	    slot->flag &= ~SYSMON_KERN_RET;
	}

	next_slot = (next_slot + 1) % sm->num_slots;
	sm->next_slot = next_slot;
	slot = &(sm->shared_slots->sm_slots[next_slot]);
    }
}

static int
sysmon_conf(void *a, struct u_device_conf *udc)
{
    struct sysmon_device *sm = a;
    void *kva;
    struct Processor *src = processor_sched();
    
    if (udc->type == device_conf_sysmon) {
	struct kobject *seg;

	// Force page alignment for simplicity
	if (udc->sysmon.offset % PGSIZE)
	    return -E_INVAL;
	if ((udc->sysmon.num_slots * sizeof(struct jsm_slot)) > PGSIZE)
	    return -E_INVAL;
	
	int r = processor_co_obj(src, udc->sysmon.seg, &seg, kobj_segment);
	if (r < 0)
	    return r;
	
	// XXX should pin the segment

	r = segment_get_page(&seg->sg, 
			     udc->sysmon.offset / PGSIZE, &kva, 
			     page_shared_cow);
	if (r < 0)
	    return r;
	
	sm->src = src;
	sm->num_slots = udc->sysmon.num_slots;
	sm->shared_slots = kva;
	return 0;
    } else if (udc->type == device_conf_sysmon_nic) {
	struct sysmon_cache *cache = 0;
	for (uint32_t i = 0; i < array_size(sm->nic_cache); i++) {
	    if (!sm->nic_cache[i].sg) {
		cache = &sm->nic_cache[i];
		break;
	    }
	}
	
	if (!cache)
	    return -E_NO_SPACE;

	struct kobject *seg, *nic;
	int r = processor_co_obj(src, udc->sysmon_nic.seg, &seg, kobj_segment);
	if (r < 0)
	    return r;
	
	r = processor_co_obj(src, udc->sysmon_nic.nic, &nic, kobj_device);
	if (r < 0)
	    return r;
	
	kobject_incref(&seg->hdr);
	kobject_incref(&nic->hdr);

	cache->sg = &seg->sg;
	cache->dv = &nic->dv;
	
	return 0;
    }
    return -E_INVAL;
}

void
sysmon_init(void)
{
    static PAD_TYPE(struct sysmon_device, JOS_CLINE) sm[NSYSMON_DEV];
    for (int i = 0; i < NSYSMON_DEV; i++) {
	sm[i].val.sm_handler.dh_arg = &sm[i];
	sm[i].val.sm_handler.dh_poll = &sysmon_poll;
	sm[i].val.sm_handler.dh_conf = &sysmon_conf;
	sm[i].val.sm_handler.dh_hdr.type = device_sysmon;
	sm[i].val.shared_slots = 0;
	sm[i].val.num_slots = 0;
	sm[i].val.next_slot = 0;
	device_register(&sm[i].val.sm_handler);
    }
}
