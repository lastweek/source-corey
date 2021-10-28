#ifndef _KEYSET_H
#define _KEYSET_H

#include "mr-def.h"

void ks_setutil(key_cmp_t keycmp_fn, combine_t cb);
void *ks_create(void *key, void *val, int factor);
int ks_insert(void *node, int hash, void *key, void *val, int factor);
int ks_getlen(void *node);
void ks_getresults(void *node, keyvals_t **res);
keyvals_arr_t *ks_getinternal(void *node);
void ks_delete(void *node);
int ks_find(void *node, void *key);
void *ks_rand_select(void *node);

int ks_insert_kvs(void *node, keyvals_t *kvs);
void *ks_create_kvs(keyvals_t *kvs);

#endif
