#ifndef BTREE_H
#define BTREE_H
#include "mr-def.h"

void btree_setutil(void *maxkey);
size_t btree_getsize(void);	//return the size of a btree
void *btree_find(void *btree, void *key);
void btree_insert(void *btree, void *key, void *val);
void btree_init(void *btree, key_cmp_t fn);
void btree_print(void *btree);

void btree_traverse(void *btree, void *arg, tv_callback_t cb);
void **btree_getresults(void *btree, int *len, void **token);

void btree_delete(void *btree);

#endif
