#include <kern/lib.h>
#include <kern/kobj.h>
#include <kern/segment.h>
#include <kern/lockmacro.h>
#include <inc/error.h>

enum { pre_clear_pages = 0 };
enum { pre_alloc_pages = 0 };

int
segment_alloc(struct Segment **sgp, proc_id_t pid)
{
    struct kobject *ko;
    int r = kobject_alloc(kobj_segment, &ko, pid);
    if (r < 0)
	return r;

    struct Segment *s = &ko->sg;
    aml_init(&s->sg_map_list);
    darray_init(&s->sg_pages, PGSIZE, !pre_alloc_pages, pid);
    *sgp = s;

    return 0;
}

int
segment_copy(struct Segment *src, struct Segment **sgp, page_sharing_mode mode,
	     proc_id_t pid)
{
    assert_locked(&src->sg_ko);
    if (!SAFE_EQUAL(mode, page_shared_cor) &&
	!SAFE_EQUAL(mode, page_shared_cow) &&
	!SAFE_EQUAL(mode, page_excl))
    {
	return -E_INVAL;
    }

    struct Segment *dst;
    int r = segment_alloc(&dst, pid);
    if (r < 0)
	return r;

    if (SAFE_EQUAL(mode, page_shared_cor) || SAFE_EQUAL(mode, page_shared_cow))
	segment_invalidate(src);

    lock_kobj(dst);
    r = darray_copy(&src->sg_pages, &dst->sg_pages, mode);
    unlock_kobj(dst);
    if (r < 0)
	return r;

    *sgp = dst;
    return 0;
}

int
segment_set_nbytes(struct Segment *sg, uint64_t num_bytes)
{
    int r = 0;
    uint64_t npages = ROUNDUP(num_bytes, PGSIZE) / PGSIZE;

    lock_kobj(sg);
    if (darray_get_nent(&sg->sg_pages) > npages)
	segment_invalidate(sg);

    if (darray_get_nent(&sg->sg_pages) != npages)
	r = darray_set_nent(&sg->sg_pages, npages, pre_clear_pages);

    unlock_kobj(sg);
    
    return r;
}

int
segment_get_page(struct Segment *sg, uint64_t npage, void **pp, 
		 page_sharing_mode mode)
{
    return darray_get(&sg->sg_pages, npage, pp, mode);
}

uint64_t 
segment_get_npage(struct Segment *sg)
{
    return darray_get_nent(&sg->sg_pages);
}

int
segment_set_pages(struct Segment *sg, void **pages, uint64_t npages)
{
    cprintf("segment_set_pages: xxx\n");
    return -1;
}

void
segment_invalidate(struct Segment *sg)
{
    segment_remove_cb(sg, 0);
}

void
segment_scope_cb(struct Segment *sg, kobject_id_t sh,
		 struct Processor *scope_ps)
{
    aml_scope(&sg->sg_map_list, sh, scope_ps);
}

void
segment_remove_cb(struct Segment *sg, kobject_id_t sh)
{
    aml_invalidate(&sg->sg_map_list, sh);
}

void
segment_gc_cb(struct Segment *sg)
{
    darray_free(&sg->sg_pages);
}
