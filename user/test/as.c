#include <machine/memlayout.h>
#include <machine/compiler.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/arch.h>
#include <test.h>
#include <string.h>

static void
uat_reset(struct u_address_tree *uat, struct u_address_mapping *ents, uint64_t n)
{
    memset(uat, 0, sizeof(*uat));
    memset(ents, 0, n * sizeof(*ents));
    uat->size = n;
    uat->ents = ents;
}

static struct u_address_tree *
uat_get(void)
{
    enum { uat_size = 64 };
    static struct u_address_mapping uat_ents[uat_size];
    static struct u_address_tree uat;
    uat_reset(&uat, uat_ents, uat_size);
    echeck(sys_at_get(processor_current_as(), &uat));
    return &uat;
}

static void __attribute__((unused))
as_test_basic(void)
{
    enum { iters = 500 } ;
    enum { sg_count = 16 };
    enum { sg_bytes = 4096 };

    struct u_address_tree *uat = uat_get();

    uint64_t npages = ROUNDUP(sg_bytes, PGSIZE) / PGSIZE;
    uint64_t sg_ids[sg_count];

    uint64_t va_ptr = UADDRMAPENTSEND;

    cprintf("Testing basic mapping/unmapping, %u times\n", iters);
    for (uint64_t k = 0; k < iters; k++) {
	if ((k % 100) == 0)
	    cprintf(" %ld\n", k);

	for (int i = 0; i < sg_count; i++)
	    echeck(sg_ids[i] = sys_segment_alloc(core_env->sh, npages * PGSIZE, 
						 "as_test-seg", core_env->pid));

	uint64_t count = sg_count * npages * PGSIZE;
	void *map_start = (void *)va_ptr;
	va_ptr += count;

	void *va = map_start;
	for (uint32_t i = uat->nent; i < sg_count + uat->nent; i++, va += npages * PGSIZE) {
	    uat->ents[i].type = address_mapping_segment;
	    uat->ents[i].object = SOBJ(core_env->sh, sg_ids[i - uat->nent]);
	    uat->ents[i].start_page = 0;
	    uat->ents[i].num_pages = npages;
	    uat->ents[i].flags = SEGMAP_READ | SEGMAP_WRITE;
	    uat->ents[i].va = (void *)va;
	}
	uat->nent += sg_count;
	
	echeck(sys_at_set(processor_current_as(), uat));

	char *dst = (char *)map_start;
	memset(dst, 0xde, count);
	
	for (int i = 0; i < sg_count; i++)
	    echeck(sys_share_unref(SOBJ(core_env->sh, sg_ids[i])));

	uat->nent -= sg_count;
	echeck(sys_at_set(processor_current_as(), uat));
    }
    cprintf("Testing basic done!\n");
}

static void __attribute__((unused))
as_test_funny(void)
{
   cprintf("Test adding funny mappings\n");
   struct u_address_tree *uat = uat_get();
   assert(uat->nent < uat->size);

   struct u_address_mapping test_mapping[] = {
       { .start_page = 0, 
	 .num_pages = 1, 
	 .va = (void *)ULIM - PGSIZE - 3 },
       { .start_page = 0,
	 .num_pages = 10000,
	 .va = (void *)ULIM - PGSIZE },
       { .start_page = 0,
	 .num_pages = 10000,
	 .va = (void *)ULIM - PGSIZE },
   };
   uint64_t bytes[] = { 3, PGSIZE, PGSIZE * 2, };

   uint64_t n = sizeof(test_mapping) / sizeof(test_mapping[0]);

   struct u_address_mapping *sm = &uat->ents[uat->nent];
   uat->nent++;
   
   for (uint64_t i = 0; i < n; i++) {
       int64_t sg_id;
       echeck(sg_id = sys_segment_alloc(core_env->sh, bytes[i], "as byte seg", 
					core_env->pid));   
       *sm = test_mapping[i];
       sm->object = SOBJ(core_env->sh, sg_id);
       sm->flags = SEGMAP_READ | SEGMAP_WRITE;
       echeck(sys_at_set(processor_current_as(), uat));   
       memset(sm->va, 0xCA, bytes[i]);
       sys_share_unref(SOBJ(core_env->sh, sg_id));
   }

   cprintf("Test funny mappnigs done!\n");
}

static volatile int shared_flag JSHARED_ATTR;
static struct sobj_ref psref JSHARED_ATTR;

static void __attribute__((unused))
unmap_loop(uint64_t psid, struct sobj_ref at, void *va_base, void *va, 
	   uint32_t kslot)
{
    enum { iters = 1000 };

    int64_t r;
    struct u_address_mapping uam;

    void *x = va_base + (uint64_t)va;

    cprintf(" Testing unmapping from ps %ld, %u times\n", psid, iters);
    for (int i = 0; i < iters; i++) {
	echeck(r = sys_segment_alloc(core_env->sh, PGSIZE, "foo", core_env->pid));
	struct sobj_ref sg = SOBJ(core_env->sh, r);
	uam.type = address_mapping_segment;
	uam.object = sg;
	uam.kslot = kslot;
	uam.flags = SEGMAP_READ | SEGMAP_WRITE;
	uam.va = va;
	uam.start_page = 0;
	uam.num_pages = 1;
	echeck(sys_at_set_slot(at, &uam));
	
	memset(x, 0, PGSIZE);
	
	uam.flags = 0;
	echeck(sys_at_set_slot(at, &uam));
	echeck(sys_share_unref(sg));
    }
    cprintf(" Testing unmapping from ps %ld done!\n", psid);
}

static void __attribute__((noreturn))
test_entry(void)
{
    struct sobj_ref at = processor_current_as();
    shared_flag = 1;
    // See memlayout.h
    void *va = (void *) UINT64(0x0000700601000000);
    unmap_loop(psref.object, at, 0, va, 64);
    shared_flag = 2;
    echeck(sys_processor_halt(psref));    
    panic("didn't halt?");
}

static void __attribute__((unused))
as_test_shared(void)
{
    struct sobj_ref atref = processor_current_as();

    int64_t ps_id;
    echeck(ps_id = sys_processor_alloc(core_env->sh, "test-processor", 1));

    psref = SOBJ(core_env->sh, ps_id);

    // Start a new Processor with the current AT..must be careful with lib/
    // because don't init the thread environment on the new Processor.
    static char buf[2 * 4096];
    static void *stacktop = buf + sizeof(buf);
    stacktop = ROUNDDOWN(stacktop, 16) - 8;
    memset(stacktop, 0, 8);
    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = atref;
    uc.uc_entry = (void *) &test_entry;
    uc.uc_stack = stacktop;;
    uc.uc_share[0] = SOBJ(core_env->sh, core_env->sh);
    echeck(sys_processor_vector(SOBJ(core_env->sh, ps_id), &uc));

    while (shared_flag < 1)
	arch_pause();

    // See memlayout.h
    void *va = (void *) UINT64(0x0000700601000000) + PGSIZE;
    unmap_loop(core_env->psref.object, atref, 0, va, 65);
   
    while (shared_flag < 2)
	arch_pause();
    
    echeck(sys_share_unref(psref));
}

static void __attribute__((unused))
as_test_interior(void)
{
    enum { bytes = PGSIZE };
    enum { big_bytes = 1024 * PGSIZE };

    cprintf("Testing interior at simple...\n");

    int64_t shid;
    echeck(shid = sys_share_alloc(core_env->sh, 1 << kobj_segment,
    				  "test-share", core_env->pid));
    struct sobj_ref shref = SOBJ(core_env->sh, shid);
    
    int64_t r;
    // See memlayout.h
    void *va = (void *) ULINKSTART;
    assert(((uint64_t)va % PISIZE) == 0);

    echeck(r = sys_at_alloc(core_env->sh, 1, "at-test", core_env->pid));
    struct sobj_ref atref = SOBJ(core_env->sh, r);
    echeck(at_map_interior(atref, SEGMAP_SHARED, va));  
    unmap_loop(core_env->psref.object, atref, va, 0, 64);
   
    cprintf("Testing interior at simple done!\n");


    cprintf("Testing interior at less simple...\n");
    void *va2 = va + PISIZE;
    echeck(r = sys_at_alloc(core_env->sh, 1, "at-test2", core_env->pid));
    struct sobj_ref atref2 = SOBJ(core_env->sh, r);
    echeck(at_map_interior(atref2, SEGMAP_SHARED, va2));  

    echeck(r = sys_segment_alloc(shid, big_bytes, "foo2", core_env->pid));
    struct sobj_ref sgref2 = SOBJ(shid, r);
    
    struct sobj_ref at[2] = { atref, atref2 };
    uint64_t map_size = big_bytes / 2;
    for (uint64_t i = 0; i < 2 ; i++) {        
	struct u_address_mapping mapping ;
        memset(&mapping, 0, sizeof(mapping));

        mapping.type = address_mapping_segment;
        mapping.object = sgref2;
        mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
        mapping.kslot = 0;
        mapping.va = 0;
        mapping.start_page = i * (map_size / PGSIZE);
        mapping.num_pages = map_size / PGSIZE;
        echeck(sys_at_set_slot(at[i], &mapping));    
    }

    memset(va, 1, map_size);

    shared_flag = 0;
    r = pforkv(1, 0, &shref, 1);
    echeck(r);
    if (r != 0) {
	memset(va, 1, map_size);
	time_delay_cycles(100000);
	shared_flag = 1;
    } else {
	while(!shared_flag)
	    arch_pause();
	memset(va, 1, map_size);
	memset(va2, 1, map_size);
	processor_halt();
    }
    time_delay_cycles(100000);
    memset(va, 1, map_size);
    memset(va2, 1, map_size);

    cprintf("Test interior at less simple done!\n");
}

void
as_test(void)
{
    //as_test_funny();
    as_test_basic();
    as_test_shared();
    as_test_interior();
}
