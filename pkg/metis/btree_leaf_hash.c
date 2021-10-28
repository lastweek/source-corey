#include "btree_leaf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

static key_cmp_t JSHARED_ATTR keycmp = NULL;
typedef keyval_arr_t leaf_t;

enum { openhash = 0 };
enum { init_keyvals_len = 577 };
enum { order = 215 };

#define PROBING(i) (i)

static void 
leaf_setutil_(key_cmp_t fn)
{
    keycmp = fn;
}

static leaf_t *
leaf_create_withlen(int len)
{
    leaf_t *leaf = (leaf_t *)malloc(sizeof(leaf_t));
    leaf->alloc_len = len;
    if (len) {
        leaf->arr = (keyval_t *)malloc(sizeof(keyval_t) * leaf->alloc_len);
	memset(leaf->arr, 0 ,sizeof(keyval_t) * leaf->alloc_len);
    }
    else
	leaf->arr = NULL;
    leaf->len = 0;
    return leaf;
}

static void
leaf_traverse_(void *leaf_, void *arg, tv_callback_t cb)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    int len = 0;
    uint32_t i;
    
    for (i = 0; i < leaf->alloc_len; i++)
        if (leaf->arr[i].key || leaf->arr[i].val) {
            cb(leaf->arr[i].val, arg);
	    len ++;
	}
}

static int
key_hash_search(leaf_t *leaf, int len, keyval_t *keys, void *key, int *bfound)
{
    int slot = ((uint64_t)key) % leaf->alloc_len;
    if (bfound)
        *bfound = 0;
    if (openhash) {
        if (leaf->arr[slot].key || leaf->arr[slot].val) {
	    if (bfound)
	        *bfound = 1;
	}
        return slot;
    }
    uint32_t iter = 0;
    while (leaf->arr[slot].key || leaf->arr[slot].val) {
	iter++;
        if (keycmp(leaf->arr[slot].key, key))
	    slot = (slot + PROBING(iter)) % leaf->alloc_len;
	else {
	    if (bfound)
	        *bfound = 1;
	    return slot;
	}
	if (iter > leaf->alloc_len) {
	    printf("bad probe 2\n");
	    exit(0);
	}
    }
    return slot;
}

static int
leaf_find_(void *leaf_, void *key, void **val)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    int bfound = 0;
    int pos = key_hash_search(leaf, leaf->alloc_len, leaf->arr, key, &bfound);
    if (bfound)
        *val = leaf->arr[pos].val;
    else
        *val = 0;
    return pos;
}

static void
findk(keyval_t *keys, int len, int k)
{
    int s = 0;
    int t = len - 1;
    while (1) {
        int orgs = s;
        int orgt = t;
        keyval_t key = keys[(s + t) / 2];
        keys[(s + t) / 2] = keys[s];
        while (s < t) {
            while (s < t && keycmp(key.key, keys[t].key) <= 0)
                t--;
            keys[s] = keys[t];
            if (s < t) {
	        while (s < t && keycmp(key.key, keys[s].key) >= 0)
		    s++;
	        keys[t] = keys[s];
	    }
	}
	keys[s] = key;
        if (s == k)
	    return;
        if (s < k) {
	    s++;
            t = orgt;
	}
        else {
	    s = orgs;
	    t--;
	}
    }
}

static void
leaf_insert_internal(leaf_t *leaf, void *key, void *val)
{
    int slot = ((uint64_t)key) % leaf->alloc_len;
    uint32_t iter = 0;
    while (leaf->arr[slot].key || leaf->arr[slot].val) {
        iter ++;
        slot = (slot + PROBING(iter)) % leaf->alloc_len;
	if (iter > leaf->alloc_len) {
	    printf("bad probe 1\n");
	    exit(0);
	}
    }
    leaf->arr[slot].key = key;
    leaf->arr[slot].val = val;
}

static keyval_t tmpkeys[order * 2];  // tls

static void *
leaf_insert_(void *leaf_, void *key, void *val, int pos, void **splitkey)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    leaf->arr[pos].key = key;
    leaf->arr[pos].val = val;
    leaf->len ++;
    if (!openhash) {
        if (leaf->len == order * 2) {
	    int len = 0;
        uint32_t i;
        
	    for (i = 0; i < leaf->alloc_len; i++)
	        if (leaf->arr[i].key || leaf->arr[i].val) {
		    tmpkeys[len++] = leaf->arr[i];
		    leaf->arr[i].key = 0;
		    leaf->arr[i].val = 0;
		}
            findk(tmpkeys, order * 2, order - 1);
	    for (i = order; i < 2 * order; i++)
	        leaf_insert_internal(leaf, tmpkeys[i].key, tmpkeys[i].val); 
	    leaf->len = order;
	    leaf_t *sib = leaf_create_withlen(init_keyvals_len);
	    for (i = 0; i < order; i++)
	        leaf_insert_internal(sib, tmpkeys[i].key, tmpkeys[i].val);
	    sib->len = order;
	    if (splitkey)
	        *splitkey = tmpkeys[order - 1].key;
	    return sib;
        }
    }
    return NULL;
}

static void *
leaf_create_(void *key, void *val)
{
    leaf_t *leaf = leaf_create_withlen(init_keyvals_len);
    int slot = key_hash_search(leaf, 0, leaf->arr, key, NULL);
    leaf_insert_(leaf, key, val, slot, NULL);
    leaf->len = 1;
    return leaf;
}

static int
leaf_keycmp(const void *v1, const void *v2)
{
    return keycmp(((keyval_t *)v1)->key, ((keyval_t *)v2)->key);
}

static keyval_arr_t *
leaf_getresults_(void *leaf_)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    int len = 0;
    uint32_t i;
    
    for (i = 0; i < leaf->alloc_len; i++)
        if (leaf->arr[i].key || leaf->arr[i].val) {
	    leaf->arr[len].key = leaf->arr[i].key;
	    leaf->arr[len].val = leaf->arr[i].val;
	    len++;
	}
    qsort(leaf->arr, leaf->len, sizeof(keyval_t), leaf_keycmp);
    return (keyval_arr_t *)leaf;
}

static void
leaf_print_(void *leaf_)
{
    keyval_arr_t *leaf = (keyval_arr_t *)leaf_;
    uint32_t i;
    
    printf("leaf length %d, alloclen %d\n", leaf->len, leaf->alloc_len);
    for (i = 0; i < leaf->alloc_len; i++) {
        if (leaf->arr[i].key || leaf->arr[i].val)
	    printf("%p\t", leaf->arr[i].key);
    }
    printf("\n");
}

const leaf_type leaf_hash = {
    .leaf_setutil = leaf_setutil_,
    .leaf_create = leaf_create_,
    .leaf_find = leaf_find_,
    .leaf_insert = leaf_insert_,
    .leaf_getresults = leaf_getresults_,
    .leaf_traverse = leaf_traverse_,
    .leaf_print = leaf_print_,
};


