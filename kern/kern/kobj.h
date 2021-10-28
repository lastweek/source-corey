#ifndef JOS_KERN_KOBJ_H
#define JOS_KERN_KOBJ_H

#include <kern/kobjhdr.h>

#include <machine/types.h>
#include <kern/processor.h>
#include <kern/segment.h>
#include <kern/at.h>
#include <kern/share.h>
#include <kern/device.h>
#include <inc/copy.h>

// XXX should pad out kobjs to distribute over cache better.

struct kobject {
    union {
	struct kobject_hdr hdr;
	
	struct Address_tree at;
	struct Segment sg;
        struct Processor ps;
	struct Share sh;
	struct Device dv;
    };

    LIST_ENTRY(kobject) ko_gc_link;
};

LIST_HEAD(kobject_list, kobject);

int  kobject_alloc(uint8_t type, struct kobject **kpp, proc_id_t pid)
    __attribute__ ((warn_unused_result));

void kobject_set_name(struct kobject_hdr *kp, const char *name);

void	 kobject_incref(struct kobject_hdr *kp);
void	 kobject_decref(struct kobject_hdr *kp);
uint64_t kobject_getref(struct kobject_hdr *kp);

void     kobject_on_scope(struct kobject_hdr *kp, kobject_id_t sh, 
			  struct Processor *scope_ps);

void kobject_gc_collect(struct kobject *ko);
void kobject_gc_scan(void);

// scope_ps droped sh, which contained ko
typedef void (*kobject_scope_cb)(kobject_ptr ko, kobject_id_t sh, 
				 struct Processor *scope_ps);
// all refs to ko were removed from sh
typedef void (*kobject_remove_cb)(kobject_ptr ko, kobject_id_t sh);
// ko has more references, clean up and free resources
typedef void (*kobject_gc_cb)(kobject_ptr ko);

#endif
