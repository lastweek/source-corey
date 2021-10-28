#ifndef JOS_KERN_AT_H
#define JOS_KERN_AT_H

#include <kern/kobjhdr.h>
#include <kern/processor.h>
#include <kern/atmap.h>
#include <kern/darray.h>
#include <inc/segment.h>

struct Address_tree {
    struct kobject_hdr at_ko;
    // Processors the are currently using this at
    struct processor_list at_ps_list;
    struct reclock at_ps_list_lock;
    // Hardware pagemap
    struct Pagemap *at_pgmap;
    struct spinlock at_pgmap_lock;
    // Utrap pointers
    uintptr_t at_utrap_entry;
    uintptr_t at_utrap_stack_base;
    uintptr_t at_utrap_stack_top;
    // Interior node of Address_tree
    uint8_t at_interior : 1;
    // If interior, list of mappings
    struct address_map_list at_map_list;

    // Lock for all address mappings
    struct rwlock at_map_lock;
    struct darray at_uaddrmap;
    struct darray at_addrmap;

    // object lookup cache
    struct {
	struct sobj_ref ref;
	struct kobject *ko;
	uint64_t ps_id;
    } at_obj;
    struct spinlock at_obj_lock;

    // reverse lookup cache
    struct {
	void *va_start;
	void *va_end;
	struct u_address_mapping *uam;
    } at_range;
};

#define N_UADDRMAP_PER_PAGE	(PGSIZE / sizeof(struct u_address_mapping))
#define N_ADDRMAP_PER_PAGE	(PGSIZE / sizeof(struct address_mapping2))

int  at_alloc(struct Address_tree **asp, char interior, proc_id_t pid)
     __attribute__ ((warn_unused_result));

int  at_pagefault(struct Address_tree *as, void *va, uint32_t reqflags)
     __attribute__ ((warn_unused_result));

void at_attach(struct Address_tree *at, struct Processor *ps);
void at_detach(struct Address_tree *at, struct Processor *ps);

int  at_to_user(struct Address_tree *at, struct u_address_tree *uat)
     __attribute__ ((warn_unused_result));
int  at_from_user(struct Address_tree *at, struct u_address_tree *uat)
     __attribute__ ((warn_unused_result));
int  at_set_uslot(struct Address_tree *at, struct u_address_mapping *uam_new)
     __attribute__ ((warn_unused_result));

void at_set_current(struct Address_tree *at);

void at_scope_cb(struct Address_tree *at, kobject_id_t sh, 
		 struct Processor *scope_ps);
void at_remove_cb(struct Address_tree *at, kobject_id_t id);
void at_gc_cb(struct Address_tree *at);

void at_invalidate_addrmap(struct Address_tree *at, struct address_mapping2 *am);

#endif
