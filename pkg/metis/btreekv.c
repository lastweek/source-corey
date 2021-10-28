#include "intermediate.h"
#include "kvhandler.h"
#include "btree.h"
#include "btree_leaf.h"
#include "keyset.h"
#include <string.h>
#include <assert.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

typedef struct {
    void *v;
    int ind_keys;
    int occurs;
} btree_bucket_t;

static const leaf_type *types[] = {
    &leaf_array,
    &leaf_hash,
};

enum { init_valloclen = 8 };
enum { index_array = 0, index_hash };
enum { def_leaf_type = index_hash };
enum { debug = 0 };

static key_cmp_t JSHARED_ATTR keycmp = NULL;
static combine_t JSHARED_ATTR combiner = NULL;
static int JSHARED_ATTR map_rows = 0;
static int JSHARED_ATTR map_cols = 0;
static btree_bucket_t ** JSHARED_ATTR mbks = 0;
static JTLS int curmap_keys = 0;
static JTLS int curmap_occurs = 0;

static __inline int
hashcmp(const void *v1, const void *v2)
{
    curmap_keys = 0;
    curmap_occurs = 0;
    uint64_t k1 = (uint64_t)v1;
    uint64_t k2 = (uint64_t)v2;
    if (k1 == k2) return 0;
    if (k1 < k2) return -1;
    return 1;
}

static int
mbks_init_(int rows, int cols, int nslots)
{
    int i, j;
    
    map_rows = rows;
    map_cols = cols;
    mbks = (btree_bucket_t **) malloc(rows * sizeof(btree_bucket_t *));
    for (i = 0; i < rows; i++) {
	mbks[i] = (btree_bucket_t *) calloc(cols, sizeof(btree_bucket_t));
	for (j = 0; j < cols; j++) {
	    mbks[i][j].v = malloc(btree_getsize());
	    btree_init(mbks[i][j].v, hashcmp);
	}
    }
    return 0;
}

static void
setutils_(key_cmp_t kfn, combine_t cfn)
{
    keycmp = kfn;
    combiner = cfn;
    types[def_leaf_type]->leaf_setutil(hashcmp);
    ks_setutil(keycmp, cfn);
    btree_setutil((void *)0xFFFFFFFFFFFFFFFFL);
}

static int
to_yield_(btree_bucket_t *bucket)
{
    return 0;
}

static void
map_put_kvs_(int row, int col, keyvals_t *kvs)
{
    int hash = kvs->hash;
    btree_bucket_t *bucket = &mbks[row][col];
    void *leaf = btree_find(bucket->v, (void *)(uint64_t)hash);
    int bnewkey = 1;
    if (leaf) {
        void *skey;
        void *ksnode = 0;
	int pos = types[def_leaf_type]->leaf_find(leaf, (void *)(uint64_t)hash, &ksnode);
    	if (ksnode) {
	    bnewkey = ks_insert_kvs(ksnode, kvs);
	}
	else {
	    ksnode = ks_create_kvs(kvs);
	    void *sib = types[def_leaf_type]->leaf_insert(leaf, (void *)(uint64_t)hash, ksnode, pos, &skey);
       	    if (sib)
	        btree_insert(bucket->v, skey, sib);
	}
    }
    else {
        void *ksnode = ks_create_kvs(kvs);
        leaf = types[def_leaf_type]->leaf_create((void *)(uint64_t)hash, ksnode);
	btree_insert(bucket->v, (void *)(uint64_t)hash, leaf);
    }
    if (bnewkey) {
        curmap_keys ++;
	bucket->ind_keys ++;
    }
    curmap_occurs ++;
    bucket->occurs ++;
}

static int
map_put_(int row, int col, unsigned hash, void *key, void *val)
{
    btree_bucket_t *bucket = &mbks[row][col];
    void *leaf = btree_find(bucket->v, (void *)(uint64_t)hash);
    int bnewkey = 1;
    if (leaf) {
        void *skey;
        void *ksnode = 0;
	int pos = types[def_leaf_type]->leaf_find(leaf, (void *)(uint64_t)hash, &ksnode);
    	if (ksnode) {
	    bnewkey = ks_insert(ksnode, hash, key, val, 0);
	}
	else {
	    ksnode = ks_create(key, val, 0);
	    void *sib = types[def_leaf_type]->leaf_insert(leaf, (void *)(uint64_t)hash, ksnode, pos, &skey);
       	    if (sib)
	        btree_insert(bucket->v, skey, sib);
	}
    }
    else {
        void *ksnode = ks_create(key, val, 0);
        leaf = types[def_leaf_type]->leaf_create((void *)(uint64_t)hash, ksnode);
	btree_insert(bucket->v, (void *)(uint64_t)hash, leaf);
    }
    if (bnewkey) {
        curmap_keys ++;
	bucket->ind_keys ++;
    }
    curmap_occurs ++;
    bucket->occurs ++;
    return to_yield_(bucket);
}

static int 
keyval_cmp(const void *v1, const void *v2) 
{
    keyvals_t *p1 = (keyvals_t *)v1;
    keyvals_t *p2 = (keyvals_t *)v2;
    return keycmp(p1->key, p2->key);
}

#define OPTIMIZE_REDUCE
#ifdef OPTIMIZE_REDUCE

static void
reduce_ksnodes(void **nodes, int nnodes, keyval_arr_t *rbucket, reduce_t reducer, int kra)
{
    int total_len = 0;
    int i;
    
    for (i = 0; i < nnodes; i++)
	 total_len += ks_getlen(nodes[i]);
    keyvals_t *kvs = (keyvals_t *) malloc(total_len * sizeof(keyvals_t));
    keyvals_t *idx = kvs;
    for (i = 0; i < nnodes; i++)
        ks_getresults(nodes[i], &idx);
    qsort(kvs, total_len, sizeof(keyvals_t), keyval_cmp);
    int start = 0;
    int valloclen = init_valloclen;
    void **vals = malloc(sizeof(void *) * valloclen);
    while (start < total_len) {
	int end = start + 1;
	while (end < total_len && !keycmp(kvs[start].key, kvs[end].key))
	    end ++;
	int vlen = 0;
	if (end - start > map_rows) {
	    for (i = start; i < end; i++)
	        printf("duplicated in one slot with %d, %d, %s\n", start, end, (char *)kvs[i].key);
	    exit(-1);
	}
	for (i = start; i < end; i++)
	    vlen += kvs[i].len;
	if (vlen > valloclen) {
	    while (vlen > valloclen)
	        valloclen *= 2;
	    vals = (void **)realloc(vals, sizeof(void *) * valloclen);
	}
	void **dest = vals;
	for (i = start; i < end; i++) {
	    memcpy(dest, kvs[i].vals, sizeof(void *) * kvs[i].len);
	    dest += kvs[i].len;
	}
	if (reducer)
	    reducer(kvs[start].key, vals, vlen);
	else if (kra) {
	    kv_put_key_val(rbucket, kvs[start].key, (void *)vals);
	    valloclen = init_valloclen;
	    vals = (void **)malloc(sizeof(void *) * valloclen);
	}
	else
	    for (i = 0; i < vlen; i++)
	        kv_put_key_val(rbucket, kvs[start].key, vals[i]);
	start = end;
    }
    if (vals)
        free(vals);
    if (kvs)
        free(kvs);

}

static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer, int kra)
{
    int i;
    
    if (!mbks)
        return;
    int *bt_lens = (int *) malloc(map_rows * sizeof(int));
    void **bt_tokens = (void **) malloc(map_rows * sizeof(void *));
    void ***bt_vals = (void ***) malloc(map_rows * sizeof(void **));
    int *cur_bt_next = (int *) malloc(map_rows * sizeof(int));
    memset(bt_tokens, 0, sizeof(void *) * map_rows);
    uint32_t *ksnode_next = (uint32_t *) malloc(map_rows * sizeof(uint32_t));
    keyval_arr_t **ksnodes = (keyval_arr_t **) malloc(map_rows * sizeof(keyval_t *));
    int *ended = (int *) malloc(map_rows * sizeof(int));

    for (i = 0; i < map_rows; i++) {
        bt_vals[i] = btree_getresults(mbks[i][col].v, &bt_lens[i], &bt_tokens[i]);
	if (bt_vals[i]) {
	    cur_bt_next[i] = 1;
	    ksnodes[i] = types[def_leaf_type]->leaf_getresults(bt_vals[i][0]);
	    ksnode_next[i] = 0;
	    ended[i] = 0;
	}
	else {
	    ended[i] = 1;
	}
    }
    void **same_key_ksnodes = (void **)malloc(sizeof(void *) * map_rows);
    while (1) {
	void **minkey = 0;
	int minindex = -1;
	for (i = 0; i < map_rows; i++) {
	    if (ended[i])
	        continue;
	    if (minindex < 0 || hashcmp(minkey, ksnodes[i]->arr[ksnode_next[i]].key) > 0) {
	        minindex = i;
		minkey = ksnodes[i]->arr[ksnode_next[i]].key;
	    }
	}
	if (minindex < 0)
	    break;
	int same_key_ksnodes_len = 0;
	for (i = 0; i < map_rows; i++) {
	    if (ended[i])
	        continue;
	    if (minindex == i || hashcmp(minkey, ksnodes[i]->arr[ksnode_next[i]].key) == 0) {
	        same_key_ksnodes[same_key_ksnodes_len++] = ksnodes[i]->arr[ksnode_next[i]].val;
	        if (!same_key_ksnodes[same_key_ksnodes_len - 1]) {
		    printf("ksnode_next[i] %d, len %d\n", ksnode_next[i], ksnodes[i]->len);
		    exit(0);
		}
		ksnode_next[i]++;
		// fix the next
		if (ksnode_next[i] == ksnodes[i]->len) {
		    // goto next ksnodes
		    if (cur_bt_next[i] == bt_lens[i]) {
		        // get next leaf array from btree
			bt_vals[i] = btree_getresults(NULL, &bt_lens[i], &bt_tokens[i]);
			if (!bt_vals[i])
			    ended[i] = 1;
			else {
			    cur_bt_next[i] = 1;
	                    ksnodes[i] = types[def_leaf_type]->leaf_getresults(bt_vals[i][0]);
	                    ksnode_next[i] = 0;
			}
		    }
		    else {
	                ksnodes[i] = types[def_leaf_type]->leaf_getresults(bt_vals[i][cur_bt_next[i]]);
		        cur_bt_next[i] ++;
			ksnode_next[i] = 0;
		    }
		}
	    }
	}
	reduce_ksnodes(same_key_ksnodes, same_key_ksnodes_len, rbucket, reducer, kra);
    }
    for (i = 0; i < map_rows; i++)
        btree_delete(mbks[i][col].v);
}

#else

static void
cb_leaf_getlen(void *kvnode, void *arg)
{
    int *len = (int *)arg;
    len[0] += ks_getlen(kvnode);
}

static void
cb_btree_getlen(void *leaf, void *arg)
{
    types[def_leaf_type]->leaf_traverse(leaf, arg, cb_leaf_getlen);
}

static void
cb_leaf_fill(void *kvnode, void *arg)
{
    ks_getresults(kvnode, (keyvals_t **)arg);
}

static void
cb_btree_fill(void *leaf, void *arg)
{
    types[def_leaf_type]->leaf_traverse(leaf, arg, cb_leaf_fill);
}

static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer)
{
    if (!mbks)
        return;
    int *lens = (int *) malloc(map_rows * sizeof(int));
    int total_len = 0;
    int i;
    
    for (i = 0; i < map_rows; i++) {
         lens[i] = 0;
         btree_traverse(mbks[i][col].v, &lens[i], cb_btree_getlen);
	 total_len += lens[i];
    }
    keyvals_t *kvs = (keyvals_t *) malloc(total_len * sizeof(keyvals_t));
    keyvals_t *idx = kvs;
    for (i = 0; i < map_rows; i++)
         btree_traverse(mbks[i][col].v, &idx, cb_btree_fill);
    qsort(kvs, total_len, sizeof(keyvals_t), keyval_cmp);
    int start = 0;
    int valloclen = init_valloclen;
    void **vals = malloc(sizeof(void *) * valloclen);
    while (start < total_len) {
	int end = start + 1;
	while (end < total_len && !keycmp(kvs[start].key, kvs[end].key))
	    end ++;
	int vlen = 0;
	if (end - start > map_rows) {
	    for (int i = start; i < end; i++)
	        printf("duplicated in one slot with %d, %d, %s\n", start, end, (char *)kvs[i].key);
	    exit(-1);
	}
	for (i = start; i < end; i++)
	    vlen += kvs[i].len;
	if (vlen > valloclen) {
	    while (vlen > valloclen)
	        valloclen *= 2;
	    vals = (void **)realloc(vals, sizeof(void *) * valloclen);
	}
	void **dest = vals;
	for (i = start; i < end; i++) {
	    memcpy(dest, kvs[i].vals, sizeof(void *) * kvs[i].len);
	    dest += kvs[i].len;
	}
	if (reducer)
	    reducer(kvs[start].key, vals, vlen);
	else
	    for (i = 0; i < vlen; i++)
	        kv_put_key_val(rbucket, kvs[start].key, vals[i]);
	start = end;
    }
    if (vals)
        free(vals);
    if (kvs)
        free(kvs);
    for (i = 0; i < map_rows; i++)
        btree_delete(mbks[i][col].v);
   
}
#endif
const kvhandler_t btreekv = {
    .kv_mbks_init = mbks_init_,
    .kv_map_put = map_put_,
    .kv_setutils = setutils_,
    .kv_do_reduce_task = do_reduce_task_,
    .kv_map_put_kvs = map_put_kvs_,
};

