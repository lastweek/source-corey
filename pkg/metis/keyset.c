#include "keyset.h"
#include "redblack.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

//#define KEEP_SORTED
//#define RBT2

static key_cmp_t JSHARED_ATTR cmpfn = 0;
static combine_t JSHARED_ATTR combiner = 0;

enum { start_knarray_size = 8 };
enum { start_valarray_size = 8 };
enum { def_vals_arr_len = 8 };

typedef keyvals_arr_t setnode_t;

void 
ks_setutil(key_cmp_t keycmp_fn, combine_t cb) 
{
   cmpfn = keycmp_fn;
   combiner = cb;
}

static void
insert_val(keyvals_t *node, void *val, uint32_t factor)
{
    if (node->alloc_len == 0) {
	node->alloc_len = def_vals_arr_len;
        if (factor > 0 && factor < node->alloc_len)
            node->alloc_len = factor;
        node->vals = (void **)malloc(sizeof(void *) * node->alloc_len);
    } 
#ifdef VAL_ARR_LIST
    else if (node->alloc_len == node->len) {
        if (node->len < al_vals_arr_llen) {
	    node->alloc_len *= 2;
	    node->vals = (void **)realloc(node->vals, sizeof(void *) * node->alloc_len);
	} else {
	    void **n = (void **)malloc((al_vals_arr_llen) * sizeof(void *));
	    n[al_vals_arr_llen] = (void *)node->vals;
	    node->vals = n;
	    node->alloc_len += al_vals_arr_llen;
	}
    }
    node->vals[node->len % al_vals_arr_llen] = val;
    node->len++;
#else
    else if (node->alloc_len == node->len) {
        node->alloc_len *= 2;
	node->vals = (void **)realloc(node->vals, sizeof(void *) * node->alloc_len);
    }
    node->vals[node->len++] = val;
#endif
    if (combiner && node->len == def_vals_arr_len)
        node->len = combiner(node->key, node->vals, node->len);
}

void *
ks_create_kvs(keyvals_t *kvs)
{
#ifdef RBT2
    // create an rbtree
    RedBlackTree *tree = (RedBlackTree *)malloc(sizeof(RedBlackTree));
    printf("Hello shit\n");
    rbtInit(tree, cmpfn);
    keyvals_t *kn = (keyvals_t *)malloc(sizeof(keyvals_t));
    memset(kn, 0, sizeof(keyvals_t));
    kn->key = key;
    insert_val(kn, val, factor);
    rbtInsert(tree, key, kn);
    return tree;
#elif defined(RBT1)
    keyvals_t *node = (keyvals_t *)malloc(sizeof(keyvals_t));
    memset(node, 0, sizeof(keyvals_t));
    node->key = key;
    insert_val(node, val, factor);
    return node;
#else
    setnode_t *s = malloc(sizeof(setnode_t));
    s->alloc_len = start_knarray_size;
    s->arr = malloc(s->alloc_len * sizeof(keyvals_t));
    memset(s->arr, 0, s->alloc_len * sizeof(keyvals_t));
    // use the first node
    s->arr[0] = *kvs;
    s->len = 1;
    return s;
#endif
}

void *
ks_create(void *key, void *val, int factor)
{
#ifdef RBT2
    // create an rbtree
    RedBlackTree *tree = (RedBlackTree *)malloc(sizeof(RedBlackTree));
    printf("Hello shit\n");
    rbtInit(tree, cmpfn);
    keyvals_t *kn = (keyvals_t *)malloc(sizeof(keyvals_t));
    memset(kn, 0, sizeof(keyvals_t));
    kn->key = key;
    insert_val(kn, val, factor);
    rbtInsert(tree, key, kn);
    return tree;
#elif defined(RBT1)
    keyvals_t *node = (keyvals_t *)malloc(sizeof(keyvals_t));
    memset(node, 0, sizeof(keyvals_t));
    node->key = key;
    insert_val(node, val, factor);
    return node;
#else
    setnode_t *s = malloc(sizeof(setnode_t));
    s->alloc_len = start_knarray_size;
    s->arr = malloc(s->alloc_len * sizeof(keyvals_t));
    memset(s->arr, 0, s->alloc_len * sizeof(keyvals_t));
    // use the first node
    s->arr[0].key = key;
    insert_val(&s->arr[0], val, factor);
    s->len = 1;
    return s;
#endif
}

#ifdef KEEP_SORTED
static int
key_binary_search(int len, keyvals_t *keys, void *key, int *bfound)
{
    int res = cmpfn(key, keys[len - 1].key);
    *bfound = 0;
    if (!res) {
        *bfound = 1;
        return len - 1;
    }
    if (res > 0 || len == 1)
	return len;
    if (len == 2) {
        if (cmpfn(key, keys[0].key) <= 0)
	    return 0;
	return 1;
    }
    int left = 0;
    int right = len - 2;
    int mid;
    while (left < right) {
	mid = (left + right) / 2;
	res = cmpfn(key, keys[mid].key);
	if (!res) {
	    *bfound = 1;
	    return mid;
	}
	else if (res < 0)
	    right = mid - 1;
	else
	    left = mid + 1;
    }
    res = cmpfn(key, keys[left].key);
    if (res > 0)
        return left + 1;
    return left;
}
#endif

int
ks_insert_kvs(void *node, keyvals_t *kvs)
{
    uint32_t i;

    keyvals_t *kn = 0;
    int bnewkey = 0;
    void *key = kvs->key;
    setnode_t *s = (setnode_t *)node;
#ifndef KEEP_SORTED
    for (i = 0; i < s->len; i++)
        if (!cmpfn(key, s->arr[i].key)) {
            kn = &s->arr[i];
	    break;
	}
    if (!kn) {
	// insert the node into the keynode set
	if (s->len == s->alloc_len) {
	    s->alloc_len *= 2;
	    s->arr = realloc(s->arr, s->alloc_len * sizeof(keyvals_t));
	}
	kn = &s->arr[s->len++];
	kn->alloc_len = 0;
	kn->len = 0;
	kn->key = key;
	bnewkey = 1;
    }
#else
    int bfound = 0;
    int pos = s->len;
    if (s->len > 32) {
        pos = key_binary_search(s->len, s->arr, key, &bfound);
        if (bfound)
            kn = &s->arr[pos];
    }
    else
        for (int i = 0; i < s->len; i++) {
	    int res = cmpfn(key, s->arr[i].key);
            if (!res) {
	        bfound = 1;
	        kn = &s->arr[i];
	        break;
	    }
	    if (res < 0) {
	        pos = i;
	        break;
	    }
	}
    if (!kn) {
	// insert the node into the keynode set
	if (s->len == s->alloc_len) {
	    s->alloc_len *= 2;
	    s->arr = realloc(s->arr, s->alloc_len * sizeof(keyvals_t));
	}
	if (pos < s->len)
            memmove(&s->arr[pos + 1], &s->arr[pos], sizeof(s->arr[0]) * (s->len - pos));
	kn = &s->arr[pos];
	s->len ++;
	*kn = *kvs;
	bnewkey = 1;
    }
#endif
    return bnewkey;
}

int
ks_insert(void *node, int hash, void *key, void *val, int factor)
{
    keyvals_t *kn = 0;
    int bnewkey = 0;
#ifdef RBT2
    RedBlackTree *tree = (RedBlackTree *)node;
    NodeType *p = rbtFind(tree, key);
    if (!p) {
        kn = (keyvals_t *)malloc(sizeof(keyvals_t));
        memset(kn, 0, sizeof(keyvals_t));
        kn->key = key;
        rbtInsert(tree, key, kn);
    } else 
        kn = (keyvals_t *)p->val;
#elif defined(RBT1)
    kn = (keyvals_t *)node;
#else
    setnode_t *s = (setnode_t *)node;
    uint32_t i;

#ifndef KEEP_SORTED
    for (i = 0; i < s->len; i++) 
        if (!cmpfn(key, s->arr[i].key)) {
            kn = &s->arr[i];
	    break;
	}
    if (!kn) {
	// insert the node into the keynode set
	if (s->len == s->alloc_len) {
	    s->alloc_len *= 2;
	    s->arr = realloc(s->arr, s->alloc_len * sizeof(keyvals_t));
	}
	kn = &s->arr[s->len++];
	kn->alloc_len = 0;
	kn->len = 0;
	kn->key = key;
	bnewkey = 1;
    }
#else
    int bfound = 0;
    int pos = s->len;
    if (s->len > 32) {
        pos = key_binary_search(s->len, s->arr, key, &bfound);
        if (bfound)
            kn = &s->arr[pos];
    }
    else
        for (int i = 0; i < s->len; i++) {
	    int res = cmpfn(key, s->arr[i].key);
            if (!res) {
	        bfound = 1;
	        kn = &s->arr[i];
	        break;
	    }
	    if (res < 0) {
	        pos = i;
	        break;
	    }
	}
    if (!kn) {
	// insert the node into the keynode set
	if (s->len == s->alloc_len) {
	    s->alloc_len *= 2;
	    s->arr = realloc(s->arr, s->alloc_len * sizeof(keyvals_t));
	}
	if (pos < s->len)
            memmove(&s->arr[pos + 1], &s->arr[pos], sizeof(s->arr[0]) * (s->len - pos));
	kn = &s->arr[pos];
	s->len ++;
	kn->alloc_len = 0;
	kn->len = 0;
	kn->key = key;
	kn->hash = hash;
	bnewkey = 1;
    }
#endif
#endif
    insert_val(kn, val, factor);
    return bnewkey;
}

int
ks_getlen(void *node) 
{
#ifdef RBT2
    return ((RedBlackTree *)node)->nnode;
#elif defined(RBT1)
    return 1;
#else
    if (!node)
        return 0;
    return ((setnode_t *)node)->len;
#endif
}

#ifdef RBT2
static void
fill(void *arg, NodeType *p)
{
    keyvals_t **res = (keyvals_t **)arg;
    memcpy(*res,(keyvals_t *)p->val, sizeof(keyvals_t));
    (*res)++;
}
#endif

void
ks_getresults(void *node, keyvals_t **res)
{
    assert(node);
#ifdef RBT2
    RedBlackTree *tree = (RedBlackTree *)node;
    rbtInorder(tree, res, fill);
#elif defined(RBT1)
    memcpy(*res, node, sizeof(keyvals_t));
    (*res) ++;
#else
    setnode_t *s = (setnode_t *)node;
    memcpy(*res, s->arr, sizeof(keyvals_t) * s->len);
    (*res) += s->len;
#endif
}

keyvals_arr_t *
ks_getinternal(void *node)
{
    return (keyvals_arr_t *)node;   
}

void
ks_delete(void *node_)
{
    uint32_t i;

    keyvals_arr_t *node = (keyvals_arr_t *)node_;
    for (i = 0; i < node->len; i++)
        if (node->arr[i].vals) {
	    free(node->arr[i].vals);
	    node->arr[i].vals = NULL;
	}
    if (node->arr) {
        free(node->arr);
        node->arr = NULL;
    }
    free(node);
}

int 
ks_find(void *node, void *key)
{
    if (!node)
        return 0;
    setnode_t *s = (setnode_t *)node;
    uint32_t i;

    for (i = 0; i < s->len; i++) {
        if (!cmpfn(key, s->arr[i].key))
	    return s->arr[i].len;
    }
    return 0;
}

void *
ks_rand_select(void *node)
{
    setnode_t *s = (setnode_t *)node;
    int pos = rand() % s->len;
    return s->arr[pos].key;
}
