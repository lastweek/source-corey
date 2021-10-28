#ifndef TAGMM_H
#define TAGMM_H
#include <malloc.h>

void tmalloc_init();
void *tmalloc(size_t s, int tag);
int tfree(int tag);

#endif
