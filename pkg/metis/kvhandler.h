#ifndef KVHANDLER_H
#define KVHANDLER_H
#include "mr-def.h"

#define YIELD_NO		0	// Keep the data structure
#define YIELD_HASH   		1  	// Many dup. Yield to hash table
#define YIELD_APPEND		2	// Many keys, less duplications.
#define YIELD_BALANCED		3	// Large number of keys, a few duplications, unbalanced.

#define YIELD_CHECK_THRESH 0.1
#define YIELD_NON_HASH 10000
#define YIELD_DUP_THRESH 4
#define YIELD_CHECK_OCCUR_THRESH 8000
#define YIELD_MUST_KEYS_THRESH 20000

typedef struct {
    int (*kv_mbks_init)(int rows, int cols, int nslots);
    int (*kv_map_put)(int row, int col, unsigned hash, void *key, void * val);
    void (*kv_setutils)(key_cmp_t fn, combine_t combiner);
    void (*kv_map_put_kvs)(int row, int col, keyvals_t *kvs);

    /* reduce data for map buckets, and put them into reduce buckets */
    void (*kv_do_reduce_task)(int col, keyval_arr_t *rbucket, reduce_t reducer, int kra);
    int (*kv_yield_check)(int row, int total, int finished);
    void (*kv_draw)(int row, int col, draw_callback_t cb);
    void (*kv_prof_print)(void);
} kvhandler_t;

void kv_put_key_val(keyval_arr_t * kv_arr, void *key, void *val);
keyval_arr_t *kv_rbks_init(int cols);
void kv_reduce(keyval_arr_t * kv_arr, reduce_t reducer, int nenabled);
void kv_setutils(key_cmp_t fn, out_cmp_t out_cmp);

extern const kvhandler_t openhbb;
extern const kvhandler_t closehbb;
extern const kvhandler_t append;
extern const kvhandler_t rbt2;
extern const kvhandler_t btreekv;

#endif
