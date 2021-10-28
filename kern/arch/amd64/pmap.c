#include <machine/pmap.h>
#include <machine/x86.h>
#include <machine/proc.h>
#include <kern/pageinfo.h>
#include <kern/lockmacro.h>
#include <kern/arch.h>
#include <kern/at.h>
#include <inc/segment.h>
#include <inc/error.h>

static void
page_map_free_level(struct Pagemap *pgmap, int pmlevel)
{
    // Skip the kernel half of the address space
    int maxi = (pmlevel == NPTLVLS ? NPTENTRIES / 2 : NPTENTRIES);
    int i;

    for (i = 0; i < maxi; i++) {
	ptent_t ptent = pgmap->pm_ent[i];
	pgmap->pm_ent[i] = 0;
	if (!(ptent & PTE_P))
	    continue;
	if (pmlevel > 0) {
	    struct Pagemap *pm = (struct Pagemap *) pa2kva(PTE_ADDR(ptent));
	    struct page_info *pi = page_to_pageinfo(pm);
	    if (pi->pi_pmap)
		continue;
	    page_map_free_level(pm, pmlevel - 1);
	    page_free(pm);
	}
    }
}

void
page_map_free(struct Pagemap *pm)
{
    struct page_info *pi = page_to_pageinfo(pm);
    assert(pi->pi_pmap);
    pi->pi_pmap = 0;
    page_map_free_level(pm, NPTLVLS);
    page_free(pm);
}

int
page_map_alloc(struct Pagemap **pm_store, int interior, proc_id_t pid)
{
    void *pmap;
    int r = page_alloc(&pmap, pid);
    if (r < 0)
	return r;

    struct page_info *pi = page_to_pageinfo(pmap);
    pi->pi_pmap = 1;
    pi->pi_pmap_int = interior;
    memcpy(pmap, &bootpml4[arch_cpu()], PGSIZE);
    *pm_store = (struct Pagemap *) pmap;
    return 0;
}

void
pmap_set_current(struct Pagemap *pm)
{
    if (!pm)
	pm = &bootpml4[arch_cpu()];

    lcr3(kva2pa(pm));
}

static int
page_map_traverse_internal(struct Pagemap *pgmap, int pmlevel,
			   const void *first, const void *last,
			   int flags,
			   page_map_traverse_cb cb, const void *arg,
			   void *va_base, proc_id_t pid)
{
    int r;
    assert(pmlevel >= 0 && pmlevel <= NPTLVLS);

    uint32_t first_idx = PDX(pmlevel, first);
    uint32_t last_idx = PDX(pmlevel, last);

    for (uint64_t idx = first_idx; idx <= last_idx; idx++) {
	ptent_t *pm_entp = &pgmap->pm_ent[idx];
	ptent_t pm_ent = *pm_entp;

	void *ent_va = va_base + (idx << PDSHIFT(pmlevel));

	if (pmlevel == 0) {
	    if ((flags & PM_TRAV_CREATE) || (pm_ent & PTE_P))
		cb(arg, pm_entp, ent_va);
	    continue;
	}

	if (pmlevel == 1 && (pm_ent & PTE_PS)) {
	    cb(arg, pm_entp, ent_va);
	    continue;
	}

	if (pmlevel == 2 && (flags & PM_TRAV_LINK)) {
	    cb(arg, pm_entp, ent_va);
	    continue;
	}

	if (!(pm_ent & PTE_P)) {
	    if (!(flags & PM_TRAV_CREATE))
		continue;

	    void *p;
	    if ((r = page_alloc(&p, pid)) < 0)
		return r;

	    page_zero(p);
	    pm_ent = kva2pa(p) | PTE_P | PTE_U | PTE_W;
	    *pm_entp = pm_ent;
	}

	struct Pagemap *pm_next = (struct Pagemap *) pa2kva(PTE_ADDR(pm_ent));
	const void *first_next = (idx == first_idx) ? first : 0;
	const void *last_next =
	    (idx == last_idx) ? last : (const void *) (uintptr_t) UINT64(~0);
	r = page_map_traverse_internal(pm_next, pmlevel - 1, first_next,
				       last_next, flags, cb, arg, ent_va,
				       pid);
	if (r < 0)
	    return r;
    }

    return 0;
}

static void
page_map_invalidate_internal(struct Pagemap *pgmap, 
			     const void *first, const void *last, 
			     int pmlevel)
{
    assert(pmlevel >= 0 && pmlevel <= NPTLVLS);

    uint32_t first_idx = PDX(pmlevel, first);
    uint32_t last_idx = PDX(pmlevel, last);

    for (uint64_t idx = first_idx; idx <= last_idx; idx++) {
	ptent_t *pm_entp = &pgmap->pm_ent[idx];
	
	if (!(*pm_entp & PTE_P))
	    continue;
	
	if (pmlevel == 0) {
	    *pm_entp = 0;
	    continue;
	}

	struct Pagemap *pm_next = (struct Pagemap *) pa2kva(PTE_ADDR(*pm_entp));
	struct page_info *pi = page_to_pageinfo(pm_next);
	if (pi->pi_pmap) {
	    *pm_entp = 0;	    
	    continue;
	}

	const void *first_next = (idx == first_idx) ? first : 0;
	const void *last_next =
	    (idx == last_idx) ? last : (const void *) (uintptr_t) UINT64(~0);
	page_map_invalidate_internal(pm_next, first_next, last_next, 
				     pmlevel - 1);
    }
}

void
page_map_invalidate(struct Pagemap *pgmap, const void *first, const void *last)
{
    assert(last < (const void *) ULIM);
    struct page_info *pi = page_to_pageinfo(pgmap);    
    if (pi->pi_pmap_int)
	page_map_invalidate_internal(pgmap, first, last, 1);
    else
	page_map_invalidate_internal(pgmap, first, last, NPTLVLS);
}

static void
pgdir_walk_cb(const void *arg, ptent_t * ptep, void *va)
{
    ptent_t **pte_store = (ptent_t **) arg;
    *pte_store = ptep;
}

static int
pgdir_walk(struct Pagemap *pgmap, int pmlevel, const void *va,
	   int flags, ptent_t ** pte_store, proc_id_t pid)
{
    *pte_store = 0;
    if ((uintptr_t) va >= ULIM)
	panic("padir_walk: va %p over ULIM", va);
    
    int r = page_map_traverse_internal(pgmap, pmlevel, 
				       va, va,
				       flags, &pgdir_walk_cb, pte_store, 0, pid);

    if (r < 0)
	return r;
    if ((flags & PM_TRAV_CREATE) && !*pte_store)
	return -E_INVAL;
    return 0;
}

void
as_arch_page_invalidate_cb(const void *arg, ptent_t * ptep, void *va)
{
    uint64_t pte = *ptep;
    if (pte & PTE_P)
	*ptep = 0;
}

int
as_arch_putpage(struct Pagemap *pgmap, void *va, void *pp, uint32_t flags,
		proc_id_t pid)
{
    uint64_t ptflags = PTE_P | PTE_U | PTE_NX;
    if ((flags & SEGMAP_WRITE))
	ptflags |= PTE_W;
    if ((flags & SEGMAP_EXEC))
	ptflags &= ~PTE_NX;

    ptent_t *ptep;
    int r;
    struct page_info *pi = page_to_pageinfo(pgmap);
    if (pi->pi_pmap_int)
	r = pgdir_walk(pgmap, 1, va, PM_TRAV_CREATE, &ptep, pid);
    else
	r = pgdir_walk(pgmap, NPTLVLS, va, PM_TRAV_CREATE, &ptep, pid);

    if (r < 0)
	return r;

    *ptep = kva2pa(pp) | ptflags;
    return 0;
}

int  
as_arch_putinterior(struct Pagemap *pgmap, void *va, struct Pagemap *pimap, 
		    proc_id_t pid)
{
    ptent_t *ptep;
    int r = pgdir_walk(pgmap, NPTLVLS, va, PM_TRAV_CREATE | PM_TRAV_LINK, 
		       &ptep, pid);
    if (r < 0)
	return r;

    if (!(*ptep & PTE_P)) {
	uint64_t pdflags = PTE_P | PTE_U | PTE_W;
	*ptep = kva2pa(pimap) | pdflags;
    } else {
	assert(PTE_ADDR(*ptep) == kva2pa(pimap));
    }

    return 0;
}

static void *
page_lookup(struct Pagemap *pgmap, void *va, ptent_t ** pte_store)
{
    if ((uintptr_t) va >= ULIM)
	panic("page_lookup: va %p over ULIM", va);

    ptent_t *ptep;
    int r = pgdir_walk(pgmap, NPTLVLS, va, 0, &ptep, 0);
    if (r < 0)
	panic("pgdir_walk(%p, create=0) failed: %d", va, r);

    if (pte_store)
	*pte_store = ptep;

    if (ptep == 0 || !(*ptep & PTE_P))
	return 0;

    return pa2kva(PTE_ADDR(*ptep));
}

int
check_user_access(struct Address_tree *at, const void *ptr,
		  uint64_t nbytes, uint32_t reqflags)
{
    char release = 0;
    if (!spin_locked(&at->at_ko.ko_lock)) {
	lock_kobj(at);
	release = 1;
    }

    ptent_t pte_flags = PTE_P | PTE_U;
    if (reqflags & SEGMAP_WRITE)
	pte_flags |= PTE_W;

    int r = 0;
    if (nbytes > 0) {
	int overflow = 0;
	uintptr_t iptr = (uintptr_t) ptr;
	uintptr_t start = ROUNDDOWN(iptr, PGSIZE);
	uintptr_t end = ROUNDUP(iptr + nbytes, PGSIZE);

	if (end <= start || overflow) {
	    r = -E_INVAL;
	    goto done;
	}

	for (uintptr_t va = start; va < end; va += PGSIZE) {
	    if (va >= ULIM) {
		r = -E_INVAL;
		goto done;
	    }

	    ptent_t *ptep;
	    if (at->at_pgmap &&
		page_lookup(at->at_pgmap, (void *) va, &ptep) &&
		(*ptep & pte_flags) == pte_flags)
		continue;

	    r = at_pagefault(at, (void *) va, reqflags);
	    if (r < 0)
		goto done;
	}
    }

  done:
    if (release)
	unlock_kobj(at);

    return r;
}

void *
pa2kva(physaddr_t pa)
{
    return (void *) (pa + PHYSBASE);
}

physaddr_t
kva2pa(void *kva)
{
    extern char end[];
    physaddr_t va = (physaddr_t) kva;
    if (va >= KERNBASE && va < (uint64_t)end)
	return va - KERNBASE;
    if (va >= PHYSBASE && va < PHYSBASE + (global_npages << PGSHIFT))
	return va - PHYSBASE;
    panic("kva2pa called with invalid kva %p", kva);
}

ppn_t
pa2ppn(physaddr_t pa)
{
    ppn_t pn = pa >> PGSHIFT;
    if (pn > global_npages)
	panic("pa2ppn: pa 0x%lx out of range, npages %" PRIu64,
	      (unsigned long) pa, global_npages);
    return pn;
}

physaddr_t
ppn2pa(ppn_t pn)
{
    if (pn > global_npages)
	panic("ppn2pa: ppn %lx out of range, npages %" PRIu64,
	      (unsigned long) pn, global_npages);
    return (pn << PGSHIFT);
}

void
cpu_init_pmaps(void)
{
    extern char ereplicate[], sreplicate[];
    uint64_t size = (uintptr_t)ereplicate - (uintptr_t)sreplicate;

    assert(size < 0x100000);

    for (int i = 0; i < nmemnode; i++) {
	struct memory_node *mn = &memnode[i];

	assert(mn->length >= size);
	if (RELOC(sreplicate) >= mn->baseaddr &&
	    RELOC(sreplicate) < mn->baseaddr + mn->length)
	    continue;

	// Copy the replicated .text and .rodata section to each memory node
	memcpy(pa2kva(mn->baseaddr), sreplicate, size);
    }

    extern struct Pagemap bootpts_ktext[JOS_NCPU];
    uint64_t npages = ROUNDUP(size, PGSIZE) / PGSIZE;

    for (struct cpu * c = cpus; c < cpus + ncpu; c++) {
	struct memory_node *mn = &memnode[c->nodeid];

	if (RELOC(sreplicate) >= mn->baseaddr &&
	    RELOC(sreplicate) < mn->baseaddr + mn->length)
	    continue;
	
	// Map local .text and .rodata starting at 0xffffffff80100000
	struct Pagemap *pts_ktext = &bootpts_ktext[c - cpus];
	for (uint64_t i = 0; i < npages; i++) {
	    ptent_t ent = (mn->baseaddr) + (i << PGSHIFT);
	    ent |= KPTE_BITS;
	    pts_ktext->pm_ent[i + 256] = ent;
	}
    }
}
