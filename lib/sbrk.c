#include <machine/memlayout.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/assert.h>

#include <bits/unimpl.h>
#include <unistd.h>
#include <string.h>

enum { no_shrink = 1 };

static struct {
    thread_mutex_t mu;
    struct sobj_ref seg;
    size_t brk;
    size_t real_brk;
} *heap;

static void *heap_base = (void *) UHEAP;
static uint64_t heap_maxbytes = UHEAPTOP - UHEAP;

libc_hidden_proto(sbrk)

void *
sbrk(intptr_t x)
{
    void *p = (void *)-1;
    uint32_t extra_flags = SEGMAP_HEAP;

    int r;
    if (!heap) {
	r = segment_alloc(core_env->sh, sizeof(*heap), 0, (void **)&heap, 
			  extra_flags, "meta-heap", core_env->pid);	
	if (r < 0) {
	    cprintf("sbrk: segment_alloc failed: %s\n", e2s(r));
	    return p;
	}
	memset(heap, 0, sizeof(*heap));
    }
    
    thread_mutex_lock(&heap->mu);

    intptr_t needed = x;
    if (no_shrink && x < 0) {
	needed -= x;
    } else if (heap->real_brk != heap->brk) {
	intptr_t avail = heap->real_brk - heap->brk;
	intptr_t use = JMIN(avail, x);
	needed -= use;
    }
   
    if (!needed) {
	// Already have all the space
	p = heap_base + heap->brk;
	heap->brk += x;
	thread_mutex_unlock(&heap->mu);
	return p;
    }

    struct u_address_mapping uam;
    r = as_lookup(heap_base, &uam);
    if (r < 0) {
	cprintf("sbrk: segment_lookup failed: %s\n", e2s(r));
	return p;
    }

    if (r) {
	heap->seg = uam.object;
    } else {
	r = segment_alloc(core_env->sh, 0, &heap->seg, 0, 0, "heap", core_env->pid);
	if (r < 0) {
	    cprintf("sbrk: cannot allocate heap: %s\n", e2s(r));
	    goto done;
	}
	r = as_map(heap->seg, 0, SEGMAP_READ | SEGMAP_WRITE | extra_flags,
		   &heap_base, &heap_maxbytes);

	if (r < 0) {
	    sys_share_unref(heap->seg);
	    cprintf("sbrk: cannot map heap: %s\n", e2s(r));
	    goto done;
	}
    }

    size_t nbrk = heap->real_brk + needed;
    if (nbrk > heap_maxbytes) {
	cprintf("sbrk: heap too large: %zu > %lu\n", nbrk, heap_maxbytes);
	goto done;
    }

    r = sys_segment_set_nbytes(heap->seg, nbrk);
    if (r < 0) {
	cprintf("sbrk: resizing heap to %zu: %s\n", nbrk, e2s(r));
	goto done;
    }

    p = heap_base + heap->brk;
    heap->real_brk = nbrk;
    heap->brk += x;
    
 done:
    thread_mutex_unlock(&heap->mu);
    return p;
}

libc_hidden_def(sbrk)
