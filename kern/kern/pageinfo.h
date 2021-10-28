#ifndef JOS_KERN_PAGEINFO_H
#define JOS_KERN_PAGEINFO_H

#include <machine/types.h>
#include <kern/arch.h>
#include <inc/pad.h>

struct page_info {
    // references to this page from pagetree's
    jos_atomic_t pi_ref;
    // page on the free list
    uint32_t pi_freepage : 1;
    // page is a special hardware page for VMs to use
    uint32_t pi_hwpage : 1;
    // page is a Pagemap
    uint32_t pi_pmap : 1;
    // page is also an interior Pagemap
    uint32_t pi_pmap_int : 1;
    // page is cleared
    uint32_t pi_clear : 1;
    struct spinlock pi_clear_lock;
    // readable and writeable hardware refs (DMA or pgmap)
    jos_atomic_t pi_hw_ref;
};

// Padding the page_info will decrease false sharing, but it may
// also increase the working set, since related pages that are
// physically close will no longer have page_infos in the same
// cache line.

typedef PAD_TYPE(struct page_info, JOS_CLINE) page_info_t;
extern page_info_t *page_infos;

static __inline struct page_info *
page_to_pageinfo(void *p)
{
    ppn_t pn = pa2ppn(kva2pa(p));
    return &page_infos[pn].val;
}

#endif
