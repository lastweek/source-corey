#include <machine/mmu.h>
#include <machine/compiler.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/copy.h>
#include <inc/error.h>
#include <inc/arch.h>
#include <test.h>
#include <string.h>

static void __attribute__((unused))
verify_mem(void *va, int c, uint64_t size)
{
    for (uint64_t i = 0; i < size; i++)
        assert(*((uint8_t *)va) == c);
}

static int64_t
alloc_and_map(void **va, uint64_t size)
{
    int64_t sg = sys_segment_alloc(core_env->sh, size, "foo", core_env->pid);
    if (sg < 0)
        return sg;
    echeck(as_map(SOBJ(core_env->sh, sg), 0, SEGMAP_READ | SEGMAP_WRITE, va, 0));
    return sg;
}

static int64_t
copy_and_map(uint64_t sg_id, void **va, page_sharing_mode mode)
{
    int64_t sg2 = sys_segment_copy(core_env->sh, 
				   SOBJ(core_env->sh, sg_id), 
				   "foo2", mode, core_env->pid);
    if (sg2 < 0)
        return sg2;
    echeck(as_map(SOBJ(core_env->sh, sg2), 0, 
		  SEGMAP_READ | SEGMAP_WRITE, va, 0));
    return sg2;
}

static int
alloc_copy_map(uint64_t *sg, void **va, 
	       uint64_t *sg2, void **va2, 
	       int size, page_sharing_mode mode)
{
    int64_t s = alloc_and_map(va, size);
    if (s < 0)
        return s;
    *sg = s;
    memset(*va, 0xff, size);
    
    s = copy_and_map(*sg, va2, mode);
    if (s < 0) {
        as_unmap(va);
        assert(sys_share_unref(SOBJ(core_env->sh, *sg)) == 0);
        return s;
    }
    *sg2 = s;
    return 0;
}

static void
unmap_unref(uint64_t sg, void *va, uint64_t sg2, void *va2)
{
    as_unmap(va);
    as_unmap(va2);
    assert(sys_share_unref(SOBJ(core_env->sh, sg)) == 0);
    assert(sys_share_unref(SOBJ(core_env->sh, sg2)) == 0);
}

static void __attribute__((unused))
segment_copy_test(void)
{
    int r;
    
    char warned = 0;
#define warn_once(r)                                            \
    if (!warned) {                                              \
        warned = 1;                                             \
        cprintf("%s:%d: %s\n", __FUNCTION__, __LINE__, e2s(r)); \
    }
    
    enum { iters = 1000 };
    enum { segment_size_small = 1 * PGSIZE };
    enum { segment_size_medium = 19 * PGSIZE };
    enum { segment_size_large = 509 * PGSIZE };
    cprintf("Segment copy test\n");

    cprintf("Test dumb copy %d times, with size %d:\n", iters, segment_size_small);
    for (int i = 0; i < iters; i++) {
        if ((i % 200 ) == 0)
            cprintf(" %d\n", i);
        void *va = 0;
        void *va2 = 0;
        uint64_t sg = 0;
        uint64_t sg2 = 0;
        
        r = alloc_copy_map(&sg, &va, &sg2, &va2, segment_size_small, page_excl);
        if (r < 0) {
            warn_once(r);
            continue;
        }        
        memset(va, 0xde, segment_size_small);
        memset(va2, 0xce, segment_size_small);
        verify_mem(va, 0xde, segment_size_small);        
        verify_mem(va2, 0xce, segment_size_small);        
        unmap_unref(sg, va, sg2, va2);
    }
    cprintf("Test dumb copy done!\n");

    cprintf("Test copy-on-write %d times, with size %d:\n", iters, segment_size_medium);
    for (int i = 0; i < iters; i++) {
        if ((i % 200 ) == 0)
            cprintf(" %d\n", i);
        void *va = 0;
        void *va2 = 0;
        uint64_t sg = 0;
        uint64_t sg2 = 0;
        
        r = alloc_copy_map(&sg, &va, &sg2, &va2, 
			   segment_size_medium, 
			   page_shared_cow);
        if (r < 0) {
            warn_once(r);
            continue;
        }
        
        switch ((i % 2)) {
        case 0:
            memset(va, 0xde, segment_size_medium);
            verify_mem(va, 0xde, segment_size_medium);        
            verify_mem(va2, 0xff, segment_size_medium);        
            break;
        case 1:
            memset(va2, 0xce, segment_size_medium);
            memset(va, 0xde, segment_size_medium);
            verify_mem(va, 0xde, segment_size_medium);        
            verify_mem(va2, 0xce, segment_size_medium);        
            break;
        default:
            panic("test case not handled");
            break;
        }
        unmap_unref(sg, va, sg2, va2);
    }
    cprintf("Test copy-on-write done!\n");

    cprintf("Test copy-on-read %d times, with size %d:\n", iters, segment_size_medium);
    for (int i = 0; i < iters; i++) {
        if ((i % 200 ) == 0)
            cprintf(" %d\n", i);
        void *va = 0;
        void *va2 = 0;
        uint64_t sg = 0;
        uint64_t sg2 = 0;
        
        r = alloc_copy_map(&sg, &va, &sg2, &va2, 
			   segment_size_medium, 
			   page_shared_cor);
        if (r < 0) {
            warn_once(r);
            continue;
        }
        
        switch ((i % 2)) {
        case 0:
            memset(va, 0xde, segment_size_medium);
            verify_mem(va, 0xde, segment_size_medium);        
            verify_mem(va2, 0xff, segment_size_medium);        
            break;
        case 1:
            memset(va2, 0xce, segment_size_medium);
            memset(va, 0xde, segment_size_medium);
            verify_mem(va, 0xde, segment_size_medium);        
            verify_mem(va2, 0xce, segment_size_medium);        
            break;
        default:
            panic("test case not handled");
            break;
        }
        unmap_unref(sg, va, sg2, va2);
    }
    cprintf("Test copy-on-read done!\n");

#undef warn_once
}

static void __attribute__((unused))
segment_size_test(void)
{
    enum { segcount = 8 };
    uint64_t one_gb = 1024 * 1024 * 1024;
    cprintf("Test alloc&write %u 1GB segments\n", segcount);
    for (int i = 0; i < segcount; i++) {
        cprintf(" %d\n", i);
        void *va = 0;
	int64_t sg = alloc_and_map(&va, one_gb);
	if (sg == -E_NO_MEM) {
	    if (i == 0) {
		cprintf("Insufficient memory for 1GB segments?"
			"  Skipping test..\n");
		return;
	    } else 
		panic("alloc_and_map error: %s", e2s(sg));
	}
	echeck(sg);
	memset(va, 0xda, one_gb);
	as_unmap(va);
	assert(sys_share_unref(SOBJ(core_env->sh, sg)) == 0);
    }

    cprintf("Test 1GB segments done!\n");
}

static void __attribute__((unused))
segment_n_test(void)
{
    enum { sg_count = 600 };
    enum { iters = 50 };
    int64_t sg[sg_count];

    cprintf("Test allocate&free %u segments, %u times\n", sg_count, iters);
    
    for (int k = 0; k < iters; k++) {
	if ((k % 10) == 0)
	    cprintf(" %d\n", k);
	for (int i = 0; i < sg_count; i++) {
	    sg[i] = sys_segment_alloc(core_env->sh, 4096, "foo", core_env->pid);
	    if (sg[i] < 0)
		panic("failed to allocate %dth segment: %s", i, e2s(sg[i]));
	}
	
	for (int i = 0; i < sg_count; i++)
	    assert(sys_share_unref(SOBJ(core_env->sh, sg[i])) == 0);
    }

    cprintf("Test allocate&free done!\n");
}

static volatile int signal JSHARED_ATTR;

static void
do_simple(void)
{
    enum { seg_size =  128 * 1024 * 1024 };
    enum { core_count = 2 };
    
    void *va[core_count];
    struct sobj_ref sg[core_count];
    memset(va, 0, sizeof(va));
    memset(sg, 0, sizeof(sg));

    for (int i = 0; i < core_count; i++)
	assert(segment_alloc(core_env->sh, seg_size, &sg[i], &va[i], 0, "test", i) == 0);

    for (int i = 0; i < 2; i++) {
	uint64_t s = arch_read_tsc();
	memset(va[i], 1, seg_size);
	uint64_t e = arch_read_tsc();
	cprintf(" %d took %ld cycles\n", i, e - s);
    }

    for (int i = 0; i < core_count; i++) {
	as_unmap(va[i]);
	assert(sys_share_unref(sg[i]) == 0);
    }
}

static void __attribute__((unused, noreturn))
segment_simple(void)
{
    cprintf("0:\n");
    do_simple();

    for (uint32_t i = 1; i < 16; i++) {
	signal = 0;
	int64_t r = pfork(i);
	if (r == 0) {
	    cprintf("%d:\n", i);
	    do_simple();
	    signal = 1;
	    processor_halt();
	} else {
	    while (signal == 0)
		arch_pause();
	}
    }
}

void
segment_test(void)
{
    //segment_simple();
    segment_n_test();
    segment_copy_test();
    segment_size_test();
}
