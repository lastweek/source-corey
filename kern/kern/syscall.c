#include <kern/arch.h>
#include <kern/lib.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/segment.h>
#include <kern/lockmacro.h>
#include <kern/kobj.h>
#include <kern/debug.h>
#include <kern/device.h>
#include <kern/prof.h>
#include <kern/uaccess.h>
#include <machine/trap.h>
#include <inc/error.h>
#include <inc/locality.h>
#include <inc/copy.h>

#define check(expr)					\
    ({							\
	int64_t __c = (expr);				\
	if (__c < 0)					\
	    return __c;					\
	__c;						\
    })

enum { syscall_debug = 0 };

// Syscall handlers
static int64_t __attribute__ ((warn_unused_result))
sys_cons_puts(struct Processor *src, const char *s, uint64_t size)
{
    check(uaccess_start());
    for (uint64_t i = 0; i < size; i++)
	cons_putc(s[i], cons_source_user);
    uaccess_stop();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_cons_flush(struct Processor *src)
{
    cons_flush();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_locality_get(struct Processor *src, struct u_locality_matrix *ulm)
{
    check(uaccess_start());
    arch_locality_fill(ulm);
    uaccess_stop();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_device_list(struct Processor *src, struct u_device_list *udl)
{
    check(uaccess_start());
    check(device_list(udl));
    uaccess_stop();
    return 0;
}

static int64_t __attribute__ ((warn_unused_result))
sys_device_alloc(struct Processor *src, uint64_t sh, uint64_t did, uint64_t pid)
{
    struct Device *dv;
    check(device_alloc(&dv, did, pid));
    check(processor_import_obj(src, sh, (struct kobject *)dv));
    return dv->dv_ko.ko_id;
}

static int __attribute__ ((warn_unused_result))
sys_device_stat(struct Processor *src, struct sobj_ref devref, 
		struct u_device_stat *uds)
{
    struct kobject *dv;
    check(processor_co_obj(src, devref, &dv, kobj_device));    
    check(uaccess_start());
    check(device_stat(&dv->dv, uds));
    uaccess_stop();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_device_conf(struct Processor *src, struct sobj_ref devref, 
		struct u_device_conf *udc)
{
    struct kobject *dv;
    check(processor_co_obj(src, devref, &dv, kobj_device));    
    check(uaccess_start());
    check(device_conf(&dv->dv, udc));
    uaccess_stop();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_device_buf(struct Processor *src, struct sobj_ref devref, 
	       struct sobj_ref sgref, uint64_t offset, devio_type type)
{
    struct kobject *dv, *sg;
    check(processor_co_obj(src, devref, &dv, kobj_device));    
    check(processor_co_obj(src, sgref, &sg, kobj_segment));    
    check(device_buf(&dv->dv, &sg->sg, offset, type));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_debug(struct Processor *src, uint64_t op, uint64_t a0, uint64_t a1,
	  uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    check(debug_call(op, a0, a1, a2, a3, a4, a5));
    return 0;
}

static int64_t __attribute__ ((warn_unused_result))
sys_obj_get_name(struct Processor *src, struct sobj_ref obj, char *name)
{  
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_any));
    check(uaccess_start());
    strncpy(name, &ko->hdr.ko_name[0], JOS_KOBJ_NAME_LEN);
    uaccess_stop();
    return 0;
}

static int64_t __attribute__ ((warn_unused_result))
sys_share_alloc(struct Processor *src, uint64_t sh_id, uint64_t mask, 
		const char *name, uint64_t pid)
{
    struct Share *sh;
    check(share_alloc(&sh, mask, pid));
    check(uaccess_start());    
    kobject_set_name(&sh->sh_ko, name);
    uaccess_stop();
    check(processor_import_obj(src, sh_id, (struct kobject *)sh));
    check(processor_add_share(src, sh));
    return sh->sh_ko.ko_id;
}

static int64_t __attribute__ ((warn_unused_result))
sys_share_addref(struct Processor *src, uint64_t dst_sh_id, 
		 uint64_t src_sh_id, uint64_t obj_id)
{
    struct Share *src_sh, *dst_sh;
    struct kobject *obj;

    check(processor_get_share(src, dst_sh_id, &dst_sh));
    check(processor_get_share(src, src_sh_id, &src_sh));
    check(share_co_obj(src_sh, obj_id, &obj, kobj_any));
    if (obj->hdr.ko_type == kobj_share)    
	return -E_INVAL;
    check(share_import_obj(dst_sh, obj));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_share_unref(struct Processor *src, uint64_t sh_id, uint64_t obj_id) 
{
    check(processor_remove_obj(src, SOBJ(sh_id, obj_id)));
    return 0;
}

static int64_t __attribute__ ((warn_unused_result))
sys_segment_alloc(struct Processor *src, uint64_t sh_id, uint64_t num_bytes, 
		 const char *name, uint64_t pid)
{
    struct Segment *sg;
    check(segment_alloc(&sg, pid));
    check(uaccess_start());
    kobject_set_name(&sg->sg_ko, name);
    uaccess_stop();
    check(segment_set_nbytes(sg, num_bytes));
    check(processor_import_obj(src, sh_id, (struct kobject *)sg));
    debug(syscall_debug, "new %ld.%s", sg->sg_ko.ko_id, name);
    return sg->sg_ko.ko_id;
}

static int64_t __attribute__ ((warn_unused_result))
sys_segment_copy(struct Processor *src, uint64_t dst_sh, struct sobj_ref obj, 
		 const char *name, uint64_t mode, uint64_t pid)
{
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_segment));
    struct Segment *new_sg;
    check(locked_call(segment_copy, &ko->sg, &new_sg, 
		      SAFE_WRAP(page_sharing_mode, mode), pid));
    check(uaccess_start());
    kobject_set_name(&new_sg->sg_ko, name);
    uaccess_stop();
    check(processor_import_obj(src, dst_sh, (struct kobject *)new_sg));
    debug(syscall_debug, "new %ld.%s", new_sg->sg_ko.ko_id, name);
    return new_sg->sg_ko.ko_id;
}

static int64_t __attribute__ ((warn_unused_result))
sys_segment_get_nbytes(struct Processor *src, struct sobj_ref obj)
{
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_segment));
    return segment_get_npage(&ko->sg) * PGSIZE;
}

static int64_t __attribute__ ((warn_unused_result))
sys_segment_set_nbytes(struct Processor *src, struct sobj_ref obj, uint64_t nbytes)
{
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_segment));
    check(segment_set_nbytes(&ko->sg, nbytes));
    return 0;
}

static int64_t __attribute__ ((warn_unused_result))
sys_processor_alloc(struct Processor *src, uint64_t sh, const char *name, 
		    uint64_t pid)
{
    struct Processor *ps;
    check(processor_alloc(&ps, pid));
    check(processor_import_obj(src, sh, (struct kobject *)ps));
    check(uaccess_start());
    kobject_set_name(&ps->ps_ko, name);
    uaccess_stop();
    debug(syscall_debug, "new %ld.%s", ps->ps_ko.ko_id, name);
    return ps->ps_ko.ko_id;
}

static int64_t __attribute__ ((warn_unused_result))
sys_processor_current(struct Processor *src)
{
    return src->ps_ko.ko_id;
}

static int __attribute__ ((warn_unused_result))
sys_processor_vector(struct Processor *src, struct sobj_ref obj, 
		     struct u_context *uc)
{
    struct u_context uc2;
    check(uaccess_start());
    memcpy(&uc2, uc, sizeof(uc2));
    uaccess_stop();
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_processor));
    struct Processor *ps = &ko->ps;
    check(locked_call(processor_vector, ps, &uc2));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_processor_set_interval(struct Processor *src, struct sobj_ref obj, uint64_t hz)
{
    struct kobject *ko;
    check(processor_co_obj(src, obj, &ko, kobj_processor));
    locked_void_call(processor_set_interval, &ko->ps, hz);
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_processor_halt(struct Processor *src, struct sobj_ref obj)
{
    struct kobject *ko;    
    check(processor_co_obj(src, obj, &ko, kobj_processor));
    processor_halt(&ko->ps);
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_processor_set_device(struct Processor *src, struct sobj_ref psref, 
			 uint64_t i, struct sobj_ref devref)
{
    struct kobject *ko;
    struct Processor *dst;
    check(processor_co_obj(src, psref, &ko, kobj_processor));    
    dst = &ko->ps;
    check(locked_call(processor_set_device, dst, i, devref));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_self_fp_enable(struct Processor *src, uint64_t x)
{
    locked_void_call(processor_enable_fp, src);
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_self_drop_share(struct Processor *src, uint64_t sh_id)
{
    check(processor_remove_share(src, sh_id));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_self_get_as(struct Processor *src, struct sobj_ref *obj)
{
    check(uaccess_start());
    *obj = src->ps_atref;
    uaccess_stop();
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_self_get_pid(struct Processor *src)
{
    return src->ps_pid;
}

static int64_t __attribute__ ((warn_unused_result))
sys_at_alloc(struct Processor *src, uint64_t sh_id, uint64_t interior, 
	     const char *name, uint64_t pid)
{
    struct Address_tree *at;
    check(at_alloc(&at, interior, pid));
    check(processor_import_obj(src, sh_id, (struct kobject *)at));
    check(uaccess_start());
    kobject_set_name(&at->at_ko, name);
    uaccess_stop();
    debug(syscall_debug, "new %ld.%s", at->at_ko.ko_id, name);
    return at->at_ko.ko_id;
}

static int __attribute__ ((warn_unused_result))
sys_at_get(struct Processor *src, struct sobj_ref atref, 
	   struct u_address_tree *uat)
{
    struct kobject *ko;
    check(processor_co_obj(src, atref, &ko, kobj_address_tree));
    check(at_to_user(&ko->at, uat));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_at_set(struct Processor *src, struct sobj_ref atref, 
	   struct u_address_tree *uat)
{
    struct kobject *ko;
    check(processor_co_obj(src, atref, &ko, kobj_address_tree));
    check(at_from_user(&ko->at, uat));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_at_set_slot(struct Processor *src, struct sobj_ref atref, 
                struct u_address_mapping *uam)
{
    struct kobject *ko;
    check(processor_co_obj(src, atref, &ko, kobj_address_tree));
    check(at_set_uslot(&ko->at, uam));
    return 0;
}

static int __attribute__ ((warn_unused_result))
sys_machine_reinit(struct Processor *src, const char *s, uint64_t size)
{
    if (size >= PGSIZE)
	return -E_INVAL;
    check(uaccess_start());
    memcpy(boot_args, s, size);
    uaccess_stop();
    boot_args[size] = 0;

    arch_reinit();
}

#define SYSCALL(name, args...)						\
    case SYS_##name:							\
	debug(syscall_debug, "sys_" #name "()");			\
	return sys_##name(src, ##args);

static int64_t __attribute__ ((warn_unused_result))
syscall_exec(struct Processor *src, uint64_t num, uint64_t a1, uint64_t a2, 
	     uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    void __attribute__((unused)) *p1 = (void *) (uintptr_t) a1;
    void __attribute__((unused)) *p2 = (void *) (uintptr_t) a2;
    void __attribute__((unused)) *p3 = (void *) (uintptr_t) a3;
    void __attribute__((unused)) *p4 = (void *) (uintptr_t) a4;
    void __attribute__((unused)) *p5 = (void *) (uintptr_t) a5;
    void __attribute__((unused)) *p6 = (void *) (uintptr_t) a6;
    void __attribute__((unused)) *p7 = (void *) (uintptr_t) a7;
    
    switch (num) {
        SYSCALL(cons_puts, p1, a2);
        SYSCALL(cons_flush);
        SYSCALL(locality_get, p1);
	SYSCALL(device_list, p1);
	SYSCALL(device_alloc, a1, a2, a3);
	SYSCALL(device_stat, SOBJ(a1, a2), p3);
	SYSCALL(device_conf, SOBJ(a1, a2), p3);
	SYSCALL(device_buf, SOBJ(a1, a2), SOBJ(a3, a4), a5, a6);
	SYSCALL(debug, a1, a2, a3, a4, a5, a5, a7);
        SYSCALL(obj_get_name, SOBJ(a1, a2), p3);
	SYSCALL(share_alloc, a1, a2, p3, a4);
	SYSCALL(share_addref, a1, a2, a3);
	SYSCALL(share_unref, a1, a2);
        SYSCALL(segment_alloc, a1, a2, p3, a4);
        SYSCALL(segment_copy, a1, SOBJ(a2, a3), p4, a5, a6);
        SYSCALL(segment_get_nbytes, SOBJ(a1, a2));
        SYSCALL(segment_set_nbytes, SOBJ(a1, a2), a3);
        SYSCALL(processor_alloc, a1, p2, a3);
        SYSCALL(processor_current);
        SYSCALL(processor_vector, SOBJ(a1, a2), p3);
        SYSCALL(processor_set_interval, SOBJ(a1, a2), a3);
        SYSCALL(processor_halt, SOBJ(a1, a2));
	SYSCALL(processor_set_device, SOBJ(a1, a2), a3, SOBJ(a4, a5));
	SYSCALL(self_fp_enable, a1);
	SYSCALL(self_drop_share, a1);
	SYSCALL(self_get_as, p1);
	SYSCALL(self_get_pid);
        SYSCALL(at_alloc, a1, a2, p3, a4);
        SYSCALL(at_get, SOBJ(a1, a2), p3);
        SYSCALL(at_set, SOBJ(a1, a2), p3);
        SYSCALL(at_set_slot, SOBJ(a1, a2), p3);
	SYSCALL(machine_reinit, p1, a2);
    default:
	cprintf("Unknown syscall %"PRIu64"\n", num);
	return -E_INVAL;
    }
}

uint64_t
kern_syscall(struct Processor *src, uint64_t num, uint64_t a1, uint64_t a2, 
	     uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    uint64_t s = karch_get_tsc();
    int64_t r = syscall_exec(src, num, a1, a2, a3, a4, a5, a6, a7);
    prof_syscall(num, karch_get_tsc() - s);
    return r;
}

