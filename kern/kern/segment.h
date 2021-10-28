#ifndef JOS_KERN_SEGMENT_H
#define JOS_KERN_SEGMENT_H

#include <kern/lock.h>
#include <kern/kobjhdr.h>
#include <kern/at.h>
#include <kern/atmap.h>
#include <kern/darray.h>

struct Segment {
    struct kobject_hdr sg_ko;
    struct address_map_list sg_map_list;

    struct darray sg_pages;
};

int  segment_alloc(struct Segment **sgp, proc_id_t pid)
    __attribute__ ((warn_unused_result));
int  segment_copy(struct Segment *src, struct Segment **sgp,
                  page_sharing_mode mode, proc_id_t pid)
    __attribute__ ((warn_unused_result));

int  segment_set_nbytes(struct Segment *sg, uint64_t num_bytes)
    __attribute__ ((warn_unused_result));
int  segment_set_pages(struct Segment *sg, void **pages, uint64_t npages)
     __attribute__ ((warn_unused_result));

int  segment_get_page(struct Segment *sg, uint64_t npage, void **pp, 
		      page_sharing_mode mode)
     __attribute__ ((warn_unused_result));
uint64_t segment_get_npage(struct Segment *sg);

void segment_invalidate(struct Segment *sg);

void segment_scope_cb(struct Segment *sg, kobject_id_t sh, 
		      struct Processor *scope_ps);
void segment_remove_cb(struct Segment *sg, kobject_id_t id);
void segment_gc_cb(struct Segment *sg);

#endif
