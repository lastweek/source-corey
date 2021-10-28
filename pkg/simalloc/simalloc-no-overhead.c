#include "simalloc.h"
#include <stdio.h>
#ifdef JOS_USER
#include <inc/lib.h>
#else
#include "ummap.h"
#endif

void __attribute__((constructor))
memory_init()
{
    ummap_alloc_init();
    printf("simple allocator memory_init\n");
}

void * 
malloc(size_t req_size)
{
    if (!req_size)
	return NULL;
    return u_mmap(NULL, req_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

void 
free(void* object)
{
    //MUNMAP(object);
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
    // TODO check whether size is becoming smaller
    // Problematic. We do not know the size of ptr
    printf("bad realloc\n");
    /*void *newp = malloc(size);
    memcpy(newp, ptr, size);
    free(ptr);
    return newp;*/
}
