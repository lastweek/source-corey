#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <bits/unimpl.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/queue.h>
#include <inc/spinlock.h>
#include <inc/stacktrace.h>
#include <inc/compiler.h>
#include <string.h>
#include <stdio.h>
#include <inc/arch.h>

//#define UMMAP_SHARE_ADDRESS_TREE
// If the kernel does not shrink the segment, no lock will be hold on ummap during
// sys_set_segment_nbytes call. So make sure kern/kern/segment.c does not call
// segment_invalidate if you do not want define NO_SHRINK
//#define NO_SHRINK	
#define UMMAP_BASE  UMMAPSTART
#define UMMAP_INIT_LEN	 UINT64(0x100000000)   // 4 GB
#define UMMAP_UPPER_LEN  UINT64(0x800000000)  // 32 GB
#define UMMAP_END  UMMAP_BASE + UMMAP_UPPER_LEN

#define DOUBLE_GROW_LIMIT  (PISIZE >> 5)

typedef struct {
    struct sobj_ref segref;
    uint64_t base;
    uint64_t alloclen;
    volatile uint64_t cur;
#ifdef UMMAP_SHARE_ADDRESS_TREE
    struct spinlock mu;
#endif
} __attribute__((aligned(JOS_CLINE))) alloc_state_t;

typedef struct  {
    struct sobj_ref shref[JOS_NCPU];
    uint64_t nshref;
    // ummap only uses one astates if UMMAP_SHARE_ADDRESS_TREE
    alloc_state_t astates[JOS_NCPU];
} __attribute__((aligned(JOS_CLINE))) u_mmap_state_t;

static u_mmap_state_t JSHARED_ATTR u_mmap_state;

enum { profile_sc = 0 };

static JTLS uint64_t time_sc __attribute__((unused)) = 0;

static inline uint64_t
atmc_fetch_and_add(volatile uint64_t *address, uint64_t value)
{
     uint64_t prev = value;
     __asm__ __volatile__("lock; xaddq %1, %0"
		          : "+m" (*address)
		          : "r" (value)
			  : "memory");
     return prev + value;
}

void
ummap_get_shref(struct sobj_ref **sh, uint64_t *n)
{
    *sh = u_mmap_state.shref;
    *n = u_mmap_state.nshref;
}

static void
map_at_seg(uint64_t sh_id, struct sobj_ref seg_ref, uint64_t offset, void *va, 
	   proc_id_t pid)
{
    assert((offset % PISIZE) == 0);

    int64_t atid;

    echeck(atid = sys_at_alloc(sh_id, 1, "at-ummap", pid));
    struct sobj_ref atref = SOBJ(sh_id, atid);
    
    echeck(at_map_interior(atref, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP, va ));
    struct u_address_mapping mapping ;
    memset(&mapping, 0, sizeof(mapping));
    
    mapping.type = address_mapping_segment;
    mapping.object = seg_ref;
    mapping.flags = SEGMAP_READ | SEGMAP_WRITE;
    mapping.kslot = 0;
    mapping.va = 0 ;
    mapping.start_page = offset / PGSIZE;
    mapping.num_pages = PISIZE / PGSIZE;
    echeck(sys_at_set_slot(atref, &mapping));
}

void
ummap_alloc_init(void)
{
    memset(&u_mmap_state, 0 , sizeof(u_mmap_state_t));
    
#ifdef UMMAP_SHARE_ADDRESS_TREE
    uint64_t nnodes = 1;
    spin_init(&u_mmap_state.astates[0].mu);
#else
    struct u_locality_matrix ulm;
    echeck(sys_locality_get(&ulm));
    uint64_t nnodes = ulm.ncpu;
#endif

    for (uint64_t i = 0 ; i < nnodes; i ++) {
	int64_t shid ;
	echeck(shid = sys_share_alloc(core_env->sh, 
				      (1 << kobj_segment) | (1 << kobj_address_tree),
				      "ummap-share", i));
	u_mmap_state.shref[i] = SOBJ(core_env->sh, shid);

        alloc_state_t* astate  = &u_mmap_state.astates[i];
        astate->alloclen = UMMAP_INIT_LEN;
        astate->base =  UMMAP_BASE +  i * UMMAP_UPPER_LEN;
        astate->cur = astate->base;
        int64_t sgid;
        char seg_name[20];
        sprintf(seg_name, "ummap-seg%d", i);
        echeck(sgid = sys_segment_alloc(shid, UMMAP_INIT_LEN, seg_name, i));
        struct sobj_ref sgref = SOBJ(shid, sgid);
        astate->segref = sgref ;

        for (uint64_t j = 0;  j <  UMMAP_UPPER_LEN / PISIZE ; j++) {
	    void *va = (void *) (UMMAP_BASE + i * UMMAP_UPPER_LEN + j * PISIZE);
	    map_at_seg(shid, sgref, j * PISIZE, va, i);
	}
    }

    u_mmap_state.nshref = nnodes;
}

int __attribute__((noreturn))
ummap_finit(void) 
{
    panic("XXX");
#if 0
    int r;
    r = sys_share_unref(u_mmap_state.ummap_shref);
    if (r < 0)
        cprintf("ummap_finit: sys_processor_unref failed: %s\n", e2s(r));
    return 0;
#endif
}

static void *
ummap_addr_alloc(uint64_t nbytes, proc_id_t pid)
{
#ifdef UMMAP_SHARE_ADDRESS_TREE
    alloc_state_t *astate = &u_mmap_state.astates[0];
    uint64_t va = atmc_fetch_and_add(&astate->cur, nbytes) - nbytes;
    uint64_t alloclen = astate->alloclen;
    uint64_t req_len = va + nbytes - astate->base;

    if (req_len > UMMAP_UPPER_LEN) {
        print_stacktrace();
        panic("ummap: req len 0x%lx no space\n", nbytes);
    }

    if (req_len > alloclen) {
        while (alloclen <= req_len)
            if (alloclen < DOUBLE_GROW_LIMIT)
                alloclen *= 2;
            else
                alloclen += DOUBLE_GROW_LIMIT;
#ifdef NO_SHRINK
	if (alloclen > astate->alloclen) {
            int r = sys_segment_set_nbytes(astate->segref, alloclen);
            spin_lock(&astate->mu);
	    if (astate->alloclen < alloclen)
	        astate->alloclen = alloclen;
            spin_unlock(&astate->mu);
            //cprintf("resizing segment to 0x%lx\n", astate->alloclen);
            if (r < 0)
                panic("ummap_addr_alloc: fail to resize segment to 0x%lx: %s\n", alloclen, e2s(r));
	}
#else
	if (alloclen > astate->alloclen) {
            spin_lock(&astate->mu);
	    if (alloclen > astate->alloclen) {
                int r = sys_segment_set_nbytes(astate->segref, alloclen);
                //spin_lock(&astate->mu);
	        if (astate->alloclen < alloclen)
	            astate->alloclen = alloclen;
                //spin_unlock(&astate->mu);
                //cprintf("resizing segment to 0x%lx\n", astate->alloclen);
                if (r < 0)
                    panic("ummap_addr_alloc: fail to resize segment to 0x%lx: %s\n", alloclen, e2s(r));
	    }
	    spin_unlock(&astate->mu);
	}
#endif
    }
    return (void *)va;
#else
    alloc_state_t *astate = &u_mmap_state.astates[pid];
    uint64_t va = astate->cur;
    astate->cur += nbytes;
    uint64_t req_len = astate->cur - astate->base;
    if (req_len < astate->alloclen)
	return (void *)va;
    if (req_len > UMMAP_UPPER_LEN) {
        print_stacktrace();
        panic("ummap: req len 0x%lx no space\n", nbytes);
    }
    while (astate->alloclen <= req_len)
        if (astate->alloclen < DOUBLE_GROW_LIMIT)
            astate->alloclen *= 2;
        else
            astate->alloclen += DOUBLE_GROW_LIMIT;
    uint64_t s = 0;
    if (profile_sc) 
        s = read_tsc();
    int r = sys_segment_set_nbytes(astate->segref, astate->alloclen);
    if (profile_sc) 
        time_sc += read_tsc() - s;
    if (r < 0)
        panic("ummap_addr_alloc: resizing segment to 0x%lx: %s failed\n", astate->alloclen, e2s(r));

    return (void *)va;
#endif
}

void *
u_mmap(void *addr, size_t len, ...)
{
    assert(addr == 0);
#ifdef UMMAP_PAGEALIGN
    len = ROUNDUP(len, PGSIZE);
#else
    len = ROUNDUP(len, 8);
#endif

    return ummap_addr_alloc(len, core_env->pid);
}

int
u_munmap(void *addr, size_t  len,...)
{
    return 1;
}

void *
u_mremap (void *addr, size_t old_len, size_t new_len,
          int flags,...)
{
    void *va = addr;

    if( old_len < new_len) {
        va = u_mmap(0, new_len);
        memcpy(va, addr, old_len);
        u_munmap(addr,old_len);
    }
    return va;
}

void
ummap_init_usage(memusage_t *usage)
{
#ifndef UMMAP_SHARE_ADDRESS_TREE
    alloc_state_t *astate = &u_mmap_state.astates[core_env->pid];
    usage->last = astate->cur;
    if (profile_sc)
        time_sc = 0;
#endif
}

void 
ummap_print_usage(memusage_t *usage)
{
#ifndef UMMAP_SHARE_ADDRESS_TREE
    alloc_state_t *astate = &u_mmap_state.astates[core_env->pid];
    cprintf("cpu %d: Allocated: %ld MB, Segment length: %ld MB, Time alloc: %ld\n", 
	    core_env->pid, (astate->cur - usage->last) / 0x100000, 
	    (astate->alloclen) / 0x100000, time_sc * 1000 / (2000 * 1000 * 1000));
#endif
}

uint64_t
ummap_prefault(uint64_t segsize)
{
#ifndef UMMAP_SHARE_ADDRESS_TREE
    uint64_t s = read_tsc();
    alloc_state_t *astate = &u_mmap_state.astates[core_env->pid];
    uint64_t nbytes = segsize - astate->alloclen;
    cprintf("cpu %d started 0x%lx\n", core_env->pid, nbytes);
    char *p = (char *)ummap_addr_alloc(nbytes, core_env->pid);
    uint64_t sum = 0;
    uint64_t sys = read_tsc() - s;
    s = read_tsc();
    for (uint64_t i = 0; i < nbytes; i+=PGSIZE)
	sum += (uint64_t)p[i];
    astate->cur -= nbytes;
    uint64_t touch = read_tsc() - s;
    uint64_t freq = core_env->cpufreq;
    cprintf("cpu %d: %ld, %ld\n", core_env->pid, sys * 1000 / freq, touch * 1000 / freq);
    return sum;
#else
    return 0;
#endif
}
