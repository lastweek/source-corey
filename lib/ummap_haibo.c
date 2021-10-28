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

#include <string.h>
#include <stdio.h>

/* A not-so-crabby user-level mmap requests handling that cross processors.
 *  Design: three types of memory allocations:
 *           (1)  4k-4MB: buddy system based allocation.
 *           (2)   8MB:    fixed size list based allocation (to cooperate with streamflow)
             (3)   > 8MB: variable-lenght based allocation.

 *  Flags:
    UMMAP_USE_ADDRESS_TREE: use address tree to avoid 1-pagefault-per-core
    UMMAP_NUMA_ALLOCATOR:   turn on NUMA aware memory mapping.
 */

/* with or without using address tree */
#define UMMAP_USE_ADDRESS_TREE
#ifdef UMMAP_USE_ADDRESS_TREE
#define UMMAP_NUMA_ALLOCATOR
#endif

//#define MIN_BUDDY_SHIFT  12
//#define MIN_BUDDY_SIZE  (1 << MIN_BUDDY_SHIFT)
//#define MAX_BUDDY_SIZE  (MIN_BUDDY_SIZE * 1024)

/* fixed medium chunk size */
//#define MEDIUM_CHUNK_SIZE (8 * 1024 * 1024)

// First 8MB are for user mmap mappings
// Limit 32GB
#define UMMAP_BASE  UMMAPSTART
/*must be large enough to initialize the per-processor buddy heap and medium chunk size */
#define UMMAP_INIT_LEN	 (0x800000)
#ifndef UMMAP_NUMA_ALLOCATOR
#define UMMAP_UPPER_LEN (UINT64(0x800000000))  //32G
#else
#define UMMAP_UPPER_LEN (UINT64(0x80000000)) // 2G
#endif
#define UMMAP_END  UMMAP_BASE + UMMAP_UPPER_LEN

/* to avoid huge lock in segment growing. linearly increase the segment instead of growing by doubling the size */

#define DOUBLE_GROW_LIMIT  (PISIZE >> 3)

enum { ummap_debug = 0};
enum { sanity_remap = 0} ;

#define dprintf(fmt,args...)        \
do {                                            \
    if (ummap_debug) cprintf("ummap.cpu.%u:"fmt, core_env->pid, ##args );    \
}while (0)

#define ummap_assert(expr)  \
do {                                            \
    if (ummap_debug)   assert((expr));         \
}while (0)

#define MAX_BUDDY_ORDERS  11        /* related to MIN and MAX of buddy size */
#define BUDDY_BITMAP_SIZE   MAX_BUDDY_ORDERS

#define CHUNK_HDR_ALLOC_SIZE   PGSIZE

typedef struct chunk_hdr{
    void * start;
    size_t length;
#ifdef UMMAP_NUMA_ALLOCATOR
    proc_id_t pid;
#endif
    LIST_ENTRY(chunk_hdr) ch_link;
} chunk_hdr_t;

LIST_HEAD(free_list, chunk_hdr);

/*
 * processor local buddy heap
 * allocation size range from 4k-4MB.
 */

typedef struct {
    /* buddy allocation. */
    struct free_list	buddy[MAX_BUDDY_ORDERS];
} small_buddy_t;


typedef struct{
    void * next_free;
    void * untouched_addr;
    size_t nr_free_elem;
} chunk_hdr_mgr;

#ifdef UMMAP_NUMA_ALLOCATOR
typedef struct {
    struct sobj_ref ummap_sgref;
    uint64_t umap_bs_cur;
    uint64_t seg_alloc_len;
    struct free_list medium_chunks;
    struct free_list large_chunks;
} per_cpu_state_t;
#endif

/*TODO: more cache friendly */
typedef struct  {
    small_buddy_t buddies[JOS_NCPU];

    chunk_hdr_mgr chunk_hdrs[JOS_NCPU];

    struct sobj_ref ummap_shref;
#ifndef UMMAP_NUMA_ALLOCATOR
    struct sobj_ref ummap_sgref;
    uint64_t umap_bs_cur;
    uint64_t seg_alloc_len;
    struct spinlock  mu;
    struct free_list medium_chunks;
    struct free_list large_chunks;
    struct spinlock medium_mu;
    struct spinlock large_mu;
#else
    per_cpu_state_t per_cpu_state[JOS_NCPU];
#endif
} u_mmap_state_t;

static u_mmap_state_t * u_mmap_state;

static inline void * ummap_addr_alloc(uint64_t nbytes, proc_id_t pid);
//static inline void buddy_free (int cpu, void * addr, size_t len);
//static inline void medium_chunk_free(void * addr, proc_id_t pid);
//static  void dump_free_list(struct free_list *free_list) __attribute__((unused));

extern int valid_super_list(void);

struct sobj_ref*
            ummap_get_shref(void)
{
    return &u_mmap_state->ummap_shref;
}
/*
static inline void*
alloc_chunk_hdr(int cpu)
{
    chunk_hdr_t * ch_hdr = NULL;
    chunk_hdr_mgr * ch_mgr = &u_mmap_state->chunk_hdrs[cpu];

    if (ch_mgr->next_free != NULL) {
        ch_hdr = (chunk_hdr_t *)ch_mgr->next_free;
        ch_mgr->next_free = * ((void **) ch_hdr);
        ummap_assert(ch_mgr->nr_free_elem > 0);
        ch_mgr->nr_free_elem-- ;
    }
    else  {
        if (ch_mgr->nr_free_elem == 0 ) {
           ummap_assert( ((size_t)ch_mgr->untouched_addr & (CHUNK_HDR_ALLOC_SIZE - 1)) == 0);
           ch_mgr->untouched_addr = ummap_addr_alloc(CHUNK_HDR_ALLOC_SIZE, cpu);
           ch_mgr->nr_free_elem = CHUNK_HDR_ALLOC_SIZE / sizeof(chunk_hdr_t);
        }

        ch_hdr = ch_mgr->untouched_addr;
        ch_mgr->untouched_addr += sizeof(chunk_hdr_t);
        ch_mgr->nr_free_elem -- ;
    }
#ifdef UMMAP_NUMA_ALLOCATOR
    ch_hdr->pid = cpu;
#endif
    return ch_hdr;
}

static inline  void
free_chunk_hdr(void * hdr){
    chunk_hdr_mgr * ch_mgr = &u_mmap_state->chunk_hdrs[core_env->pid];
    void * next_free = ch_mgr->next_free;
    ch_mgr->next_free = hdr ;
    *((void **)hdr) = next_free;
    ch_mgr->nr_free_elem ++;
}

static inline  void*
large_chunk_alloc(size_t len, proc_id_t pid){
    size_t remainder = 0;
    chunk_hdr_t * hdr;
    void * res = NULL;
    struct free_list * lcl;

#ifdef UMMAP_NUMA_ALLOCATOR
    lcl = &u_mmap_state->per_cpu_state[pid].large_chunks ;
#else
    spin_lock(&u_mmap_state->large_mu);
    lcl = &u_mmap_state->large_chunks;
#endif
    LIST_FOREACH(hdr, lcl, ch_link){

        if( hdr->length >= len
#ifdef UMMAP_NUMA_ALLOCATOR
         &&  hdr->pid == core_env->pid
#endif
       ) {
            remainder = hdr->length - len;
            res = hdr->start ;

            //do not need to unlink the list. just update it.
            if (remainder > MEDIUM_CHUNK_SIZE) {
                hdr->start = res + len;
                hdr->length = remainder;
            }
            else {
                LIST_REMOVE(hdr,ch_link);
                free_chunk_hdr(hdr);
            }
            break;
        }
    }
#ifndef UMMAP_NUMA_ALLOCATOR
    spin_unlock(&u_mmap_state->large_mu);
#endif
    if (!res)
        return ummap_addr_alloc(len,pid);
    // find it. But we still need to do some clean work
    if (remainder >= MIN_BUDDY_SIZE && remainder < MEDIUM_CHUNK_SIZE)
        buddy_free(pid, res+len, remainder);
    // remainder == MEDIUM_CHUNK_SIZE
    else if(remainder == MEDIUM_CHUNK_SIZE)
        medium_chunk_free(res+len, pid);

    dprintf("large_chunk_alloc addr %p len %lx \n", res, len );
    return res;

}

// dump list ,dump large chunk list
static  void
dump_free_list(struct free_list *free_list){
    size_t nr_elems = 0;
    chunk_hdr_t* hdr ;

    LIST_FOREACH(hdr, free_list, ch_link){
        cprintf("%ld %p (%lx) ", nr_elems, hdr->start, hdr->length);
        nr_elems ++ ;
        if (nr_elems % 4 == 0)
            cprintf("\n");
        cprintf("\n");
    }
}

static inline void
large_chunk_free(void * addr, size_t len, proc_id_t pid){
    chunk_hdr_t*  hdr  = alloc_chunk_hdr(pid);
    hdr->start = addr;
    hdr->length = len;

#ifdef UMMAP_NUMA_ALLOCATOR
    LIST_INSERT_HEAD(&u_mmap_state->per_cpu_state[pid].large_chunks, hdr, ch_link);
#else
    spin_lock(&u_mmap_state->large_mu);
    //list_add(&u_mmap_state->large_chunks, hdr, ch_link);
    LIST_INSERT_HEAD(&u_mmap_state->large_chunks, hdr, ch_link);
    spin_unlock(&u_mmap_state->large_mu);
#endif
    dprintf("large_chunk_free addr %p len %lx\n",  addr, len );
}


// fixed size allocator.

static inline  void
add_medium_chunk(void * addr, proc_id_t pid){
    chunk_hdr_t* mc = alloc_chunk_hdr(pid);
    mc->start = addr;
    mc->length = MEDIUM_CHUNK_SIZE ;
    // TODO: make it atomic
#ifdef UMMAP_NUMA_ALLOCATOR
    LIST_INSERT_HEAD(&u_mmap_state->per_cpu_state[pid].medium_chunks, mc, ch_link);
#else
    spin_lock(&u_mmap_state->medium_mu);
    LIST_INSERT_HEAD(&u_mmap_state->medium_chunks, mc, ch_link);
    spin_unlock(&u_mmap_state->medium_mu);
#endif

}

static inline void*
medium_chunk_alloc(proc_id_t pid){
    struct free_list* mc_list;
    void* res = NULL;

#ifdef UMMAP_NUMA_ALLOCATOR
    mc_list = &u_mmap_state->per_cpu_state[pid].medium_chunks ;
#else
    spin_lock(&u_mmap_state->medium_mu);
    mc_list = &u_mmap_state->medium_chunks;
#endif
    if (!LIST_EMPTY(mc_list)) {
        chunk_hdr_t* ch = LIST_FIRST(mc_list);
        LIST_REMOVE(ch, ch_link);
        res = ch->start;
        ummap_assert(ch->length == MEDIUM_CHUNK_SIZE);
        free_chunk_hdr(ch);
    }
#ifndef UMMAP_NUMA_ALLOCATOR
    spin_unlock(&u_mmap_state->medium_mu);
#endif
    dprintf("medium_chunk_alloc addr %p \n", res );
    if (res) {
        return res;
    }
    // not found. allocate from the heap.
    return large_chunk_alloc(MEDIUM_CHUNK_SIZE, pid);
}

static inline void
medium_chunk_free(void * addr, proc_id_t pid){
    dprintf("medium_chunk_free addr %p (cpu %d) \n", addr, pid );
    add_medium_chunk(addr, pid);
}

// crabby buddy allocator.  Never coalence.


static inline void
add_buddy_chunk(int cpu, void * addr, int order) {
    chunk_hdr_t* bc = alloc_chunk_hdr(cpu);
    small_buddy_t* buddy = &u_mmap_state->buddies[cpu];

    bc->start = addr;
    bc->length = (1 << (order + MIN_BUDDY_SHIFT)) ;
    LIST_INSERT_HEAD(&(buddy->buddy[order]), bc, ch_link);
}

// to do
static inline int
get_buddy_order(size_t len){
     if (!len)
        return 0;
     __asm__("bsrq %1,%0"
		:"=r" (len)
		:"rm" (len));
    ummap_assert(len >= MIN_BUDDY_SHIFT);
    return (int)(len - MIN_BUDDY_SHIFT);
}

static inline void
buddy_free (int cpu, void * addr, size_t len){
    int order ;
    int round_len;
    size_t remainder;

    order = get_buddy_order(len);
    round_len = ROUNDDOWN(len, (1 << (order + MIN_BUDDY_SHIFT)));

    //dprintf("buddy_free cpu.%d addr %p len %lx order %x round_len %x\n", cpu,addr, len,order, round_len);
    ummap_assert(order < MAX_BUDDY_ORDERS);

    add_buddy_chunk(cpu, addr, order);
    addr += 1 << (order+ MIN_BUDDY_SHIFT);

    remainder =  len - round_len ;
    if (remainder >= MIN_BUDDY_SIZE)
        buddy_free(cpu, addr, remainder);
}

// len must be in the right order
static inline void*
buddy_alloc (int cpu, size_t len){
    void * ret_val = NULL;
    uint64_t i;
    small_buddy_t* buddy = &u_mmap_state->buddies[cpu];
    int order = get_buddy_order(len);

    //dprintf("buddy_alloc len %lx order %x \n", len, order );

    // See if we can find one from the buddy.
    for(i  = order ; i < MAX_BUDDY_ORDERS; i ++){
        struct free_list * sbl = &(buddy->buddy[i]);
        if (!LIST_EMPTY(sbl)){
            size_t blen;
            size_t remainder ;
            chunk_hdr_t* ch =  LIST_FIRST(sbl);

            LIST_REMOVE(ch, ch_link);
            ret_val = ch->start;
            blen = ch->length;
            ummap_assert(blen == (size_t)(1 <<  (i + MIN_BUDDY_SHIFT)));
            remainder = blen - len;
            free_chunk_hdr(ch);
            if (remainder >= MIN_BUDDY_SIZE){
                buddy_free(cpu, ret_val + len, remainder);
            }

            return ret_val;
        }
    }
    // if there is not enough free chunks in buddy. allocate it.
    ret_val = medium_chunk_alloc(cpu);
    buddy_free(cpu, ret_val+len, MEDIUM_CHUNK_SIZE - len);

    return ret_val;
}

static void
init_ummap_heap(void) {

    // buddy allocation.
    struct u_locality_matrix ulm;
    echeck(sys_locality_get(&ulm));

#ifndef UMMAP_NUMA_ALLOCATOR
    // large chunks: initially empty
    LIST_INIT(&u_mmap_state->medium_chunks);
    spin_init(&u_mmap_state->medium_mu);
    // medium chunks:  initially empty
    LIST_INIT(&u_mmap_state->large_chunks);
    spin_init(&u_mmap_state->large_mu);
#endif
    // init the buddy free list
   for (uint64_t i = 0 ; i < ulm.ncpu; i ++  ) {
#ifdef UMMAP_NUMA_ALLOCATOR
        LIST_INIT(&u_mmap_state->per_cpu_state[i].large_chunks);
        LIST_INIT(&u_mmap_state->per_cpu_state[i].medium_chunks);
#endif
        small_buddy_t* buddy = &u_mmap_state->buddies[i];
        for (uint64_t j = 0; j < MAX_BUDDY_ORDERS ; j ++)
            LIST_INIT(&(buddy->buddy[j]));
   }
}
*/

int
ummap_alloc_init(void){
    dprintf("initializing the ummap segment for mmap\n");
    // allocat the ummap shared state
    echeck(segment_alloc(core_env->sh, sizeof(*u_mmap_state), 0, (void **)&u_mmap_state,
                         SEGMAP_SHARED, "UMMAP_SHARE_STATE",core_env->pid));
    memset(u_mmap_state, 0 , sizeof(u_mmap_state_t));
    // allocate the mmap segment

    int64_t shid ;
    echeck(shid = sys_share_alloc(core_env->sh, 1 << kobj_segment,
                                  "ummap-share", core_env->pid));

    u_mmap_state->ummap_shref = SOBJ(core_env->sh, shid);

#ifdef UMMAP_USE_ADDRESS_TREE

#ifndef UMMAP_NUMA_ALLOCATOR
    int64_t sgid;

    echeck(sgid = sys_segment_alloc(shid, UMMAP_INIT_LEN, "ummap-seg", core_env->pid));
    struct sobj_ref sgref = SOBJ(shid, sgid);
    u_mmap_state->ummap_sgref = sgref ;

    // create the address tree for ummap
    for (uint64_t i = 0;  i <  UMMAP_UPPER_LEN / PISIZE ; i ++) {
        int64_t atid;
        echeck(atid = sys_at_alloc(core_env->sh, 1, "at-ummap", core_env->pid));
        struct sobj_ref atref = SOBJ(core_env->sh, atid);

        void *va = (void *) (UMMAP_BASE + i * PISIZE);
        echeck(at_map_interior(atref, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP, va ));
        dprintf("adding mapping to interior...\n");
        struct u_address_mapping mapping ;
        memset(&mapping, 0, sizeof(mapping));

        mapping.type = address_mapping_segment;
        mapping.object = sgref;
        mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
        mapping.kslot = 0;
        mapping.va = 0;
        mapping.start_page = i * PISIZE / PGSIZE;
        mapping.num_pages = PISIZE / PGSIZE;
        echeck(sys_at_set_slot(atref, &mapping));
    }

    u_mmap_state->seg_alloc_len = UMMAP_INIT_LEN;
    u_mmap_state->umap_bs_cur = UMMAP_BASE;
    spin_init(&u_mmap_state->mu);

#else
    // buddy allocation
    struct u_locality_matrix ulm;
    echeck(sys_locality_get(&ulm));
//#define HACK_PF
#ifdef HACK_PF
    uint64_t sum = 0;
#endif
    for (uint64_t i = 0 ; i < ulm.ncpu; i ++) {
        char seg_name[20];
        per_cpu_state_t* pstate  = &u_mmap_state->per_cpu_state[i];
        pstate->seg_alloc_len = UMMAP_INIT_LEN;
        pstate->umap_bs_cur =  UMMAP_BASE +  i * UMMAP_UPPER_LEN;

        // alllocate the segment
        int64_t sgid;
        sprintf(seg_name, "ummap-seg%d", i);
        echeck(sgid = sys_segment_alloc(shid, UMMAP_INIT_LEN, seg_name, i));
        struct sobj_ref sgref = SOBJ(shid, sgid);
        pstate->ummap_sgref = sgref ;

        for (uint64_t j = 0;  j <  UMMAP_UPPER_LEN / PISIZE ; j ++) {
            int64_t atid;
            echeck(atid = sys_at_alloc(core_env->sh, 1, "at-ummap", core_env->pid));
            struct sobj_ref atref = SOBJ(core_env->sh, atid);

            void *va = (void *) (UMMAP_BASE + i * UMMAP_UPPER_LEN + j * PISIZE);
            echeck(at_map_interior(atref, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP, va ));
            dprintf("adding mapping to interior... %ld %ld \n", i, j);
            struct u_address_mapping mapping ;
            memset(&mapping, 0, sizeof(mapping));

            mapping.type = address_mapping_segment;
            mapping.object = sgref;
            mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
            mapping.kslot = 0;
            mapping.va = 0 ;
            mapping.start_page = j * PISIZE / PGSIZE;
            mapping.num_pages = PISIZE / PGSIZE;
            echeck(sys_at_set_slot(atref, &mapping));
        }
#ifdef HACK_PF
        char *p = (char *)(UMMAP_BASE + i * UMMAP_UPPER_LEN);
        for (uint64_t k = 0; k < UMMAP_INIT_LEN ; k += 4096) {
            sum += (uint32_t)p[k];
        }
#endif

    }
#ifdef HACK_PF
    printf("ignore %ld\n", sum);
#endif
#endif

#else

    int64_t sgid;
    echeck(sgid = sys_segment_alloc(shid, UMMAP_INIT_LEN, "ummap-seg", core_env->pid));
    struct sobj_ref sgref = SOBJ(shid, sgid);
    u_mmap_state->ummap_sgref = sgref;

    void *ummap_base = (void *) UMMAP_BASE;
    uint64_t ummap_total_len = UMMAP_UPPER_LEN / 10;
    as_map(sgref, 0, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP,
           (void **)&ummap_base, &ummap_total_len);

    u_mmap_state->seg_alloc_len = UMMAP_INIT_LEN;
    u_mmap_state->umap_bs_cur = UMMAP_BASE;
    spin_init(&u_mmap_state->mu);

#endif

    // init_ummap_heap();
    return 1;
}

int
ummap_finit(void){
    int r;
    dprintf("finalizing the ummap segment\n");
    r = sys_share_unref(u_mmap_state->ummap_shref);
    if (r < 0)
        cprintf("ummap_finit: sys_processor_unref failed: %s\n", e2s(r));
    return 0;
}

static void *
ummap_addr_alloc(uint64_t nbytes, proc_id_t pid)
{
    void * res;
    int r = 0;
    size_t bs_cur, seg_len, upper_bound, req_len;
    struct sobj_ref objref;

#ifdef UMMAP_NUMA_ALLOCATOR
    bs_cur = u_mmap_state->per_cpu_state[pid].umap_bs_cur ;
    seg_len = u_mmap_state->per_cpu_state[pid].seg_alloc_len;
    objref = u_mmap_state->per_cpu_state[pid].ummap_sgref ;
    upper_bound = UMMAP_END + pid * UMMAP_UPPER_LEN;
    req_len = (bs_cur - UMMAP_BASE  - pid * UMMAP_UPPER_LEN) + nbytes;
#else
    spin_lock(&u_mmap_state->mu);
    bs_cur = u_mmap_state->umap_bs_cur;
    seg_len = u_mmap_state->seg_alloc_len;
    objref = u_mmap_state->ummap_sgref;
    upper_bound = UMMAP_END;
    req_len = bs_cur + nbytes - UMMAP_BASE ;
#endif
    if (bs_cur + nbytes > upper_bound) {
        print_stacktrace();
        panic("ummap: req len 0x%lx no space\n", nbytes);
    }
    /*see if we need to grow the mmap segments. */
    if (req_len > seg_len) {
        while (seg_len <= req_len)
            if (seg_len < DOUBLE_GROW_LIMIT)
                seg_len *= 2;
            else
                seg_len += DOUBLE_GROW_LIMIT;
        dprintf("cpu.%d ummap_addr_alloc: resizing segment to 0x%lx\n", pid, seg_len);
        r = sys_segment_set_nbytes(objref, seg_len);
        if (r < 0)
            panic("ummap_addr_alloc: resizing segment to 0x%lx: %s failed\n", seg_len, e2s(r));
    }

    res = (void *)bs_cur;
    dprintf("cpu.%d ummap_addr_alloc: cur %lx\n", pid, bs_cur);
#ifdef UMMAP_NUMA_ALLOCATOR
    u_mmap_state->per_cpu_state[pid].umap_bs_cur += nbytes;
    u_mmap_state->per_cpu_state[pid].seg_alloc_len = seg_len;
#else
    u_mmap_state->umap_bs_cur += nbytes;
    u_mmap_state->seg_alloc_len = seg_len;
    spin_unlock(&u_mmap_state->mu);
#endif

    return res;
}

void *
u_mmap(void *addr, size_t len, ...)
{
    assert(addr == 0);
    len = ROUNDUP(len, PGSIZE);
    return ummap_addr_alloc(len, core_env->pid);
    /*
    void * va ;
    if (len < MEDIUM_CHUNK_SIZE)
        va =  buddy_alloc(core_env->pid, len);
    else if (len == MEDIUM_CHUNK_SIZE)
        va = medium_chunk_alloc(core_env->pid);
    else
        va =  large_chunk_alloc(len,core_env->pid);

    dprintf("u_mmap: va %p len 0x%lx \n", va, len);
    return va;*/
}

int
u_munmap(void *addr, size_t  len,...)
{
    return 1;
    /*
    if (len < MEDIUM_CHUNK_SIZE) {
        ummap_assert(len >= MIN_BUDDY_SIZE);
        buddy_free(core_env->pid, addr, len);
    }
    else if (len == MEDIUM_CHUNK_SIZE){
        medium_chunk_free(addr, core_env->pid);
    }
    else {
        large_chunk_free(addr,len, core_env->pid);
    }
    dprintf("u_munmap: va %p len %lx\n", addr, len);

    return 1;*/
}

/* crappy:  just allocate a new address and copy it.
 *  Fix.
 */
void *
u_mremap (void *addr, size_t old_len, size_t new_len,
          int flags,...)
{
    /*void * va = 0;
    ummap_assert(addr != 0);

    // shrinking?
    if( old_len >= new_len) {
        size_t remainder = old_len -  new_len;
        if (remainder  >= MIN_BUDDY_SIZE)
            u_munmap(addr + new_len, remainder);
        va = addr;
    }
    else {
        va = u_mmap(0, new_len);
        // copy it.
        memcpy(va, addr, old_len);
        if (sanity_remap) {
            size_t * old  = (size_t *)addr;
            size_t * new = (size_t *) va;
            for (size_t i = 0 ; i < old_len / sizeof(size_t); i ++)
                if (old[i] != new[i]) {
                    cprintf("cpu.%d %lx != %lx \n",core_env->pid, old[i], new[i]);
                    print_stacktrace();
                }
        }
        // unmap it.
        u_munmap(addr,old_len);
    }
    dprintf("u_mremap: %p (len: %lx) --> %p (len: %lx)\n", addr, old_len, va, new_len);

    return va;    */
    return NULL;
}


