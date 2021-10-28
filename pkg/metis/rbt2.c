#include "kvhandler.h"
#include "intermediate.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "redblack.h"
#include "keyset.h"

enum { init_valloclen = 8 };

typedef RedBlackTree rbt_bucket_t;
static key_cmp_t keycmp = NULL;
static int map_rows = 0;
static int map_cols = 0;
static rbt_bucket_t **mbks = 0;

static int
hash_cmp(const void *v1, const void *v2)
{
    int a = (int)(long)v1;
    int b = (int)(long)v2;
    if (a > b) return 1;
    if (a == b) return 0;
    return -1;
}

static int 
keyval_cmp(const void *v1, const void *v2) 
{
    keyvals_t *p1 = (keyvals_t *)v1;
    keyvals_t *p2 = (keyvals_t *)v2;
    return keycmp(p1->key, p2->key);
}

static void 
counting(void *arg, NodeType *p)
{
    int *c = (int *)arg;
    *c += ks_getlen(p->val);
}

static void
fill(void *arg, NodeType *p)
{
    ks_getresults(p->val, (keyvals_t **)arg);
}

static int
rbt_getlen(rbt_bucket_t *bucket)
{   
    int cnt = 0;
    rbtInorder(bucket, &cnt, counting);
    return cnt;
}

static int
mbks_init_(int rows, int cols, int nslots)
{
    map_rows = rows;
    map_cols = cols;
    mbks = (rbt_bucket_t **) malloc(rows * sizeof(rbt_bucket_t *));
    for (int i = 0; i < rows; i++) {
	mbks[i] = (rbt_bucket_t *) calloc(cols, sizeof(rbt_bucket_t));
	for (int j = 0; j < cols; j++) {
#ifdef RBT1
            rbtInit(&mbks[i][j], kcmp_fn);
#else
            rbtInit(&mbks[i][j], hash_cmp);
#endif
	}
    }
    return 0;
}

static int
to_yield_(rbt_bucket_t *bucket)
{
    return 0;
}

static int
map_put_(int row, int col, unsigned hash, void *key, void *val)
{
    rbt_bucket_t *bucket = &mbks[row][col];
#ifdef RBT1
    NodeType *p = rbtFind(bucket, key);
#else
    NodeType *p = rbtFind(bucket, (void *)(long)hash);
#endif
    if (!p) {
	void *set = ks_create(key, val, 0);
#ifdef RBT1
	rbtInsert(bucket, key, set);
#else
        rbtInsert(bucket, (void *)(long)hash, set);
#endif
    } else {
	ks_insert(p->val, hash, key, val, 0);
    }
    return to_yield_(bucket);
}

static void
setutils_(key_cmp_t kfn, combine_t cfn)
{
    keycmp = kfn;
    ks_setutil(kfn, cfn);
}

static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer)
{
    if (!mbks)
        return;
    int *lens = (int *) malloc(map_rows * sizeof(int));
    int total_len = 0;
    for (int i = 0; i < map_rows; i++) {
         lens[i] = rbt_getlen(&mbks[i][col]);
	 total_len += lens[i];
    }
    keyvals_t *kvs = (keyvals_t *) malloc(total_len * sizeof(keyvals_t));
    keyvals_t *idx = kvs;
    for (int i = 0; i < map_rows; i++)
         rbtInorder(&mbks[i][col], &idx, fill);

    qsort(kvs, total_len, sizeof(keyvals_t), keyval_cmp);
    int start = 0;
    int valloclen = init_valloclen;
    void **vals = malloc(sizeof(void *) * valloclen);
    while (start < total_len) {
	int end = start + 1;
	while (end < total_len && !keycmp(kvs[start].key, kvs[end].key))
	    end ++;
	int vlen = 0;
	for (int i = start; i < end; i++)
	    vlen += kvs[i].len;
	if (vlen > valloclen) {
	    while (vlen > valloclen)
	        valloclen *= 2;
	    vals = (void **)realloc(vals, sizeof(void *) * valloclen);
	}
	void **dest = vals;
	for (int i = start; i < end; i++) {
	    memcpy(dest, kvs[i].vals, sizeof(void *) * kvs[i].len);
	    dest += kvs[i].len;
	}
	if (reducer)
	    reducer(kvs[start].key, vals, vlen);
	else
	    for (int i = 0; i < vlen; i++)
	        kv_put_key_val(rbucket, kvs[start].key, vals[i]);
	start = end;
    }
    if (vals)
        free(vals);
    if (kvs)
        free(kvs);
    for (int i = 0; i < map_rows; i++)
        rbtDestroy(&mbks[i][col]);
}


const kvhandler_t rbt2 = {
    .kv_mbks_init = mbks_init_,
    .kv_map_put = map_put_,
    .kv_setutils = setutils_,
    .kv_do_reduce_task = do_reduce_task_,
};

