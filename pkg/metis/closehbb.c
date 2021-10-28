#include "intermediate.h"
#include "kvhandler.h"
#include <string.h>
#include <assert.h>
#include <math.h>

enum { def_hash_slots = 2401 };
enum { combine_thres = 8 };
enum { init_vals_len = 8 };

typedef struct {
    size_t capacity;
    size_t size;
    keyvals_t *entries;
} hash_bucket_t;

static int hash_slots = def_hash_slots;
static key_cmp_t keycmp = NULL;
static combine_t combiner = NULL;
static int map_rows = 0;
static int map_cols = 0;
static hash_bucket_t **mbks = 0;

static int
mbks_init_(int rows, int cols, int nslots)
{
    if (nslots)
        hash_slots = nslots;
    map_rows = rows;
    map_cols = cols;
    int prime = 0;
    while (!prime) {
        prime = 1;
    	for (int i = 2; i <= sqrt(hash_slots); i++) {
            if (hash_slots % i == 0) {
	        hash_slots ++;
	        prime = 0;
	        break;
	    }
        }
    }
    printf("prime is %d\n", hash_slots);
    mbks = (hash_bucket_t **) malloc(rows * sizeof(hash_bucket_t *));
    for (int i = 0; i < rows; i++) {
	mbks[i] = (hash_bucket_t *) calloc(cols, sizeof(hash_bucket_t));
	for (int j = 0; j < cols; j++) {
	    mbks[i][j].capacity = hash_slots;
	    mbks[i][j].size = 0;
	    mbks[i][j].entries = (keyvals_t *) calloc(mbks[i][j].capacity, sizeof(keyvals_t));
	}
    }
    return 0;
}

static void
setutils_(key_cmp_t kfn, combine_t cfn)
{
    keycmp = kfn;
    combiner = cfn;
}

static void
bucket_free(hash_bucket_t *bucket)
{
    free(bucket->entries);
    bucket->entries = 0;
}

static int
to_yield_(hash_bucket_t *bucket)
{
    return 0;
}

static int
map_put_(int row, int col, unsigned hash, void *key, void *val)
{
    hash_bucket_t *bucket = &mbks[row][col];
    if (bucket->capacity * 0.75 < bucket->size) {
        bucket->capacity *= 2;
	printf("realloc to %ld\n", bucket->capacity);
        bucket->entries = (keyvals_t *) realloc(bucket->entries, bucket->capacity * sizeof(keyvals_t));
	memset(&bucket->entries[bucket->capacity / 2], 0, sizeof(keyvals_t) * bucket->capacity / 2);
    }
    int islot = hash % hash_slots;
    int prob = 1;
    for (int i = 0; i < bucket->capacity; i++) {
        if (!bucket->entries[islot].len)
	    break;
	if (!keycmp(key, bucket->entries[islot].key))
	    break;
	prob *= 2;
	islot = (islot + prob) % bucket->capacity;
    }
    keyvals_t *slot = &bucket->entries[islot];
    if (!slot->len) {
        bucket->size ++;
        slot->key = key;
    }
    if (slot->len == slot->alloc_len) {
        if (slot->alloc_len) {
	    slot->alloc_len *= 2;
	    slot->vals = realloc(slot->vals, slot->alloc_len * sizeof(void *));
	}
	else {
	    slot->alloc_len = combine_thres;
	    slot->vals = malloc(slot->alloc_len * sizeof(void *));
	}
    }
    slot->vals[slot->len++] = val;
    if (combiner && slot->len == combine_thres)
	slot->len = combiner(slot->key, slot->vals, slot->len);
    return to_yield_(bucket);
}

static inline int
keyvals_cmp(const void *kvs1, const void *kvs2)
{
    return keycmp((*((keyvals_t **) kvs1))->key, (*((keyvals_t **) kvs2))->key);
}

/** 
 * 1. first copy each slot with the same hash to an array.
 * 2. use qsort to sort the array.
 * 3. scan the array and do the reduce.
 */
static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer)
{
    if (!mbks)
        return;

    keyvals_t **kvs_arr = NULL;
    size_t kvs_len = 0;
    for (int i = 0; i < map_rows; i++)
        kvs_len += mbks[i][col].size;  	
    kvs_arr = (keyvals_t **)malloc(kvs_len * sizeof(keyvals_t *));
    int idx = 0;
    for (int i = 0; i < map_rows; i++)
        for (int j = 0; j < mbks[i][col].capacity; j++) 
	    if (mbks[i][col].entries[j].len)
	        kvs_arr[idx++] = &mbks[i][col].entries[j];
    qsort(kvs_arr, kvs_len, sizeof(keyvals_t *), keyvals_cmp);
    if (!kvs_len)
        goto leave;
    keyvals_t *cur_kvs = kvs_arr[0], *next_kvs = NULL;
    idx = 1;
    do {
        next_kvs = kvs_arr[idx];
        if (idx < kvs_len && keycmp(next_kvs->key, cur_kvs->key) == 0) {
    	    if (cur_kvs->alloc_len < cur_kvs->len + next_kvs->len) {
	        while (cur_kvs->alloc_len < cur_kvs->len + next_kvs->len)
	   	    cur_kvs->alloc_len *= 2;
		cur_kvs->vals =(void **) realloc(cur_kvs->vals, cur_kvs->alloc_len * sizeof(void *));
	    }
	    memcpy(cur_kvs->vals + cur_kvs->len, next_kvs->vals, next_kvs->len * sizeof(void *));
   	    cur_kvs->len += next_kvs->len;
	    if (combiner && cur_kvs->len >= combine_thres)
	        cur_kvs->len = combiner(cur_kvs->key, cur_kvs->vals, cur_kvs->len);
	    //free(next_kvs->vals);
	}
	else {
	    if (reducer)
		reducer(cur_kvs->key, cur_kvs->vals, cur_kvs->len);
	    else {
	        if (cur_kvs->len > 1)
	      	    printf("Warning: length for a key is %d. How to reduce it?\n", cur_kvs->len);
		for (int j = 0; j < cur_kvs->len; j++)
		    kv_put_key_val(rbucket, cur_kvs->key, cur_kvs->vals[j]);
	    }
	    //free(cur_kvs->vals);
	    cur_kvs = next_kvs;
	}
	idx++;
    } while (idx <= kvs_len);
leave:
    for (int i = 0; i < map_rows; i++)
	bucket_free(&mbks[i][col]);
    if (kvs_arr)
        free(kvs_arr);
}

const kvhandler_t closehbb = {
    .kv_mbks_init = mbks_init_,
    .kv_map_put = map_put_,
    .kv_setutils = setutils_,
    .kv_do_reduce_task = do_reduce_task_,
};

