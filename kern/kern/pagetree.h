#ifndef JOS_KERN_PAGETREE_H
#define JOS_KERN_PAGETREE_H

#include <machine/types.h>
#include <machine/mmu.h>
#include <inc/proc.h>
#include <inc/copy.h>
#include <inc/spinlock.h>

struct pagetree_entry
{
    void *page;
    page_sharing_mode mode;
    struct spinlock lock;
};

#define PAGETREE_ENTRIES_PER_PAGE (PGSIZE / sizeof(struct pagetree_entry))
#define PAGETREE_INDIRECT_PER_PAGE (PGSIZE / sizeof(struct pagetree_indirect *))

struct pagetree_indirect
{
    union {
        struct pagetree_entry pi_entry[PAGETREE_ENTRIES_PER_PAGE];
        struct pagetree_indirect *pi_indir[PAGETREE_INDIRECT_PER_PAGE];
    };
};

#define PAGETREE_DIRECT_ENTRIES 3
#define PAGETREE_INDIRECT_ENTRIES 4

struct pagetree
{
    struct pagetree_entry pt_direct[PAGETREE_DIRECT_ENTRIES];
    struct pagetree_indirect *pt_indir[PAGETREE_INDIRECT_ENTRIES];
    proc_id_t pid;
    char demand;
};

void pagetree_init(struct pagetree *pt, char on_demand, proc_id_t pid);

// Copy the pagetree structure, copy data based on flags
int pagetree_copy(struct pagetree *ptsrc, struct pagetree *ptdst, page_sharing_mode mode)
     __attribute__ ((warn_unused_result));

// Put a page into the pagetree
int  pagetree_put_page(struct pagetree *pt, uint64_t npage, void *page)
    __attribute__ ((warn_unused_result));

// Get a page from the pagetree
int  pagetree_get_page(struct pagetree *pt, uint64_t npage, void **pagep, 
		       page_sharing_mode mode)
    __attribute__ ((warn_unused_result));

// Decref all pages in the pagetree and free all pagtree_indirect structs
void pagetree_free(struct pagetree *pt);

void pagetree_incref_hw(void *p);

void pagetree_decref_hw(void *p);
    
#endif
