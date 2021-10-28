#ifndef JOS_KERN_SHAREMAP_H
#define JOS_KERN_SHAREMAP_H

#include <kern/arch.h>
#include <kern/map.h>
#include <kern/kobjhdr.h>

struct Sharemap {
    struct spinlock lock;
    struct Map map;
    struct Processor *owner;
    int mask;
};

void sharemap_init(struct Sharemap *sm, proc_id_t pid, struct Processor *owner,
		   int share_mask);
int  sharemap_add(struct Sharemap *sm, struct Share *sh)
     __attribute__((warn_unused_result));
int  sharemap_remove(struct Sharemap *sm, kobject_id_t id)
     __attribute__((warn_unused_result));
void sharemap_clear(struct Sharemap *sm);
int  sharemap_get(struct Sharemap *sm, kobject_id_t id, struct Share **sh)
     __attribute__((warn_unused_result));
void sharemap_free(struct Sharemap *sm);
int  sharemap_co_obj(struct Sharemap *sm, struct sobj_ref oref, 
		     struct kobject **ko, uint8_t type)
     __attribute__((warn_unused_result));

#endif
