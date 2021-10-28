#include "btree.h"
#include <string.h>
#include <stdlib.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

enum { order = 4 };

typedef struct {
    short nkeys;
    void *parent;
    void *keys[2 * order + 1];
    void *pts[2 * order + 1];	// maybe values or nodes, depending on the node type
    void *next;
} node_t;

typedef struct {
    short nlevel;
    key_cmp_t keycmp;
    node_t *root;
} btree_t;

static void * JSHARED_ATTR maxkey = 0;

size_t
btree_getsize(void)
{
    return sizeof(btree_t);
}

void
btree_setutil(void *key)
{
    maxkey = key;
}

void
btree_init(void *btree, key_cmp_t fn)
{
    btree_t *bt = (btree_t *)btree;
    bt->nlevel = 0;
    bt->root = NULL;
    bt->keycmp = fn;
}

// return the position of the first element that is greater or equal than key
static int
key_binary_search(int len, void **keys, void *key, key_cmp_t keycmp)
{
    int res = keycmp(key, keys[len - 1]);

    if (res > 0)
        return len;
    if (len == 1)
	return 0;
    if (len == 2) {
        if (keycmp(key, keys[0]) <= 0)
	    return 0;
	return 1;
    }
    int left = 0;
    int right = len - 2;
    int mid;
    while (left < right) {
	mid = (left + right) / 2;
	res = keycmp(key, keys[mid]);
	if (!res)
	    return mid;
	else if (res < 0)
	    right = mid - 1;
	else
	    left = mid + 1;
    }
    res = keycmp(key, keys[left]);
    if (res > 0)
        return left + 1;
    return left;
}

void *
btree_find(void *btree, void *key)
{
    int i;
    
    btree_t *bt = (btree_t *)btree;
    if (!bt->nlevel)
	return NULL;
    node_t *node = bt->root;
    // search for the leaf node
    int ipt;
    for (i = 0; i < bt->nlevel - 1; i++){
	ipt = key_binary_search(node->nkeys, node->keys, key, bt->keycmp);
	node = (node_t *)node->pts[ipt];
    }
    // search for the value node within the leaf node
    ipt = key_binary_search(node->nkeys, node->keys, key, bt->keycmp);
    if (ipt == node->nkeys)
	return NULL;
    return node->pts[ipt];
}

static node_t *
btree_split_internal(node_t *node) 
{
    node_t *newsib = (node_t *)malloc(sizeof(node_t));
    newsib->nkeys = order;
    memcpy(newsib->keys, &node->keys[order + 1], sizeof(node->keys[0])* order);
    memcpy(newsib->pts, &node->pts[order + 1], sizeof(node->pts[0])* (order + 1));
    node->nkeys = order;
    return newsib;
}

// node <= key < newsib
static void
btree_insert_index(btree_t *bt, void *key, node_t *node, node_t *newsib) 
{
    int i;
    
    if (!node->parent) {
	node_t *newroot = (node_t *)malloc(sizeof(node_t));
	newroot->nkeys = 1;
	newroot->parent = NULL;
	newroot->keys[0] = key;
	newroot->pts[0] = node;
	newroot->pts[1] = newsib;
	bt->root = newroot;
	node->parent = newroot;
	newsib->parent = newroot;
	bt->nlevel ++;
    } else {
	node_t *parent = (node_t *)node->parent;
	int ikey = key_binary_search(parent->nkeys, parent->keys, key, bt->keycmp);
	// insert newkey at ikey, values at ikey + 1
	for (i = parent->nkeys - 1; i >= ikey; i--)
	    parent->keys[i + 1] = parent->keys[i];
	for (i = parent->nkeys; i >= ikey + 1; i--)
	    parent->pts[i + 1] = parent->pts[i];
	parent->keys[ikey] = key;
	parent->pts[ikey + 1] = newsib;
	parent->nkeys ++;
	newsib->parent = parent;
	if (parent->nkeys == 2 * order + 1) {
	    void *newkey = parent->keys[order];
	    node_t *newparent = btree_split_internal(parent);
	    // push up newkey
	    btree_insert_index(bt, newkey, parent, newparent);
	    // fix parent pointers
            for (i = 0; i < newparent->nkeys + 1; i++)
		((node_t *)newparent->pts[i])->parent = newparent;
	}
    }
}

static void
btree_split_leaf(btree_t *bt, node_t *node)
{
    // split into two nodes 
    node_t *newsib = (node_t *)malloc(sizeof(node_t));
    newsib->nkeys = order;
    memcpy(newsib->keys, &node->keys[order + 1], order * sizeof(node->keys[0]));
    memcpy(newsib->pts, &node->pts[order + 1], order * sizeof(node->pts[0]));
    node->nkeys = order + 1;
    newsib->next = node->next;
    node->next = newsib;
    btree_insert_index(bt, node->keys[order], node, newsib);
}

void
btree_insert(void *btree, void *key, void *val)
{
    btree_t *bt = (btree_t *)btree;
    node_t *node = (node_t *)bt->root;
    if (!bt->nlevel) {
	node = (node_t *)malloc(sizeof(node_t));
        node->parent = 0;
	node->nkeys = 1;
	node->keys[0] = maxkey;
	node->pts[0] = val;
	node->next = 0;
	bt->root = node;
        bt->nlevel = 1;
	return;
    }
    // search for the leaf node
    int ikey = -1;
    int i;
    for (i = 0; i < bt->nlevel - 1; i++){
	ikey = key_binary_search(node->nkeys, node->keys, key, bt->keycmp);
	node = (node_t *)node->pts[ikey];
    }
    // search within the leaf node
    ikey = key_binary_search(node->nkeys, node->keys, key, bt->keycmp);
    // shift keys and values right
    for (i = node->nkeys - 1; i >= ikey; i--) {
	node->pts[i + 1] = node->pts[i];
	node->keys[i + 1] = node->keys[i];
    }
    // insert the key and value at ikey
    node->keys[ikey] = key;
    node->pts[ikey] = val;
    node->nkeys ++;
    // split the leaf if full
    if (node->nkeys == 2 * order + 1)
	btree_split_leaf(bt, node);
}

void
btree_traverse(void *btree, void *arg, tv_callback_t cb)
{
    btree_t *bt = (btree_t *)btree;
    int i;
    
    if (!bt->nlevel)
	return;
    node_t *node = bt->root;
    for (i = 0; i < bt->nlevel - 1; i++)
	node = (node_t *)node->pts[0];
    while (node) {
	for (i = 0; i < node->nkeys; i++)
	    cb(node->pts[i], arg);
	node = (node_t *)node->next;
    }
}

void **
btree_getresults(void *btree, int *len, void **token_)
{
    btree_t *bt = (btree_t *)btree;
    node_t **token = (node_t **)token_;
    int i;
    
    if (bt && !bt->nlevel) {
        *token = NULL;
	*len = 0;
	return NULL;
    }
    if (!(*token)) {
        node_t *node = bt->root;
        for (i = 0; i < bt->nlevel - 1; i++)
	    node = (node_t *)node->pts[0];
	*token = node;
    } else
        *token = (node_t *)(*token)->next;
    if (*token) {
        *len = (*token)->nkeys;
        return (*token)->pts;
    }
    *len = 0;
    return NULL;
}

void
btree_print(void *btree)
{
    btree_t *bt = (btree_t *)btree;
    int i;
    
    printf("the tree is %d levels\n", bt->nlevel);
    if (!bt->nlevel)
	return;
    node_t *node = bt->root;
    for (i = 0; i < bt->nlevel - 1; i++)
	node = (node_t *)node->pts[0];
    while (node) {
	for (i = 0; i < node->nkeys; i++)
	    printf("%p <= %p < ", node->pts[i], node->keys[i]);
	printf("\n");
	node = (node_t *)node->next;
    }
}

static void
btree_delete_level(node_t *node, int level)
{
    if (!level)
	free(node);
    else {
        int i;

        for (i = 0; i < node->nkeys; i++) {
            btree_delete_level((node_t *)node->pts[i], level - 1);
        }
    }
}

void
btree_delete(void *btree)
{
    btree_t *bt = (btree_t *)btree;
    if (bt->nlevel)
	btree_delete_level(bt->root, bt->nlevel);
}
