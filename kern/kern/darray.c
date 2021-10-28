#include <kern/darray.h>
#include <kern/pageinfo.h>
#include <kern/lib.h>
#include <inc/error.h>

void 
darray_init(struct darray *a, uint64_t ent_size, char on_demand, proc_id_t pid)
{
    assert(PGSIZE >= ent_size);
    pagetree_init(&a->da_pt, on_demand, pid);
    rw_init(&a->da_ptl);
    a->da_pid = pid;
    a->da_esz = ent_size;
    a->da_pp = PGSIZE / ent_size;
    a->da_nent = 0;
    a->da_demand = on_demand;
}

void
darray_free(struct darray *a)
{
    pagetree_free(&a->da_pt);
}

int
darray_get(struct darray *a, uint64_t i, void **pp, page_sharing_mode mode)
{
    rw_read_lock(&a->da_ptl);
    if (i >= a->da_nent) {
	rw_read_unlock(&a->da_ptl);
	return -E_INVAL;
    }

    uint64_t npage = i / a->da_pp;
    uint64_t pagei = (i % a->da_pp) * a->da_esz;

    char *b;
    int r = pagetree_get_page(&a->da_pt, npage, (void **)&b, mode);
    rw_read_unlock(&a->da_ptl);
    if (r < 0)
	return r;

    *pp = &b[pagei];
    return 0;
}

static int
darray_set_nent_internal(struct darray *a, uint64_t n, char clear)
{
    uint64_t curnpg = ROUNDUP(a->da_nent, a->da_pp) / a->da_pp;
    uint64_t npg = ROUNDUP(n, a->da_pp) / a->da_pp;

    int r = 0;
    for (uint64_t i = npg; i < curnpg; i++) {
	r = pagetree_put_page(&a->da_pt, i, 0);
	if (r < 0)
	    panic("darray_set_nbytes: cannot drop page: %s", e2s(r));
    }
    
    uint64_t maxalloc = curnpg;
    for (uint64_t i = curnpg; !a->da_demand && i < npg; i++) {
	void *p;
	r = page_alloc(&p, a->da_pid);
	if (r == 0) {
	    r = pagetree_put_page(&a->da_pt, i, p);
	    if (r < 0)
		page_free(p);
	    else
		maxalloc++;
	}

	if (r < 0) {
	    // free all the pages we allocated up to now
	    for (uint64_t j = curnpg; j < maxalloc; j++)
		assert(0 == pagetree_put_page(&a->da_pt, j, 0));
	    return r;
	}

	if (clear) {
	    page_zero(p);
	    page_to_pageinfo(p)->pi_clear = 1;
	}
    }

    a->da_nent = n;
    return 0;
}

int
darray_set_nent(struct darray *a, uint64_t n, char clear)
{
    rw_write_lock(&a->da_ptl);
    int r = darray_set_nent_internal(a, n, clear);
    rw_write_unlock(&a->da_ptl);
    return r;
}

int
darray_grow_nent(struct darray *a, uint64_t n)
{
    rw_write_lock(&a->da_ptl);
    if (n <= a->da_nent) {
	rw_write_unlock(&a->da_ptl);
	return 0;
    }
    int r = darray_set_nent_internal(a, n, 1);
    rw_write_unlock(&a->da_ptl);
    return r;
}

uint64_t
darray_get_nent(const struct darray *a)
{
    return a->da_nent;
}

int
darray_copy(struct darray *src, struct darray *dst, page_sharing_mode mode)
{
    if (dst->da_nent != 0 || src->da_nent == 0)
        return -E_INVAL;
    
    // Lock order does not matter, since dst is empty
    rw_write_lock(&dst->da_ptl);
    rw_write_lock(&src->da_ptl);
    int r = pagetree_copy(&src->da_pt, &dst->da_pt, mode);
    if (r < 0) {
	rw_write_unlock(&dst->da_ptl);
	rw_write_unlock(&src->da_ptl);
        return r;
    }
    dst->da_nent = src->da_nent;
    rw_write_unlock(&dst->da_ptl);
    rw_write_unlock(&src->da_ptl);
    return 0;
}
