#ifndef JOS_KERN_ATMAP_H
#define JOS_KERN_ATMAP_H

#include <kern/kobjhdr.h>
#include <inc/types.h>
#include <inc/spinlock.h>
#include <inc/queue.h>

struct address_mapping2 {
    struct spinlock am_lock;    
    // kobject (Address_tree, Segment) mapped
    struct kobject *am_kobj;
    // Addtress_tree this mapping is for
    struct Address_tree *am_parent;
    // Processor responsible for mapping
    struct Processor *am_ps;
    // share that was used to specify the kobject
    kobject_id_t am_sh;
    
    // first virtual address (page) of mapping
    void *am_va_first;
    // last virtual addresss (page) of mapping
    void *am_va_last;
        
    // slot that backs this address_mapping 
    uint64_t am_at_slot;
    
    LIST_ENTRY(address_mapping2) am_link;
};

struct address_map_list {
    LIST_HEAD(list, address_mapping2) list;
    struct spinlock lock;
};

void aml_init(struct address_map_list *aml);
void aml_invalidate(struct address_map_list *aml, kobject_id_t sh);
void aml_scope(struct address_map_list *aml, kobject_id_t sh,
	       struct Processor *scope_ps);
void aml_insert(struct address_map_list *aml, struct address_mapping2 *am);
void aml_remove(struct address_map_list *aml, struct address_mapping2 *am);
void aml_foreach(struct address_map_list *aml, void (*cb)(struct address_mapping2 *));

struct address_map_list *aml_get_list(struct kobject *kobj);

#endif
