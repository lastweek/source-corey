#ifndef JOS_INC_SEGMENT_H
#define JOS_INC_SEGMENT_H

#include <inc/kobj.h>
#include <inc/share.h>

/*
 * Must define at least one of these for the entry to be valid.
 * These match with the ELF flags (inc/elf64.h).
 */
#define SEGMAP_EXEC		0x01
#define SEGMAP_WRITE		0x02
#define SEGMAP_READ		0x04

#define SEGMAP_HW               0x08

/*
 * User-interpreted flags
 */
#define SEGMAP_SHARED		0x0100
#define SEGMAP_HEAP		0x0200

typedef enum address_mapping_enum {
    address_mapping_segment = 1,
    address_mapping_interior,
} address_mapping_t;

struct u_address_mapping {
    address_mapping_t type;

    struct sobj_ref object;
    uint32_t kslot;
    uint32_t flags;
    void *va;
    
    // For address_mapping_interior these are hardware specific 
    uint64_t start_page;
    uint64_t num_pages;
};

struct u_address_tree {
    uint64_t size;
    uint64_t nent;
    struct u_address_mapping *ents;
    
    // Only used for root Address_maps
    void *trap_handler;
    void *trap_stack_base;
    void *trap_stack_top;
};

#endif
