#include "btree_leaf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

static key_cmp_t JSHARED_ATTR keycmp = NULL;
typedef keyval_arr_t leaf_t;

enum { init_keyvals_len = 577 };
enum { order = 277 };

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
    if (len)
        leaf->arr = (keyval_t *)malloc(sizeof(keyval_t) * leaf->alloc_len);
    else
	leaf->arr = NULL;
    leaf->len = 0;
    return leaf;
}

static void *
leaf_create_(void *key, void *val)
{
    leaf_t *leaf = leaf_create_withlen(init_keyvals_len);
    leaf->arr[0].key = key;
    leaf->arr[0].val = val;
    leaf->len = 1;
    return leaf;
}

static void
leaf_traverse_(void *leaf_, void *arg, tv_callback_t cb)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    uint32_t i;

    for (i = 0; i < leaf->len; i++)
        cb(leaf->arr[i].val, arg);
}

static int
key_binary_search(int len, keyval_t *keys, void *key, int *bfound)
{
    int res = keycmp(key, keys[len - 1].key);
    *bfound = 0;
    if (!res) {
        *bfound = 1;
        return len - 1;
    }
    if (res > 0 || len == 1)
	return len;
    if (len == 2) {
        if (keycmp(key, keys[0].key) <= 0)
	    return 0;
	return 1;
    }
    int left = 0;
    int right = len - 2;
    int mid;
    while (left < right) {
	mid = (left + right) / 2;
	res = keycmp(key, keys[mid].key);
	if (!res) {
	    *bfound = 1;
	    return mid;
	}
	else if (res < 0)
	    right = mid - 1;
	else
	    left = mid + 1;
    }
    res = keycmp(key, keys[left].key);
    if (res > 0)
        return left + 1;
    return left;
}

static int
leaf_find_(void *leaf_, void *key, void **val)
{
    leaf_t *leaf = (leaf_t *)leaf_;
    int bfound = 0;
    int pos = key_binary_search(leaf->len, leaf->arr, key, &bfound);
    if (bfound)
        *val = leaf->arr[pos].val;
    else
        *val = 0;
    return pos;
}

static void *
leaf_insert_(void *leaf_, void *key, void *val, int pos, void **splitkey)
{
    // search
    leaf_t *leaf = (leaf_t *)leaf_;
    if (leaf->len == leaf->alloc_len) {
        leaf->alloc_len *= 2;
        leaf->arr = (keyval_t *)realloc(leaf->arr, leaf->alloc_len * sizeof(keyval_t));
    }
    if (leaf->len > (uint32_t)pos)
        memmove(&leaf->arr[pos + 1], &leaf->arr[pos], (leaf->len - pos) * sizeof(keyval_t));
    leaf->len ++;
    leaf->arr[pos].key = key;
    leaf->arr[pos].val = val;
    if (leaf->len == order * 2) {
	// split into two
	leaf_t *sib = leaf_create_withlen(order * 2);
	sib->len = order;
	memcpy(sib->arr, &leaf->arr[order], order * sizeof(keyvals_t));
	leaf->len = order;
	*splitkey = leaf->arr[order - 1].key;
	return sib;
    }
    return NULL;
}

static keyval_arr_t *
leaf_getresults_(void *leaf)
{
    return (keyval_arr_t *)leaf;
}

static void
leaf_print_(void *leaf_)
{
    keyval_arr_t *leaf = (keyval_arr_t *)leaf_;
    uint32_t i;
    
    printf("leaf length %d, alloclen %d\n", leaf->len, leaf->alloc_len);
    for (i = 0; i < leaf->len; i++) {
	printf("%p\t", leaf->arr[i].key);
    }
    printf("\n");
}

const leaf_type leaf_array = {
    .leaf_setutil = leaf_setutil_,
    .leaf_create = leaf_create_,
    .leaf_find = leaf_find_,
    .leaf_insert = leaf_insert_,
    .leaf_getresults = leaf_getresults_,
    .leaf_traverse = leaf_traverse_,
    .leaf_print = leaf_print_,
};


