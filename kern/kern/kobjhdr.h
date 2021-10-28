#ifndef JOS_KERN_KOBJHDR_H
#define JOS_KERN_KOBJHDR_H

#include <machine/types.h>
#include <machine/atomic.h>
#include <kern/id.h>
#include <kern/pagetree.h>
#include <kern/lock.h>
#include <inc/proc.h>

struct kobject_hdr {
    kobject_id_t ko_id;
    
    kobject_type_t ko_type;
    char ko_name[JOS_KOBJ_NAME_LEN];

    jos_atomic64_t ko_ref;
    
    // Lock for the whole kobject
    struct spinlock ko_lock;

    // The processor (memory node) this object is allocated in.
    proc_id_t ko_pid;
};

typedef union {
    struct kobject *ko;
    struct Address_tree *at;
    struct Segment *sg;
    struct Processor *ps;
    struct Share *sh;
    struct Device *dv;
} kobject_ptr __attribute__((__transparent_union__));

#define KOBJ_NAME(ko) (ko)->ko_name[0] ? (ko)->ko_name : "unknown"

#endif
