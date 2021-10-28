#include <kern/lib.h>
#include <kern/pagetree.h>
#include <kern/pageinfo.h>
#include <inc/error.h>
#include <inc/copy.h>

enum { debug_copy = 0 };

static void
pagetree_decref(void *p)
{
    struct page_info *pi = page_to_pageinfo(p);
    if (jos_atomic_dec_and_test(&pi->pi_ref)) {
	assert(jos_atomic_read(&pi->pi_hw_ref) == 0);
	page_free(p);
    }
}

static void
pagetree_incref(void *p)
{
    struct page_info *pi = page_to_pageinfo(p);
    jos_atomic_inc(&pi->pi_ref);
}

void 
pagetree_incref_hw(void *p)
{
    struct page_info *pi = page_to_pageinfo(p);
    if (jos_atomic_read(&pi->pi_ref) == 0)
	panic("trying to pin a 0 refed page");
    if (jos_atomic_read(&pi->pi_hw_ref) > jos_atomic_read(&pi->pi_ref))
	panic("imbalanced references");
    
    jos_atomic_inc(&pi->pi_ref);
    jos_atomic_inc(&pi->pi_hw_ref);
}

void pagetree_decref_hw(void *p)
{
    struct page_info *pi = page_to_pageinfo(p);
    if (jos_atomic_read(&pi->pi_hw_ref) == 0)
	panic("trying to decref a 0 refed page");
    if (jos_atomic_read(&pi->pi_hw_ref) > jos_atomic_read(&pi->pi_ref))
	panic("imbalanced references");
    
    jos_atomic_dec(&pi->pi_hw_ref);
    pagetree_decref(p);
}

static int
pagetree_copy_page(struct pagetree_entry *pe, proc_id_t pid)
{
    void *pp;
    int r = page_alloc(&pp, pid);
    if (r < 0)
	return r;

    if (debug_copy)
	cprintf("pagetree_copy_page: flags %d src pp %p dst pp %p\n",
		SAFE_UNWRAP(pe->mode), pe->page, pp);

    memcpy(pp, pe->page, PGSIZE);
    struct page_info *pi = page_to_pageinfo(pp);
    pi->pi_clear = 1;
    
    pagetree_incref(pp);
    pagetree_decref(pe->page);
    pe->mode = page_excl;
    pe->page = pp;
    return 0;
}

static int
pagetree_get_entp_indirect(struct pagetree_indirect **nvindir, uint64_t npage,
			   struct pagetree_entry **outp, int level,
			   proc_id_t pid)
{
    volatile struct pagetree_indirect **indir = 
	(volatile struct pagetree_indirect **)nvindir;

    if (*indir == 0) {
	void *va;
	int r = page_alloc(&va, pid);
	if (r < 0)
	    return r;
	page_zero(va);

	jos_atomic64_t *atomic = (jos_atomic64_t *)(uintptr_t)indir;
	if (jos_atomic_compare_exchange64(atomic, 0, (uintptr_t)va) != 0)
	    page_free(va);
    }

    if (level == 0) {
	assert(npage < PAGETREE_ENTRIES_PER_PAGE);
	*outp = (struct pagetree_entry *)&(*indir)->pi_entry[npage];
	return 0;
    }

    uint64_t n_pages_per_i = PAGETREE_ENTRIES_PER_PAGE;
    for (int i = 0; i < level - 1; i++)
	n_pages_per_i *= PAGETREE_INDIRECT_PER_PAGE;

    uint32_t next_slot = npage / n_pages_per_i;
    uint32_t next_page = npage % n_pages_per_i;

    struct pagetree_indirect **next = (struct pagetree_indirect **)&(*indir)->pi_indir[next_slot];
    return pagetree_get_entp_indirect(next, next_page, outp, level - 1, pid);
}

static int
pagetree_get_entp(struct pagetree *pt, uint64_t npage,
		  struct pagetree_entry **outp)
{
    if (npage < PAGETREE_DIRECT_ENTRIES) {
	*outp = &pt->pt_direct[npage];
	return 0;
    }
    npage -= PAGETREE_DIRECT_ENTRIES;

    uint64_t num_indirect_pages = PAGETREE_ENTRIES_PER_PAGE;
    for (uint64_t i = 0; i < PAGETREE_INDIRECT_ENTRIES; i++) {
	if (npage < num_indirect_pages)
	    return pagetree_get_entp_indirect(&pt->pt_indir[i], npage,
					      outp, i, pt->pid);
	npage -= num_indirect_pages;
	num_indirect_pages *= PAGETREE_INDIRECT_PER_PAGE;
    }

    cprintf("pagetree_get_entp: %ld pages leftover!\n", npage);
    return -E_NO_SPACE;
}

int
pagetree_get_page(struct pagetree *pt, uint64_t npage, void **pagep,
		  page_sharing_mode mode)
{
    int r;
    struct pagetree_entry *pe;
    r = pagetree_get_entp(pt, npage, &pe);
    if (r < 0)
	return r;
    assert(pe);

    if (SAFE_EQUAL(pe->mode, page_shared_cor)) {
	assert(!SAFE_EQUAL(mode, page_excl));
	r = pagetree_copy_page(pe, pt->pid);
    } else if (SAFE_EQUAL(pe->mode, page_shared_cow)) {
	assert(!SAFE_EQUAL(mode, page_excl));
	if (SAFE_EQUAL(mode, page_shared_cow))
	    r = pagetree_copy_page(pe, pt->pid);
    }
    if (r < 0)
	return r;

    if (pe->page == 0 && pt->demand) {
	spin_lock(&pe->lock);
	if (pe->page == 0) {
	    void *p;
	    assert(page_alloc(&p, pt->pid) == 0);
	    pagetree_incref(p);
	    pe->page = p;
	    pe->mode = page_excl;
	}
	spin_unlock(&pe->lock);
    }
    
    *pagep = pe->page;
    return 0;
}

int
pagetree_put_page(struct pagetree *pt, uint64_t npage, void *page)
{
    int r;
    struct pagetree_entry *pe;
    r = pagetree_get_entp(pt, npage, &pe);
    if (r < 0)
	return r;
    assert(pe);

    if (pe->page)
	pagetree_decref(pe->page);
    if (page)
	pagetree_incref(page);

    pe->page = page;
    pe->mode = page_excl;
    return 0;
}

static int __attribute__ ((warn_unused_result))
pagetree_copy_ent(struct pagetree_entry *pesrc, struct pagetree_entry *pedst,
		  page_sharing_mode mode, proc_id_t pid)
{
    assert(pesrc->page);
    
    if (SAFE_EQUAL(mode, page_excl)) {
	int r = page_alloc(&pedst->page, pid);
	if (r < 0)
	    return r;
	pagetree_incref(pedst->page);
	memcpy(pedst->page, pesrc->page, PGSIZE);
    } else {
	pedst->page = pesrc->page;
	pagetree_incref(pedst->page);
	pesrc->mode = page_shared_cow;    
    }

    pedst->mode = mode;
    return 0;
}

static int
pagetree_copy_indirect(struct pagetree_indirect *srcindir, struct
		       pagetree_indirect **dstindir, page_sharing_mode mode,
		       int level, proc_id_t pid)
{
    if (!srcindir)
	return 0;

    assert(!(*dstindir));
    int r = page_alloc((void **) dstindir, pid);
    if (r < 0)
	return r;
    page_zero(*dstindir);

    if (level == 0) {
	for (uint64_t i = 0; i < PAGETREE_ENTRIES_PER_PAGE; i++)
	    if (srcindir->pi_entry[i].page) {
		r = pagetree_copy_ent(&srcindir->pi_entry[i],
				      &(*dstindir)->pi_entry[i], mode, pid);
		if (r < 0)
		    return r;
	    }
	return 0;
    }

    for (uint64_t i = 0; i < PAGETREE_INDIRECT_PER_PAGE; i++) {
	r = pagetree_copy_indirect(srcindir->pi_indir[i],
				   &(*dstindir)->pi_indir[i],
				   mode, level - 1, pid);
	if (r < 0)
	    return r;
    }
    return 0;
}

int
pagetree_copy(struct pagetree *ptsrc, struct pagetree *ptdst, 
	      page_sharing_mode mode)
{
    pagetree_free(ptdst);
    ptdst->demand = ptsrc->demand;

    int r;
    for (uint64_t i = 0; i < PAGETREE_DIRECT_ENTRIES; i++)
	if (ptsrc->pt_direct[i].page) {
	    r = pagetree_copy_ent(&ptsrc->pt_direct[i], &ptdst->pt_direct[i],
				  mode, ptdst->pid);
	    if (r < 0) {
		pagetree_free(ptdst);
		return r;
	    }
	}

    for (uint64_t i = 0; i < PAGETREE_INDIRECT_ENTRIES; i++) {
	r = pagetree_copy_indirect(ptsrc->pt_indir[i], &ptdst->pt_indir[i],
				   mode, i, ptdst->pid);
	if (r < 0) {
	    pagetree_free(ptdst);
	    return r;
	}
    }
    return 0;
}

static void
pagetree_free_indirect(struct pagetree_indirect *indir, int level)
{
    if (!indir)
	return;

    if (level == 0) {
	for (uint64_t i = 0; i < PAGETREE_ENTRIES_PER_PAGE; i++)
	    if (indir->pi_entry[i].page)
		pagetree_decref(indir->pi_entry[i].page);
    } else {
	for (uint64_t i = 0; i < PAGETREE_INDIRECT_PER_PAGE; i++)
	    pagetree_free_indirect(indir->pi_indir[i], level - 1);
    }

    page_free(indir);
}

void
pagetree_free(struct pagetree *pt)
{
    for (uint64_t i = 0; i < PAGETREE_DIRECT_ENTRIES; i++) {
	if (pt->pt_direct[i].page)
	    pagetree_decref(pt->pt_direct[i].page);
	pt->pt_direct[i].page = 0;
    }

    for (uint64_t i = 0; i < PAGETREE_INDIRECT_ENTRIES; i++) {
	pagetree_free_indirect(pt->pt_indir[i], i);
	pt->pt_indir[i] = 0;
    }
}

void
pagetree_init(struct pagetree *pt, char on_demand, proc_id_t pid)
{
    memset(pt, 0, sizeof(*pt));
    pt->pid = pid;
    pt->demand = on_demand;
}
