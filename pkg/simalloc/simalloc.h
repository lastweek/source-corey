#ifndef SIMALLOC_H
#define SIMALLOC_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

void* malloc(size_t requested_size);
void free(void* object);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

#endif
