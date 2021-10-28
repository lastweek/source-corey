#include <kern/lockmacro.h>
#include <kern/debug.h>
#include <kern/kobj.h>
#include <kern/arch.h>
#include <kern/lib.h>
#include <kern/at.h>
#include <kern/uaccess.h>
#include <kern/pageinfo.h>
#include <inc/safeint.h>
#include <inc/error.h>

enum { debug_map = 0 };
enum { debug_interior = 0 };
enum { debug_bad_mapping = 1 };
enum { debug_tlb = 1 };

enum { lookup_cache = 1 };

static int at_pmap_fill(struct Address_tree *at, void *va, uint32_t reqflags);
static void at_tlbflush_others(struct Address_tree *at);

int
at_alloc(struct Address_tree **atp, char interior, proc_id_t pid)
{
    struct kobject *ko;
    int r = kobject_alloc(kobj_address_tree, &ko, pid);
    if (r < 0)
	return r;

    struct Pagemap *pgmap;
    r = page_map_alloc(&pgmap, interior, pid);
    if (r < 0)
	return r;

    struct Address_tree *at = &ko->at;
    rw_init(&at->at_map_lock);
    spin_init(&at->at_pgmap_lock);
    at->at_pgmap = pgmap;
    at->at_interior = interior;
    if (interior)
	memset(pgmap, 0, sizeof(struct Pagemap));
    aml_init(&at->at_map_list);
    darray_init(&at->at_uaddrmap, sizeof(struct u_address_mapping), 0, pid);
    darray_init(&at->at_addrmap, sizeof(struct address_mapping2), 0, pid);

    LIST_INIT(&at->at_ps_list);
    rec_init(&at->at_ps_list_lock);
    spin_init(&at->at_obj_lock);
    
    *atp = at;
    return 0;
}

static uint64_t
at_nents(const struct Address_tree *at)
{
    return darray_get_nent(&at->at_uaddrmap);
}

static int
at_resize(struct Address_tree *at, uint64_t nent)
{
    if (nent < at->at_uaddrmap.da_nent)
	at->at_range.uam = 0;
    return darray_set_nent(&at->at_uaddrmap, nent, 1);
}

static int __attribute__ ((warn_unused_result))
at_get_uaddrmap(struct Address_tree *at,
		struct u_address_mapping **amp, uint64_t ami)
{
    return darray_get(&at->at_uaddrmap, ami, (void **)amp, page_excl);
}

static int __attribute__ ((warn_unused_result))
at_get_addrmap(struct Address_tree *at, struct address_mapping2 **amp, 
	       uint64_t smi)
{
    int r = darray_get(&at->at_addrmap, smi, (void **)amp, page_excl);    
    if (r == -E_INVAL) {
	r = darray_grow_nent(&at->at_addrmap, ROUNDUP(smi + 1, N_ADDRMAP_PER_PAGE));
	if (r < 0)
	    return r;
	r = darray_get(&at->at_addrmap, smi, (void **)amp, page_excl);
    }
    return r;
}

static int __attribute__ ((warn_unused_result))
at_check_uat(struct Address_tree *at, struct u_address_tree *uat, 
	     struct u_address_mapping **uam, uint64_t *sizep, uint64_t *nentp)
{
    int r = check_user_access(at, uat, sizeof(*uat), SEGMAP_WRITE);
    if (r < 0)
	return r;

    uint64_t size = uat->size;
    uint64_t nent = uat->nent;
    struct u_address_mapping *ents = uat->ents;
    int overflow = 0;
    r = check_user_access(at, ents,
			  safe_mul64(&overflow, sizeof(*ents), size),
			  SEGMAP_WRITE);
    if (r < 0)
	return r;

    if (overflow)
	return -E_INVAL;

    *uam = ents;
    if (sizep)
	*sizep = size;
    if (nentp)
	*nentp = nent;

    return 0;
}

static void
at_clear_mappings(struct Address_tree *at)
{
    for (uint64_t i = 0; i < at_nents(at); i++) {
	struct u_address_mapping *uam = 0;
	assert(at_get_uaddrmap(at, &uam, i) == 0);
	if (!uam->flags)
	    continue;

	struct address_mapping2 *am;
	int r = at_get_addrmap(at, &am, i);
	if (r < 0)
	    continue;

	if (am->am_kobj) {
	    spin_lock(&am->am_lock);
	    if (am->am_kobj) {
		aml_remove(aml_get_list(am->am_kobj), am);
		am->am_kobj = 0;
	    }
	    spin_unlock(&am->am_lock);
	}	
    }
}

static void
at_invalidate_range(struct Address_tree *at, void *first, void *last)
{
    spin_lock(&at->at_pgmap_lock);
    page_map_invalidate(at->at_pgmap, first, last);
    spin_unlock(&at->at_pgmap_lock);
}

static void
at_invalidate(struct Address_tree *at, struct Processor *ps)
{
    assert(at->at_interior == 0);

    if (ps->ps_at != at)
	return;
    if (ps == processor_sched())
	at_invalidate_range(at, 0, (void*)ULIM - PGSIZE);
    else
	panic("missing IPIs");
}

static uint64_t
at_va_to_segment_page(const struct u_address_mapping *uam, void *va)
{
    void *uam_va = ROUNDDOWN(uam->va, PGSIZE);
    assert(va >= uam_va && va < uam_va + uam->num_pages * PGSIZE);
    uint64_t mapping_page = ((uintptr_t) (va - uam_va)) / PGSIZE;
    return uam->start_page + mapping_page;
}

static int
kobj_get(struct Address_tree *at, struct sobj_ref ref, 
	 struct kobject **kobj, kobject_type_t type)
{
    int r;
    struct kobject *ko;
    if (!lookup_cache) {
	r = processor_co_obj(processor_sched(), ref, 
			     &ko, type);
	*kobj = ko;
	return r;
    }

    uint64_t ps_id = processor_sched()->ps_ko.ko_id;

    spin_lock(&at->at_obj_lock);
    if (ps_id == at->at_obj.ps_id && at->at_obj.ref.object) {
	if (at->at_obj.ref.object == ref.object && 
	    at->at_obj.ref.share == ref.share)
        {
	    *kobj = at->at_obj.ko;
	    spin_unlock(&at->at_obj_lock);
	    return 0;
	}
    }
    
    r = processor_co_obj(processor_sched(), ref, 
			 &ko, type);
    if (r < 0) {
	spin_unlock(&at->at_obj_lock);
	return r;
    }

    if (at->at_obj.ref.object)
	kobject_decref(&at->at_obj.ko->hdr);

    at->at_obj.ref = ref;
    at->at_obj.ko = ko;
    at->at_obj.ps_id = ps_id;
    kobject_incref(&ko->hdr);
    
    *kobj = ko;
    spin_unlock(&at->at_obj_lock);

    return 0;
}

static void
kobj_cache_flush(struct Address_tree *at)
{
    if (!lookup_cache)
	return;

    spin_lock(&at->at_obj_lock);
    if (at->at_obj.ref.object)
	kobject_decref(&at->at_obj.ko->hdr);
    at->at_obj.ref.object = 0;
    spin_unlock(&at->at_obj_lock);
}

static int
at_pmap_fill_mapping(struct Address_tree *at, struct u_address_mapping *uam, 
		     void *va, int reqflags)
{
    int r;
    struct address_mapping2 *am;

    if (uam->type == address_mapping_interior) {
	if ((uam->flags & SEGMAP_WRITE) == 0)
	    return -E_INVAL;

	debug(debug_interior, "rat (%ld.%s), va 0x%012lx reqflags %04x",
	      at->at_ko.ko_id, at->at_ko.ko_name, (uint64_t)va, reqflags);

	struct kobject *ko;
	r = kobj_get(at, uam->object, &ko, kobj_address_tree);

	//r = processor_co_obj(processor_sched(), uam->object, 
	//&ko, kobj_address_tree);
	if (r < 0)
	    return r;

	struct Address_tree *at_i = &ko->at;
	if (!at_i->at_interior)
	    return -E_INVAL;

	r = at_get_addrmap(at, &am, uam->kslot);
	if (r < 0)
	    return r;

	assert(am->am_kobj == 0 || am->am_kobj == (struct kobject *)at_i);
	
	// We may have already filled in from this AT
	if (am->am_kobj == 0) {
	    spin_lock(&am->am_lock);
	    if (am->am_kobj == 0) {
		am->am_parent = at;
		am->am_at_slot = uam->kslot;
		am->am_sh = uam->object.share;
		am->am_kobj = (struct kobject *)at_i;
		am->am_ps = processor_sched();
		am->am_va_first = uam->va;
		am->am_va_last = uam->va + (uam->num_pages - 1) * PGSIZE;
		aml_insert(&at_i->at_map_list, am);
	    }
	    spin_unlock(&am->am_lock);

	    spin_lock(&at->at_pgmap_lock);
	    r = as_arch_putinterior(at->at_pgmap, va, at_i->at_pgmap, 
				    at_i->at_ko.ko_pid);
	    spin_unlock(&at->at_pgmap_lock);
	    
	    if (r < 0)
		return r;
	}

	void *va_interior = va - (uint64_t)ROUNDDOWN(uam->va, PISIZE);
	r = at_pmap_fill(at_i, va_interior, reqflags);

	debug(debug_interior, "filled iat (%ld.%s), va_interior 0x%012lx",
	      at_i->at_ko.ko_id, at_i->at_ko.ko_name, (uint64_t)va_interior);

	return r;
    }

    // We could do some pre-faulting here..
    void *va_begin = va;
    void *va_limit = va + PGSIZE;
    
    struct kobject *ko;
    //r = processor_co_obj(processor_sched(), uam->object, &ko, kobj_segment);
    r = kobj_get(at, uam->object, &ko, kobj_segment);
    if (r < 0)
	return r;
    struct Segment *sg = &ko->sg;

    r = at_get_addrmap(at, &am, uam->kslot);
    if (r < 0)
	return r;
    assert(am->am_kobj == 0 || am->am_kobj == (struct kobject *)sg);

    debug(debug_map, "filling from Segment (%ld.%s)",
	  sg->sg_ko.ko_id, sg->sg_ko.ko_name);

    // We may have already filled in a page from this Segment
    if (am->am_kobj == 0) {
	spin_lock(&am->am_lock);
	if (am->am_kobj == 0) {
	    am->am_parent = at;
	    am->am_at_slot = uam->kslot;
	    am->am_sh = uam->object.share;
	    am->am_ps = processor_sched();
	    am->am_va_first = uam->va;
	    am->am_va_last = uam->va + (uam->num_pages - 1) * PGSIZE;
	    am->am_kobj = (struct kobject *)sg;
	    aml_insert(&sg->sg_map_list, am);
	}
	spin_unlock(&am->am_lock);
    }

    for (void *va_fill = va_begin; va_fill != va_limit; va_fill += PGSIZE) {
	uint64_t pnum = at_va_to_segment_page(uam, va_fill);
	void *pp;

	page_sharing_mode mode = page_shared_cor;
	if (uam->flags & SEGMAP_WRITE)
	    mode = page_shared_cow;

	r = segment_get_page(sg, pnum, &pp, mode);
	if (r < 0)
	    return r;

	struct page_info *pi = page_to_pageinfo(pp);
	if (!pi->pi_clear) {
	    spin_lock(&pi->pi_clear_lock);
	    if (!pi->pi_clear) {
		page_zero(pp);
		pi->pi_clear = 1;
	    }
	    spin_unlock(&pi->pi_clear_lock);
	}
	    
	spin_lock(&at->at_pgmap_lock);
	r = as_arch_putpage(at->at_pgmap, va_fill, pp, uam->flags,
			    at->at_ko.ko_pid);
	spin_unlock(&at->at_pgmap_lock);
	if (r < 0)
	    return r;

	debug(debug_map, "mapped va 0x%012lx with flags %04x",
	      (uint64_t)va_fill, uam->flags);
    }
    return 0;
}

static int
at_pmap_fill(struct Address_tree *at, void *va, uint32_t reqflags)
{
    debug(debug_map, "at (%016lx.%s), va 0x%012lx reqflags %04x",
	  at->at_ko.ko_id, at->at_ko.ko_name, (uint64_t)va, reqflags);

    if (debug_map && at->at_interior && at->at_ko.ko_pid != arch_cpu())
	cprintf("at_pmap_fill: %u filling at on %u from rip %lx\n", 
		arch_cpu(), at->at_ko.ko_pid, processor_sched()->ps_tf.tf_rip);
	
    rw_read_lock(&at->at_map_lock);

    if (at->at_range.uam && at->at_range.uam->flags &&
	va >= at->at_range.va_start && va < at->at_range.va_end)
    {
	uint64_t flags = at->at_range.uam->flags;
	if ((flags & reqflags) == reqflags) {
	    void *va_align = ROUNDDOWN(va, PGSIZE);
	    int r = at_pmap_fill_mapping(at, at->at_range.uam, 
					 va_align, reqflags);
	    rw_read_unlock(&at->at_map_lock);
	    return r;
	}
    }

    for (uint64_t i = 0; i < at_nents(at); i++) {
	struct u_address_mapping *uam;
	int r = at_get_uaddrmap(at, &uam, i);
	if (r < 0) {
	    rw_read_unlock(&at->at_map_lock);
	    return r;
	}

	uint64_t flags = uam->flags;
	if (!(flags & SEGMAP_READ))
	    continue;
	if ((flags & reqflags) != reqflags)
	    continue;

	if (uam->type != address_mapping_segment &&
	    uam->type != address_mapping_interior)
	{
	    if (debug_bad_mapping)
		cprintf("at_pmap_fill: bad mapping %u, "
			"i %lu kslot %u object %ld.%ld\n",
			uam->type, i, uam->kslot,
			uam->object.share, uam->object.object);
	    continue;
	}

	int of = 0;
	void *va_start = ROUNDDOWN(uam->va, PGSIZE);
	void *va_end = (void *) (uintptr_t)
	    safe_addptr(&of, (uintptr_t) va_start,
			safe_mulptr(&of, uam->num_pages, PGSIZE));

	if (of || va < va_start || va >= va_end)
	    continue;

	void *va_align = ROUNDDOWN(va, PGSIZE);

	at->at_range.va_start = va_start;
	at->at_range.va_end = va_end;
	at->at_range.uam = uam;

	r = at_pmap_fill_mapping(at, uam, va_align, reqflags);
	rw_read_unlock(&at->at_map_lock);	
	return r;
    }

    rw_read_unlock(&at->at_map_lock);
    return -E_INVAL;
}

static void
at_invalidate_uaddrmap(struct Address_tree *at, struct u_address_mapping *uam)
{
    struct address_mapping2 *am;
    int r = at_get_addrmap(at, &am, uam->kslot);
    if (r < 0)
	panic("unable to get addrmap: %s\n", e2s(r));

    if (!am->am_kobj)
	return;
	
    aml_remove(aml_get_list(am->am_kobj), am);
    at_invalidate_addrmap(at, am);
}

int
at_pagefault(struct Address_tree *at, void *va, uint32_t reqflags)
{
    int r = at_pmap_fill(at, va, reqflags);
    if (r < 0)
	kobj_cache_flush(at);
    return r;
}

void
at_detach(struct Address_tree *at, struct Processor *ps)
{
    assert(ps->ps_at == at);
    rec_lock(&at->at_ps_list_lock);
    LIST_REMOVE(ps, ps_at_link);
    rec_unlock(&at->at_ps_list_lock);
}

void
at_attach(struct Address_tree *at, struct Processor *ps)
{
    rec_lock(&at->at_ps_list_lock);
    LIST_INSERT_HEAD(&at->at_ps_list, ps, ps_at_link);
    rec_unlock(&at->at_ps_list_lock);
}

void
at_set_current(struct Address_tree *at)
{
    pmap_set_current(at ? at->at_pgmap : 0);
}

int
at_to_user(struct Address_tree *at, struct u_address_tree *uat)
{
    struct u_address_mapping *ents = 0;
    uint64_t size;
    int r = at_check_uat(processor_sched()->ps_at, uat, &ents, &size, 0);
    if (r < 0)
	return r;

    rw_read_lock(&at->at_map_lock);
    uint64_t nent = 0;
    for (uint64_t i = 0; i < at_nents(at); i++) {
	struct u_address_mapping *uam;
	r = at_get_uaddrmap(at, &uam, i);
	if (r < 0)
	    goto done;

	if (uam->flags == 0)
	    continue;

	assert(uam->kslot == i);

	if (nent >= size) {
	    r = -E_NO_SPACE;
	    goto done;
	}

	ents[nent] = *uam;
	nent++;
    }

    uat->nent = nent;
    uat->trap_handler = (void *) at->at_utrap_entry;
    uat->trap_stack_base = (void *) at->at_utrap_stack_base;
    uat->trap_stack_top = (void *) at->at_utrap_stack_top;
 done:
    rw_read_unlock(&at->at_map_lock);
    return r;
}

int
at_from_user(struct Address_tree *at, struct u_address_tree *uat)
{
    struct u_address_mapping *ents = 0;
    uint64_t nent;
    int r = at_check_uat(processor_sched()->ps_at, uat, &ents, 0, &nent);
    if (r < 0)
	return r;

    rw_write_lock(&at->at_map_lock);
    // Shrinking AS'es is a little tricky, so we don't do it for now
    if (nent > at_nents(at)) {
	r = at_resize(at, nent);
	if (r < 0) {
	    rw_write_unlock(&at->at_map_lock);
	    return r;
	}
    }

    at_clear_mappings(at);

    for (uint64_t i = 0; i < at_nents(at); i++) {
	struct u_address_mapping *uam;
	r = at_get_uaddrmap(at, &uam, i);
	if (r < 0)
	    goto out;

	memset(uam, 0, sizeof(*uam));
	if (i < nent) {
	    // Sanitize address_mapping_interior mappings
	    if (ents[i].type == address_mapping_interior) {
		ents[i].num_pages = PIPAGES;
		ents[i].start_page = 0;
	    }
	    
	    ents[i].kslot = i;
	    *uam = ents[i];
	}
    }

    at->at_utrap_entry = (uintptr_t) uat->trap_handler;
    at->at_utrap_stack_base = (uintptr_t) uat->trap_stack_base;
    at->at_utrap_stack_top = (uintptr_t) uat->trap_stack_top;

    struct Processor *ps;

 out:
    rw_write_unlock(&at->at_map_lock);

    rec_lock(&at->at_ps_list_lock);
    LIST_FOREACH(ps, &at->at_ps_list, ps_at_link)
	at_invalidate(at, ps);
    rec_unlock(&at->at_ps_list_lock);
    return r;
}

int
at_set_uslot(struct Address_tree *at, struct u_address_mapping *uam_new)
{
    int r;
    if ((r = uaccess_start()) < 0)
	return r;
    struct u_address_mapping uam_copy = *uam_new;
    uaccess_stop();
    
    uint64_t slot = uam_copy.kslot;

    rw_write_lock(&at->at_map_lock);
    if (slot >= at_nents(at)) {
	r = at_resize(at, slot + 1);
	if (r < 0) {
	    rw_write_unlock(&at->at_map_lock);
	    return r;
	}
    }

    struct u_address_mapping *uam;
    r = at_get_uaddrmap(at, &uam, slot);
    if (r < 0) {
	rw_write_unlock(&at->at_map_lock);
	return r;
    }

    if (uam->flags)
	at_invalidate_uaddrmap(at, uam);

    // Sanitize address_mapping_interior mappings
    if (uam_copy.type == address_mapping_interior) {
	uam_copy.num_pages = PIPAGES;
	uam_copy.start_page = 0;
    }

    *uam = uam_copy;
    rw_write_unlock(&at->at_map_lock);
    return r;
}

void
at_scope_cb(struct Address_tree *at, kobject_id_t sh, 
	    struct Processor *scope_ps)
{
    kobj_cache_flush(at);
    if (at->at_interior) {
	aml_scope(&at->at_map_list, sh, scope_ps);
	return;
    }

    if (at == scope_ps->ps_at && sh == scope_ps->ps_atref.share)
	processor_halt(scope_ps);
}

void 
at_remove_cb(struct Address_tree *at, kobject_id_t sh)
{
    kobj_cache_flush(at);
    if (at->at_interior) {
	aml_invalidate(&at->at_map_list, sh);
	return;
    }
    
    rec_lock(&at->at_ps_list_lock);
    struct Processor *ps = LIST_FIRST(&at->at_ps_list);
    struct Processor *prev = 0;

    while (ps) {
	assert(ps->ps_at == at);
	if (ps->ps_atref.share == sh) {
	    if (processor_halt(ps)) {
		ps = prev ? LIST_NEXT(prev, ps_at_link) : 
		    LIST_FIRST(&at->at_ps_list);
		continue;
	    }
	}
	prev = ps;
	ps = LIST_NEXT(ps, ps_at_link);
    }
    rec_unlock(&at->at_ps_list_lock);
}

void
at_gc_cb(struct Address_tree *at)
{
    kobj_cache_flush(at);
    at_clear_mappings(at);
    darray_free(&at->at_uaddrmap);
    darray_free(&at->at_addrmap);
    page_map_free(at->at_pgmap);
}

static void
at_tlbflush_helper(struct address_mapping2 *am)
{
    assert(!am->am_parent->at_interior);
    at_tlbflush_others(am->am_parent);
}

static void
at_tlbflush_others(struct Address_tree *at)
{
    struct Processor *ps;
    if (at->at_interior) {
	aml_foreach(&at->at_map_list, at_tlbflush_helper);
    } else {
	int flag = 0;
	rec_lock(&at->at_ps_list_lock);	
	LIST_FOREACH(ps, &at->at_ps_list, ps_at_link)
	    if (ps != processor_sched()) {
		if (debug_tlb && !flag)
		    cprintf("at_tlbflush_others: remote flush on AT (%ld.%s)\n",
			    at->at_ko.ko_id, at->at_ko.ko_name);
		flag = 1;
		arch_tlbflush_mp(ps->ps_pid);
	    }
	rec_unlock(&at->at_ps_list_lock);
    }
}

void
at_invalidate_addrmap(struct Address_tree *at, struct address_mapping2 *am)
{
    kobj_cache_flush(at);
    if (am->am_kobj) {
	spin_lock(&am->am_lock);
	if (am->am_kobj) {
	    at_tlbflush_others(at);
	    at_invalidate_range(at, am->am_va_first, am->am_va_last);
	    am->am_kobj = 0;
	}
	spin_unlock(&am->am_lock);
    }
}
