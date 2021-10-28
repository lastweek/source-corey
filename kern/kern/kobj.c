#include <machine/proc.h>
#include <machine/mmu.h>
#include <kern/arch.h>
#include <kern/lib.h>
#include <kern/kobj.h>
#include <kern/lockmacro.h>
#include <inc/error.h>
#include <inc/pad.h>

enum { kobj_debug = 0 };

static PAD_TYPE(struct kobject_list, JOS_CLINE) ko_gc_list[JOS_NCPU];

static const kobject_gc_cb gc_cb[kobj_ntypes] = {
    [kobj_segment]	   = segment_gc_cb,
    [kobj_address_tree]	   = at_gc_cb,
    [kobj_processor]	   = processor_gc_cb,
    [kobj_share]	   = share_gc_cb,
    [kobj_device]	   = device_gc_cb,
};

int
kobject_alloc(uint8_t type, struct kobject **kpp, proc_id_t pid)
{
    static_assert(sizeof(struct kobject) < PGSIZE);
    
    void *p;
    int r = page_alloc(&p, pid);
    if (r < 0)
        return r;
    
    struct kobject *ko = (struct kobject *)p;
    memset(ko, 0, sizeof(struct kobject));
    spin_init(&ko->hdr.ko_lock);

    struct kobject_hdr *kh = &ko->hdr;
    kh->ko_type = type;
    kh->ko_id = id_alloc();
    kh->ko_pid = pid;

    kobject_gc_collect(ko);
    
    *kpp = ko;
    return 0;
}

void
kobject_incref(struct kobject_hdr *kp)
{
    jos_atomic_inc64(&kp->ko_ref);
}

void
kobject_decref(struct kobject_hdr *kp)
{
    assert(kobject_getref(kp));
    if (jos_atomic_dec_and_test64(&kp->ko_ref))
	kobject_gc_collect((struct kobject *)kp);
}

uint64_t
kobject_getref(struct kobject_hdr *kp)
{
    return jos_atomic_read(&kp->ko_ref);
}

void
kobject_set_name(struct kobject_hdr *kp, const char *name)
{
    strncpy(kp->ko_name, name, JOS_KOBJ_NAME_LEN - 1);
    kp->ko_name[JOS_KOBJ_NAME_LEN - 1] = '\0';
}

static void
kobject_free(struct kobject *ko)
{
    debug(kobj_debug, "free type = %u, koid = %ld", ko->hdr.ko_type, ko->hdr.ko_id);
    page_free(ko);
}

void
kobject_gc_collect(struct kobject *ko)
{
    LIST_INSERT_HEAD(&ko_gc_list[arch_cpu()].val, ko, ko_gc_link);
}

void
kobject_gc_scan(void)
{
    while (1) {
        struct kobject *ko = LIST_FIRST(&ko_gc_list[arch_cpu()].val);
        if (!ko)
            break;

        LIST_REMOVE(ko, ko_gc_link);
	if (kobject_getref(&ko->hdr))
	    continue;
	
	if (ko->hdr.ko_type >= array_size(gc_cb) || !gc_cb[ko->hdr.ko_type])
            panic("kobject_gc_scan: unknown kobject type %d", ko->hdr.ko_type);

	gc_cb[ko->hdr.ko_type](ko);
        kobject_free(ko);
    }
}
