#include <machine/memlayout.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/error.h>
#include <inc/assert.h>

#include <string.h>

enum { map_debug = 0 };
enum { n_base_mappings = 64 };
enum { usegmapents_bytes = UADDRMAPENTSEND - UADDRMAPENTS };

static struct u_address_tree cache_uat;
thread_mutex_t cache_uat_mu;

static int
cache_grow(void)
{
    struct sobj_ref atref = processor_current_as();
    int r = sys_at_get(atref, &cache_uat);
    if (r < 0)
	return r;

    struct u_address_mapping *cache_ents = (void *)UADDRMAPENTS;
    uint64_t nsize = cache_uat.size * 2;
    uint64_t nbytes = nsize * sizeof(*cache_ents);

    if (nbytes > usegmapents_bytes) {
	cprintf("cache_grow: huge cache_uas: size %lu bytes %lu\n", 
		nsize, nbytes);
	return -E_NO_MEM;
    }
    
    for (uint64_t i = 0; i < cache_uat.nent; i++) {
	if (cache_uat.ents[i].flags && 
	    cache_uat.ents[i].va == (void *) cache_ents) 
	{
	    r = sys_segment_set_nbytes(cache_uat.ents[i].object, nbytes);
	    if (r < 0)
		return r;
	    cache_uat.size = nsize;
	    return 0;
	}
    }
    
    return -E_NOT_FOUND;
}

static void
cache_refresh(struct sobj_ref atref)
{
    static struct sobj_ref cached_atref;
    if (cached_atref.object != atref.object) {
	int r = sys_at_get(atref, &cache_uat);
	if (r < 0)
	    panic("cache_refresh failed: %s", e2s(r));
	cached_atref = atref;
    }
}

static int64_t
cache_get_kslot(void)
{
    uint64_t kslot = 0;
    uint64_t i = 0;
    for (; i < cache_uat.nent; i++) {
        if (!cache_uat.ents[i].flags || cache_uat.ents[i].kslot != kslot)
            break;
	kslot = cache_uat.ents[i].kslot + 1;
    }

    if (i == cache_uat.size) {
	int r = cache_grow();
	if (map_debug)
	    cprintf("as_map: cache_grow to %lu: %s\n", cache_uat.size, e2s(r));
	if (r < 0)
	    return r;
    }

    return kslot;
}

void
as_print_uas(struct u_address_tree *uat)
{
    cprintf("slot  kslot    share  segment  start  npages  fl   t  va\n");
    for (uint64_t i = 0; i < uat->nent; i++) {
	char name[JOS_KOBJ_NAME_LEN];
	name[0] = '\0';
	sys_obj_get_name(uat->ents[i].object, &name[0]);

	if (uat->ents[i].flags == 0)
	    continue;
	cprintf("%4ld  %5d  %7ld  %7ld  "
		"%5ld  %6ld  %03x  %02x  %p (%s)\n",
		i, uat->ents[i].kslot,
		uat->ents[i].object.share,
		uat->ents[i].object.object,
		uat->ents[i].start_page,
		uat->ents[i].num_pages,
		uat->ents[i].flags,
		uat->ents[i].type,
		uat->ents[i].va,
		name);
    }
}

void
as_print_current_uas(void)
{
    struct sobj_ref asref= processor_current_as();
    thread_mutex_lock(&cache_uat_mu);            
    cache_refresh(asref);
    as_print_uas(&cache_uat);
    thread_mutex_unlock(&cache_uat_mu);
}

int
as_lookup(void *va, struct u_address_mapping *uam)
{
    struct sobj_ref atref = processor_current_as();

    thread_mutex_lock(&cache_uat_mu);            
    cache_refresh(atref);
        
    for (uint64_t i = 0; i < cache_uat.nent; i++) {
        if ((cache_uat.ents[i].va == va) && cache_uat.ents[i].flags) {
	    assert(cache_uat.ents[i].type == address_mapping_segment);
	    memcpy(uam, &cache_uat.ents[i], sizeof(*uam));
	    thread_mutex_unlock(&cache_uat_mu);    
            return 1;
        }
    }

    thread_mutex_unlock(&cache_uat_mu);        
    return 0;
}

int
as_unmap(void *va)
{
    struct sobj_ref atref = processor_current_as();
    int r = 0;

    thread_mutex_lock(&cache_uat_mu);    
    cache_refresh(atref);
    for (uint64_t i = 0; i < cache_uat.nent; i++) {
        if ((cache_uat.ents[i].va == va) && cache_uat.ents[i].flags) {
	    struct u_address_mapping uam = cache_uat.ents[i];
	    uam.flags = 0;
	    r = sys_at_set_slot(atref, &uam);
            if (r < 0)
		goto done;
	}
    }

 done:
    assert(sys_at_get(atref, &cache_uat) == 0);
    thread_mutex_unlock(&cache_uat_mu);    
    return r;
}

static void *
cache_get_va(uint64_t bytes)
{
    static const uint64_t map_base = UASMANAGERBASE;
    static const uint64_t map_end  = UASMANAGEREND;
    static uint64_t map_ptr = map_base;
    static char dumb_exhausted = 0;

    bytes = ROUNDUP(bytes, PGSIZE);

    if (!dumb_exhausted) {
	if (map_ptr + bytes > map_end) {
	    cprintf("cache_get_va: dumb VA allocation exhausted\n");
	    dumb_exhausted = 1;
	} else {
	    void *va = (void *) map_ptr;
	    map_ptr += bytes;
	    return va;
	}
    }

    uint64_t try_start = map_base;
    uint64_t try_end = try_start + bytes;

    // XXX O(n^2) 
 again:
    for (uint64_t i = 0; i < cache_uat.nent; i++) {
	uint64_t start = (uint64_t)cache_uat.ents[i].va;
	uint64_t end = start + cache_uat.ents[i].num_pages * PGSIZE;

	if ((start <= try_start && try_start < end) ||
	    (start < try_end && try_end <= end) ||
	    (try_start < start && end < try_end))
	{
	    try_start = end;
	    try_end = end + bytes;
	    goto again;
	}
    }
    
    if (map_end < try_end)
	panic("out of address space, end 0x%lx req 0x%lx\n", map_end, bytes);

    return (void *)try_start;
}

int
as_map(struct sobj_ref sgref, uint64_t start_byteoff, uint64_t flags, void **va_p, 
       uint64_t *bytes_store)
{
    int64_t r;
    assert((start_byteoff % PGSIZE) == 0);
    
    if (map_debug)
	cprintf("as_map: sg %lu.%lu off %lx flags %lx va %p\n", 
		sgref.share, sgref.object, start_byteoff, 
		flags, va_p ? *va_p : 0);

    uint64_t sg_bytes;
    if (bytes_store && *bytes_store)
        sg_bytes = *bytes_store + start_byteoff;
    else {
        int64_t n = sys_segment_get_nbytes(sgref);
        if (n < 0) {
            cprintf("as_map: cannot stat segment: %s\n", e2s(n));
            return n;
        }
        sg_bytes = n;
    }

    uint64_t map_bytes = ROUNDUP(sg_bytes - start_byteoff, PGSIZE);
    
    struct sobj_ref atref = processor_current_as();
    thread_mutex_lock(&cache_uat_mu);
    cache_refresh(atref);

    r = cache_get_kslot();
    if (r < 0)
	goto done;
    uint64_t kslot = r;

    // If a va_p holds non-zero, use that value blindly
    void *map_va;
    if (va_p && *va_p)
	map_va = *va_p;
    else
	map_va = cache_get_va(map_bytes);

    struct u_address_mapping mapping;
    memset(&mapping, 0, sizeof(mapping));
    mapping.type = address_mapping_segment;
    mapping.object = sgref;
    mapping.start_page = start_byteoff / PGSIZE;
    mapping.num_pages = map_bytes / PGSIZE;
    mapping.flags = flags;
    mapping.kslot = kslot;
    mapping.va = map_va;

    r = sys_at_set_slot(atref, &mapping);
    if (r < 0) {
        cprintf("as_map: failed to set AS slot %lu: %s\n", kslot, e2s(r));
	goto done;
    }

    assert(sys_at_get(atref, &cache_uat) == 0);
    
    if (bytes_store)
        *bytes_store = sg_bytes - start_byteoff;
    if (va_p)
        *va_p = map_va;

 done:
    thread_mutex_unlock(&cache_uat_mu);

    return r;
}

int
at_map_interior(struct sobj_ref intref, uint64_t flags, void *va)
{
    int64_t r = 0;
    struct sobj_ref atref = processor_current_as();
    thread_mutex_lock(&cache_uat_mu);
    cache_refresh(atref);

    r = cache_get_kslot();
    if (r < 0)
	goto done;
    uint64_t kslot = r;
    
    struct u_address_mapping mapping;
    memset(&mapping, 0, sizeof(mapping));
    mapping.type = address_mapping_interior;
    mapping.object = intref;
    mapping.flags = SEGMAP_READ | SEGMAP_WRITE | flags;
    mapping.kslot = kslot;
    mapping.va = va;
    r = sys_at_set_slot(atref, &mapping);
    if (r < 0)
	goto done;
    
    assert(sys_at_get(atref, &cache_uat) == 0);

 done:
    thread_mutex_unlock(&cache_uat_mu);
    return r;
}

int
as_set_utrap(void *entry, void *stack_base, void *stack_top)
{
    struct sobj_ref atref = processor_current_as();

    thread_mutex_lock(&cache_uat_mu);    
    cache_refresh(atref);

    cache_uat.trap_handler = entry;
    cache_uat.trap_stack_base = stack_base;
    cache_uat.trap_stack_top = stack_top;

    int r = sys_at_set(atref, &cache_uat);
    thread_mutex_unlock(&cache_uat_mu);    
    if (r < 0) {
	cprintf("as_set_utrap: failed to set as: %s\n", e2s(r));
	return r;
    }
    
    return 0;
}

void
as_init(void)
{
    struct u_address_mapping uam_ents[n_base_mappings];
    struct u_address_tree uat;
    memset(&uat, 0, sizeof(uat));
    uat.size = n_base_mappings;
    uat.ents = &uam_ents[0];
    
    struct sobj_ref atref = processor_current_as();
    int r = sys_at_get(atref, &uat);
    if (r < 0)
	panic("sys_as_get failed: %s", e2s(r));

    struct u_address_mapping *cache_ents = (void *)UADDRMAPENTS;
    int64_t sg_id = sys_segment_alloc(core_env->sh, 
				      n_base_mappings * sizeof(*cache_ents),
				      "as-segmapents",
				      core_env->pid);
    if (sg_id < 0)
	panic("sys_segment_alloc failed: %s", e2s(sg_id));
    
    uint64_t slot = uat.nent++;
    uat.ents[slot].type = address_mapping_segment;
    uat.ents[slot].object = SOBJ(core_env->sh, sg_id);
    uat.ents[slot].start_page = 0;
    uat.ents[slot].num_pages = ROUNDUP(usegmapents_bytes, PGSIZE) / PGSIZE;
    uat.ents[slot].flags = SEGMAP_READ | SEGMAP_WRITE;
    uat.ents[slot].va = (void *) cache_ents;
    
    r = sys_at_set(atref, &uat);
    if (r < 0)
	panic("sys_as_set failed: %s\n", e2s(r));

    cache_uat.size = n_base_mappings;
    cache_uat.ents = cache_ents;
    cache_refresh(atref);

    thread_mutex_init(&cache_uat_mu);
}
