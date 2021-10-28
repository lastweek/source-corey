#ifndef BTREE_LEAF_H
#define BTREE_LEAF_H
#include "mr-def.h"

typedef struct {
   void (*leaf_setutil)(key_cmp_t fn);
   void *(*leaf_create)(void *key, void *val);
   int (*leaf_find)(void *leaf, void *key, void **val);
   // if the leaf is splitted, returns the splitted sibling, and store
   // the key seperates the two leafs in splitkey. Otherwise, return NULL
   void *(*leaf_insert)(void *leaf, void *key, void *val, int pos, void **splitkey);
   keyval_arr_t *(*leaf_getresults)(void *leaf);
   void (*leaf_traverse)(void *leaf, void *arg, tv_callback_t cb);
   void (*leaf_print)(void *leaf);
} leaf_type;

extern const leaf_type leaf_array;
extern const leaf_type leaf_hash;

#endif
