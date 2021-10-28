#include "intermediate.h"
#include "kvhandler.h"
#include <string.h>
#include "bench.h"
#ifdef JOS_USER
#include <inc/compiler.h>
#include <inc/lib.h>
#include <inc/sysprof.h>
#else
#include "pmc.h"
#endif

typedef struct {
    keyval_arr_t v;
} append_bucket_t;

enum { append_arr_initlen = 8 };
enum { init_valloclen = 8 };
enum { profile_qsort = 0 };
enum { profile_user_reduce = 0 };
enum { profile_map_time = 1 };

static key_cmp_t JSHARED_ATTR keycmp = NULL;
static combine_t JSHARED_ATTR combiner = NULL;
static int JSHARED_ATTR map_rows = 0;
static int JSHARED_ATTR map_cols = 0;
static append_bucket_t ** JSHARED_ATTR mbks = 0;
static JTLS uint64_t qsort_time = 0;
static JTLS uint64_t user_reduce = 0;
static JTLS uint64_t map_time = 0;

#ifdef JOS_USER
//static JTLS struct sysprof_arg kv_reduce_cache;
#else
static JTLS uint64_t kv_reduce_l1 = 0;
static JTLS uint64_t kv_reduce_l2 = 0;
static JTLS uint64_t kv_reduce_l3 = 0;
static JTLS uint64_t kv_reduce_inst = 0;
#endif

static int
mbks_init_(int rows, int cols, int nslots)
{
    int i;

    map_rows = rows;
    map_cols = cols;
    mbks = (append_bucket_t **) malloc(rows * sizeof(append_bucket_t *));
    for (i = 0; i < rows; i++)
	mbks[i] = (append_bucket_t *) calloc(cols, sizeof(append_bucket_t));
    return 0;
}

static void
setutils_(key_cmp_t kfn, combine_t cfn)
{
    keycmp = kfn;
    combiner = cfn;
}

static void
bucket_free(append_bucket_t *bucket)
{
    if (bucket->v.arr) {
        free(bucket->v.arr);
	bucket->v.arr = NULL;
	bucket->v.len = 0;
	bucket->v.alloc_len = 0;
    }
}

static int
to_yield_(append_bucket_t *bucket)
{
    return 0;
}

static int
map_put_(int row, int col, unsigned hash, void *key, void *val)
{
    uint64_t s = 0;
    if (profile_map_time)
        s = read_tsc();
    append_bucket_t *bucket = &mbks[row][col];
    if (!bucket->v.alloc_len) {
        bucket->v.alloc_len = append_arr_initlen;
	bucket->v.arr = (keyval_t *)malloc(bucket->v.alloc_len * sizeof(keyval_t));
    }
    else if (bucket->v.alloc_len == bucket->v.len) {
        bucket->v.alloc_len *= 2;
	bucket->v.arr = (keyval_t *)realloc(bucket->v.arr, bucket->v.alloc_len * sizeof(keyval_t));
    }
    bucket->v.arr[bucket->v.len].key = key;
    bucket->v.arr[bucket->v.len].val = val;
    bucket->v.len ++;
    if (profile_map_time)
        map_time += read_tsc() - s;

    return to_yield_(bucket);
}

static void
map_put_kvs_(int row, int col, keyvals_t *kvs)
{
    uint32_t i;

    for (i = 0; i < kvs->len; i++)
        map_put_(row, col, kvs->hash, kvs->key, kvs->vals[i]);
}

static inline int
keyvals_cmp(const void *kvs1, const void *kvs2)
{
    return keycmp((*((keyval_t **) kvs1))->key, (*((keyval_t **) kvs2))->key);
}

static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer, int kra)
{
    int i;
    uint32_t j;

    if (!mbks)
        return;
    int total_len = 0;
    for (i = 0; i < map_rows; i++)
        total_len += mbks[i][col].v.len;
    keyval_t **kvs = (keyval_t **) malloc(total_len * sizeof(keyval_t *));
    int cur = 0;
    for (i = 0; i < map_rows; i++) {
        for (j = 0; j < mbks[i][col].v.len; j++) {
	    kvs[cur + j] = &mbks[i][col].v.arr[j];
	}
        cur += mbks[i][col].v.len;
    }
    uint64_t s = 0;
#ifdef JOS_USER
    //struct sysprof_arg sarg;
#else
    uint64_t sl1, sl2, sl3, sinst;
#endif
    if (profile_qsort) {
	s = read_tsc();
#ifdef JOS_USER
        //sysprof_cache(&sarg);
#else
        sl1 = pmc_l1_miss();
        sl2 = pmc_l2_miss();
        sl3 = pmc_l3_miss();
        sinst = pmc_ret_ins();
#endif
    }
    qsort(kvs, total_len, sizeof(keyval_t *), keyvals_cmp);
    if (profile_qsort) {
	qsort_time += read_tsc() - s;
#ifdef JOS_USER
        //sysprof_cache_end(&sarg);
	//kv_reduce_cache.amd_cache.l1_miss_cnt += sarg.amd_cache.l1_miss_cnt;
	//kv_reduce_cache.amd_cache.l2_miss_cnt += sarg.amd_cache.l2_miss_cnt;
	//kv_reduce_cache.amd_cache.l3_miss_cnt += sarg.amd_cache.l3_miss_cnt;
	//kv_reduce_cache.amd_cache.inst_ret += sarg.amd_cache.inst_ret;
#else
        kv_reduce_l1 += pmc_l1_miss() - sl1;
        kv_reduce_l2 += pmc_l2_miss() - sl2;
        kv_reduce_l3 += pmc_l3_miss() - sl3;
        kv_reduce_inst += pmc_ret_ins() - sinst;
#endif
    }
    uint64_t ur_start = 0;
    if (profile_user_reduce)
	ur_start = read_tsc();
    int start = 0;
    int valloclen = init_valloclen;
    void **vals = malloc(sizeof(void *) * valloclen);

    while (start < total_len) {
	int end = start + 1;
	while (end < total_len && !keycmp(kvs[start]->key, kvs[end]->key))
	    end ++;
	int vlen = 0;
	vlen = end - start;
	if (vlen > valloclen) {
	    while (vlen > valloclen)
	        valloclen *= 2;
	    vals = (void **)realloc(vals, sizeof(void *) * valloclen);
	}
	for (i = 0; i < vlen; i++)
	    vals[i] = kvs[start + i]->val;
	if (reducer)
	    reducer(kvs[start]->key, vals, vlen);
	else if (kra) {
	    kv_put_key_val(rbucket, kvs[start]->key, (void *)vals);
	    valloclen = init_valloclen;
	    vals = (void **)malloc(sizeof(void *) * valloclen);
	}
	else
	    for (i = 0; i < vlen; i++)
	        kv_put_key_val(rbucket, kvs[start]->key, vals[i]);
	start = end;
    }
    if (vals)
        free(vals);
    for (i = 0; i < map_rows; i++)
        bucket_free(&mbks[i][col]);
    if (kvs)
        free(kvs);
    if (profile_user_reduce)
        user_reduce += (read_tsc() - ur_start);
}

static void
prof_print_(void)
{
    if (profile_map_time) {
#ifdef JOS_USER
#else
        printf("   Map time: %14lu(ms)\n",
	        map_time * 1000 / get_cpu_freq());
	        
#endif
    }
    if (profile_qsort) {
#ifdef JOS_USER
#if 0
        printf("cpu %d:   Append: %lu(ms) %lu %lu %lu %lu\n", 
	        core_env->pid, 
	        qsort_time * 1000 / get_cpu_freq(), 
	       kv_reduce_cache.amd_cache.l1_miss_cnt,
	        kv_reduce_cache.amd_cache.l2_miss_cnt,
	        kv_reduce_cache.amd_cache.l3_miss_cnt,
	        kv_reduce_cache.amd_cache.inst_ret);
#endif
#else
        printf("   Append: %14lu(ms), %14lu, %14lu, %14lu, %14lu\n", 
	        qsort_time * 1000 / get_cpu_freq(), 
	        kv_reduce_l1, kv_reduce_l2, kv_reduce_l3, kv_reduce_inst);
#endif

    }
    if (profile_user_reduce) {
#ifdef JOS_USER
	printf("cpu %d: us_reduce: %lu(ms)\n", core_env->pid, user_reduce * 1000 / get_cpu_freq());
#else
	printf("us_reduce: %lu(ms)\n", user_reduce * 1000 / get_cpu_freq());
#endif
    }
    if (profile_user_reduce && profile_qsort) {
#ifdef JOS_USER
	printf("cpu %d: profiled time: %lu(ms)\n", core_env->pid, (user_reduce + qsort_time) * 1000 / get_cpu_freq());
#else
	printf("profiled_time: %lu(ms)\n", (user_reduce + qsort_time) * 1000 / get_cpu_freq());
#endif
    }
}

const kvhandler_t append = {
    .kv_mbks_init = mbks_init_,
    .kv_map_put = map_put_,
    .kv_setutils = setutils_,
    .kv_do_reduce_task = do_reduce_task_,
    .kv_map_put_kvs = map_put_kvs_,
    .kv_prof_print = prof_print_,
};

