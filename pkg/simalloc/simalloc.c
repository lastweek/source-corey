#include "simalloc.h"
#include <stdio.h>
#ifdef JOS_USER
#include <inc/lib.h>
#include <inc/queue.h>
#include <inc/assert.h>
#else
#include "ummap.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "queue.h"
#endif

static size_t sizes[] = 
{16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024};

enum { debug = 0 };

#define ALIGN 8

struct blk_hdr {
    size_t s;
    LIST_ENTRY(blk_hdr) next;
};

#define NLISTS (sizeof(sizes) / sizeof(sizes[0]) + 1)
#define BIG_BLK_INDEX (NLISTS - 1)
#define TO_DATA_PTR(b) (((char *)b) + sizeof(struct blk_hdr))
#define TO_BLK_HDR(p) (struct blk_hdr *)(((char *)p) - sizeof(struct blk_hdr))

LIST_HEAD(free_list, blk_hdr);

#ifdef JOS_USER

static struct free_list lists[JOS_NCPU][NLISTS];
static int inited[JOS_NCPU];
#define L_LISTS  lists[core_env->pid]
#define L_INITED inited[core_env->pid]

#else

static __thread struct free_list lists[NLISTS];
static __thread int inited = 0;
#define L_LISTS  lists
#define L_INITED inited

#endif

static __inline int
get_order(size_t size)
{
    for (uint32_t i = 0; i < NLISTS - 1; i++)
	if (size <= sizes[i])
	    return i;
    return BIG_BLK_INDEX;
}

void __attribute__((constructor))
memory_init(void)
{
    ummap_alloc_init();
    printf("simple allocator memory_init, %ld\n", sizeof(struct blk_hdr));
}

static void
local_lists_init(void)
{
    if (L_INITED)
        return;
    uint32_t i;
    for (i = 0; i < NLISTS; i++) {
        LIST_INIT(&L_LISTS[i]);
    }
    L_INITED = 1;
}

void *
malloc(size_t req_size)
{
    local_lists_init();
    if (!req_size)
        return NULL;
    if (debug)
        printf("allocating 0x%lx\n", req_size);
    size_t new_size = req_size + sizeof(struct blk_hdr);
    if (new_size % ALIGN)
        new_size = (1 + new_size / ALIGN) * ALIGN;
    int order = get_order(new_size);
    struct blk_hdr *blk;
    if (!LIST_EMPTY(&L_LISTS[order])) {
        if (order == BIG_BLK_INDEX) {
	    LIST_FOREACH(blk, &L_LISTS[order], next) {
	        if (blk->s >= new_size) {
		    LIST_REMOVE(blk, next);
		    return TO_DATA_PTR(blk);
	        }
	    }
        }
	else {
	    blk = LIST_FIRST(&L_LISTS[order]);
	    LIST_REMOVE(blk, next);
	    return TO_DATA_PTR(blk);
	}
    }
    if (order != BIG_BLK_INDEX) {
	 new_size = sizes[order];
    }
    blk = (struct blk_hdr *) u_mmap(NULL, new_size);
    blk->s = new_size;
    assert(new_size);
    if (debug)
        printf("allocated %p, new_size 0x%lx\n", blk, new_size);
    return TO_DATA_PTR(blk);
}

void
free(void* object)
{
    local_lists_init();
    if (!object)
        return;
    if (debug)
        printf("freeing 0x%p\n", object);
    struct blk_hdr *blk = TO_BLK_HDR(object);
    int order = get_order(blk->s);
    blk->next.le_next = NULL;
    blk->next.le_prev = NULL;
    LIST_INSERT_HEAD(&L_LISTS[order], blk, next);
}

void *
calloc(size_t nmemb, size_t size)
{
    local_lists_init();
    if (debug)
        printf("callocating 0x%lx\n", nmemb * size);
    if (!(nmemb * size))
	return 0;
    void *p = malloc(nmemb * size);
    memset(p, 0, nmemb * size);
    return p;
}

void *
realloc(void *object, size_t req_size)
{
    local_lists_init();
    if (!object)
        printf("core %d realloc %p to 0x%lx\n", core_env->pid, object, req_size);
    struct blk_hdr *blk = TO_BLK_HDR(object);
       size_t new_size = req_size + sizeof(struct blk_hdr);
    if (new_size % ALIGN)
        new_size = (1 + new_size / ALIGN) * ALIGN;
    if (new_size <= blk->s)
        return object;
    void *newp = malloc(req_size);
    memcpy(newp, object, blk->s - sizeof(struct blk_hdr));
    free(object);
    return newp;
}

#ifndef JOS_USER
#define powerof2(x) ((((x)-1)&(x))==0)

/* We need a wrapper function for one of the additions of POSIX.  */
int
__libc_memalign (void **memptr, size_t alignment, size_t size)
{
    void *mem;

    /* Test whether the SIZE argument is valid.  It must be a power of
     *      two multiple of sizeof (void *).  */
    if (alignment % sizeof (void *) != 0
            || !powerof2 (alignment / sizeof (void *)) != 0
            || alignment == 0)
        return EINVAL;

    mem = u_mmap_align(alignment, size);

    if (mem != NULL) {
        *memptr = mem;
        return 0;
    }

    return ENOMEM;
}
#endif

