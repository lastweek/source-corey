#include "simalloc.h"
#include <stdio.h>
#ifdef JOS_USER
#include <inc/lib.h>
#include <inc/queue.h>
#include <inc/pad.h>
#include <inc/array.h>
#else
#include "ummap.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "queue.h"
#include "pad.h"
#endif

/*
 * Allocates chunks rounded up to nearest bucket size.  If req_size 
 * is greater than largest bucket_size malloc panics.  Memory is never
 * unmapped.
 */
#if 0
static const uint64_t bucket_sizes[] = { 
    16,
    64,
    4096,
    4 * 4096,
    16 * 4096,
    1024 * 1024,
    16 * 1024 * 1024,
    128 * 1024 * 1024,
    256 * 1024 * 1024,
    512 * 1024 * 1024,
    1024 * 1024 * 1024,
};
#endif

static const uint64_t bucket_sizes[] = { 
16, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024
};

#define NBUCKET ((sizeof(bucket_sizes) / sizeof(bucket_sizes[0])))

struct mhdr {
    uint64_t size;
    uint64_t bucket;
    LIST_ENTRY(mhdr) next;
};
LIST_HEAD(mhdr_list, mhdr);

typedef PAD(struct mhdr_list) mhdr_list_p;

#ifdef JOS_USER
static mhdr_list_p free_list[JOS_NCPU][NBUCKET];

static mhdr_list_p *
my_list(void)
{
    return free_list[core_env->pid];
}

#else
static __thread mhdr_list_p free_list[NBUCKET];

static mhdr_list_p *
my_list(void)
{
    return free_list;
}
#endif

void memory_init(void);

void __attribute__((constructor))
memory_init(void)
{
    ummap_alloc_init();
    printf("SBW simple allocator memory_init\n");
}

static uint64_t
bucket_index(size_t *req_size)
{
    for (uint64_t k = 0; k < NBUCKET; k++)
	if (*req_size <= bucket_sizes[k]) {
	    *req_size = bucket_sizes[k];
	    return k;
	}

    printf("Too big for bucket sizes %ld\n", *req_size);
    assert(0);
}

void * 
malloc(size_t req_size)
{
    if (!req_size)
	return NULL;

    uint64_t size = req_size + sizeof(struct mhdr);
    uint64_t k = bucket_index(&size);

    struct mhdr *h;

    mhdr_list_p *l = my_list();    
    h = LIST_FIRST(&l[k].v);
    if (!h) {
	void *va = u_mmap(NULL, size, 
			  PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 
			  -1, 0);
	if (va == (void *)-1)
	    return va;
	h = va;
	h->size = size;
	h->bucket = k;
    } else {
	LIST_REMOVE(h, next);
    }

    return h + 1;
}

void 
free(void *object)
{
    if (!object)
	return;

    struct mhdr *h = object;
    h = h - 1;

    mhdr_list_p *l = my_list();
    LIST_INSERT_HEAD(&l[h->bucket].v, h, next);
}

void *
calloc(size_t nmemb, size_t size)
{
    void *p = malloc(nmemb * size);
    memset(p, 0, nmemb * size);
    return p;
}

void *
realloc(void *ptr, size_t size)
{
    struct mhdr *h = ptr;
    h = h - 1;
    if (h->size > size + sizeof(*h))
        return ptr;
	
    void *newp = malloc(size);
    memcpy(newp, ptr, h->size - sizeof(*h));
    free(ptr);
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

