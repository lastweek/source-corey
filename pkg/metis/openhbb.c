#include "intermediate.h"
#include "kvhandler.h"
#include "keyset.h"
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#include <inc/lib.h>
#include <inc/sysprof.h>
#include "bench.h"
#else
#include "pmc.h"
#endif

enum { def_hash_slots = 577 };
enum { local_reduce_thres = 8 };
enum { init_vals_len = 8 };		// initial length of the value array per key
enum { init_entry_len = 8 };		// initial length of an entry
enum { profile_qsort = 0 };
enum { profile_user_reduce = 0 };
static JTLS uint64_t qsort_time = 0;
static JTLS uint64_t user_reduce = 0;

#ifdef JOS_USER
static JTLS struct sysprof_arg kv_reduce_cache;
#else
static JTLS uint64_t kv_reduce_l1 = 0;
static JTLS uint64_t kv_reduce_l2 = 0;
static JTLS uint64_t kv_reduce_l3 = 0;
static JTLS uint64_t kv_reduce_inst = 0;
#endif

typedef struct {
    void *v;
} hash_entry_t;

typedef struct {
    size_t capacity;
    hash_entry_t *entries;
    int ind_keys;
    int occurs;
} hash_bucket_t;

static int JSHARED_ATTR hash_slots = def_hash_slots;
static key_cmp_t JSHARED_ATTR keycmp = NULL;
static combine_t JSHARED_ATTR combiner = NULL;
static int JSHARED_ATTR map_rows = 0;
static int JSHARED_ATTR map_cols = 0;
static hash_bucket_t ** JSHARED_ATTR mbks = 0;
static JTLS int curmap_keys = 0;
static JTLS int curmap_occurs = 0;

static int JSHARED_ATTR last_keys = -1;
static int JSHARED_ATTR last_occurs = -1;

static void * JSHARED_ATTR check_keys[16];
static unsigned long JSHARED_ATTR check_hashes[16];
static int JSHARED_ATTR found[16];
enum { uncheck = 0, checking, checked };
static int JSHARED_ATTR state = uncheck;
static int JSHARED_ATTR check_col = 0;

static int
mbks_init_(int rows, int cols, int nslots)
{
    int i;

    state = uncheck;
    curmap_keys = 0;
    curmap_occurs = 0;
    last_keys = -1;
    last_occurs = -1;
    //struct timeval t;
    //gettimeofday(&t, NULL);
    //srand(t.tv_usec);
    check_col = rand() % cols;
    if (nslots)
        hash_slots = nslots;
    map_rows = rows;
    map_cols = cols;
    hash_bucket_t **buckets = (hash_bucket_t **) malloc(rows * sizeof(hash_bucket_t *));
    for (i = 0; i < rows; i++) {
        int j;

        buckets[i] = (hash_bucket_t *) calloc(cols, sizeof(hash_bucket_t));
        for (j = 0; j < cols; j++) {
            buckets[i][j].capacity = hash_slots;
            buckets[i][j].entries = (hash_entry_t *) calloc(buckets[i][j].capacity,
                                    sizeof(hash_entry_t));
        }
    }
    mbks = buckets;
    return 0;
}

static void
setutils_(key_cmp_t kfn, combine_t cfn)
{
    keycmp = kfn;
    combiner = cfn;
    ks_setutil(kfn, cfn);
}

static void
bucket_free(hash_bucket_t *bucket)
{
    int i;

    for (i = 0; i < hash_slots; i++)
        if (bucket->entries[i].v)
            ks_delete(bucket->entries[i].v);
    free(bucket->entries);
    bucket->entries = 0;
    bucket->capacity = 0;
}

static int
yield_check_(int row, int total, int finished)
{
    int i;

    hash_bucket_t *bucket = &mbks[row][check_col];
    if (row == 0 && finished == 1 && last_keys < 0) {
        last_keys = bucket->ind_keys;
        last_occurs = bucket->occurs;
        return 0;
    }
    if (finished < 2)
        return 0;
    if (row == 0 && total * YIELD_CHECK_THRESH > finished && curmap_occurs < YIELD_CHECK_OCCUR_THRESH) {
        return 0;
    }
    if (row == 0 && state != checked) {
        if (bucket->ind_keys < map_rows) {
            state = checked;
            return 0;
        }
        if (state == uncheck) {
            for (i = 0; i < map_rows; i++) {
                int slot = rand() % hash_slots;
                while (!bucket->entries[slot].v)
                    slot = (slot + 1) % hash_slots;
                check_hashes[i] = slot;
                check_keys[i] = ks_rand_select(bucket->entries[slot].v);
            }
            for (i = 0; i < map_rows; i++)
                found[i] = -1;
            state = checking;
        }
        else if (state == checking) {
            // estimate the number of keys
            int estkeys = (total - finished) * (bucket->ind_keys - last_keys) + bucket->ind_keys;
            if (estkeys < YIELD_NON_HASH) {
                printf("less keys %d, cur %d, last %d, total %d, finished %d\n",
                       estkeys, bucket->ind_keys, last_keys, total, finished);
                state = checked;
                return 0;
            }
            int dups = 0;
            for (i = 1; i < map_rows; i++) {
                if (found[i] < 0)
                    return 0;
                dups += found[i];
            }
            int check_slot = check_hashes[row] % hash_slots;
            hash_entry_t *check_entry = &bucket->entries[check_slot];
            dups += ks_find(check_entry->v, check_keys[row]);
            printf("dup is %d, %d of %d finised, %d keys, %d occures, cur_bucket keys %d, estkeys %d\n",
                   dups, finished, total, curmap_keys, curmap_occurs, bucket->ind_keys, estkeys);
            int res = YIELD_NO;
            if (bucket->ind_keys > YIELD_MUST_KEYS_THRESH && bucket->ind_keys != bucket->occurs)
                res = YIELD_BALANCED;
            else if (dups < YIELD_DUP_THRESH)
                res = YIELD_APPEND;
            else
                res = YIELD_BALANCED;
            state = checked;
            return res;
        }
    }
    else if (state == checking && found[row] < 0) {
        int check_slot = check_hashes[row] % hash_slots;
        hash_entry_t *check_entry = &mbks[row][check_col].entries[check_slot];
        found[row] = ks_find(check_entry->v, check_keys[row]);
    }
    return 0;
}

static int
map_put_(int row, int col, unsigned hash, void *key, void *val)
{
    hash_bucket_t *bucket = &mbks[row][col];
    int slot = hash % hash_slots;
    hash_entry_t *hash_entry = &bucket->entries[slot];
    if (hash_entry->v) {
        int bnewkey = ks_insert(hash_entry->v, hash, key, val, 0);
        if (bnewkey) {
            bucket->ind_keys ++;
            curmap_keys ++;
        }
    }
    else {
        hash_entry->v = ks_create(key, val, 0);
        bucket->ind_keys ++;
        curmap_keys ++;
    }
    bucket->occurs ++;
    curmap_occurs ++;
    if (col != check_col)
        return 0;
    return 0;
}

static inline int
keyvals_cmp(const void *kvs1, const void *kvs2)
{
    return keycmp((*((keyvals_t **) kvs1))->key, (*((keyvals_t **) kvs2))->key);
}

static void
do_reduce_task_(int col, keyval_arr_t *rbucket, reduce_t reducer, int kra)
{
    int i, j;
    uint32_t k;

    if (!mbks)
        return;
    keyvals_t **kvs_arr = NULL;
    int kvs_len = 0;
    int valloclen = init_vals_len;
    void **vals = malloc(sizeof(void *) * valloclen);
    for (i = 0; i < hash_slots; i++) {
        int total_len = 0;
        for (j = 0; j < map_rows; j++)
            total_len += ks_getlen(mbks[j][col].entries[i].v);
        if (!total_len)
            continue;
        if (total_len > kvs_len) {
            if (kvs_len > 0)
                kvs_arr = (keyvals_t **) realloc(kvs_arr, (total_len + 1) * sizeof(keyvals_t *));
            else
                kvs_arr = (keyvals_t **) malloc((total_len + 1) * sizeof(keyvals_t *));
            kvs_len = total_len;
        }
        size_t pos = 0;
        for (j = 0; j < map_rows; j++) {
            if (!mbks[j][col].entries[i].v)
                continue;
            keyvals_arr_t *tmp_arr = ks_getinternal(mbks[j][col].entries[i].v);
            for (k = 0; k < tmp_arr->len; k++)
                kvs_arr[k + pos] = &tmp_arr->arr[k];
            pos += tmp_arr->len;
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
        qsort(kvs_arr, total_len, sizeof(keyvals_t *), keyvals_cmp);
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
//#define POS1
#define POS2
//#define POS3
        uint64_t ur_start = 0;
#if POS1
        if (profile_user_reduce)
            ur_start = read_tsc();
#endif
        if (map_rows == 1) {
            for (j = 0; j < total_len; j++) {
                if (reducer)
                    reducer(kvs_arr[j]->key, kvs_arr[j]->vals, kvs_arr[j]->len);
                else if (kra) {
                    kv_put_key_val(rbucket, kvs_arr[j]->key, (void *)kvs_arr[j]->vals);
		    kvs_arr[j]->vals = NULL;
                }
                else
                    for (k = 0; k < kvs_arr[j]->len; k++)
                        kv_put_key_val(rbucket, kvs_arr[j]->key, kvs_arr[j]->vals[k]);
            }
        }
        else {
            int start = 0;
            keyvals_t **kvs = kvs_arr;
            while (start < total_len) {
                int end = start + 1;
                while (end < total_len && !keycmp(kvs[start]->key, kvs[end]->key))
                    end ++;
                int vlen = 0;
                if (end - start > map_rows) {
                    printf("duplicated in one slot with %d, %p\n", end - start, kvs[start]->key);
                    exit(-1);
                }
                for (k = start; k < (uint32_t)end; k++)
                    vlen += kvs[k]->len;
                if (vlen > valloclen) {
                    while (vlen > valloclen)
                        valloclen *= 2;
                    vals = (void **)realloc(vals, sizeof(void *) * valloclen);
                }
#ifdef POS2
                if (profile_user_reduce)
                    ur_start = read_tsc();
#endif
                void **dest = vals;
                for (k = start; k < (uint32_t)end; k++) {
                    memcpy(dest, kvs[k]->vals, sizeof(void *) * kvs[k]->len);
                    dest += kvs[k]->len;
                }
#ifdef POS2
                if (profile_user_reduce)
                    user_reduce += (read_tsc() - ur_start);
#endif
                if (reducer)
                    reducer(kvs[start]->key, vals, vlen);
                else if (kra) {
                    kv_put_key_val(rbucket, kvs[start]->key, (void *)vals);
                    valloclen = init_vals_len;
                    vals = (void **)malloc(sizeof(void *) * valloclen);
                }
                else
                    for (k = 0; k < (uint32_t)vlen; k++)
                        kv_put_key_val(rbucket, kvs[start]->key, vals[k]);
                start = end;
            }
#ifdef POS1
            if (profile_user_reduce)
                user_reduce += (read_tsc() - ur_start);
#endif
        }
    }
    for (i = 0; i < map_rows; i++)
        bucket_free(&mbks[i][col]);
    if (kvs_arr)
        free(kvs_arr);
    if (vals)
        free(vals);

}

static void
draw_(int row, int col, draw_callback_t cb)
{
    unsigned i, j;

    hash_bucket_t *bucket = &mbks[row][col];
    for (i = 0; i < (unsigned )hash_slots; i++)
        if (bucket->entries[i].v) {
            keyvals_arr_t *kvs = ks_getinternal(bucket->entries[i].v);
            for (j = 0; j < kvs->len; j++)
                cb(&kvs->arr[j], row, col);
        }
}

static void
prof_print_(void)
{

    if (profile_qsort) {
#ifdef JOS_USER
        printf("cpu %d:   Append: %lu(ms) %lu %lu %lu %lu\n",
               core_env->pid,
               qsort_time * 1000 / get_cpu_freq(),
               kv_reduce_cache.amd_cache.l1_miss_cnt,
               kv_reduce_cache.amd_cache.l2_miss_cnt,
               kv_reduce_cache.amd_cache.l3_miss_cnt,
               kv_reduce_cache.amd_cache.inst_ret);

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

const kvhandler_t openhbb = {
                                .kv_mbks_init = mbks_init_,
                                .kv_map_put = map_put_,
                                .kv_setutils = setutils_,
                                .kv_do_reduce_task = do_reduce_task_,
                                .kv_yield_check = yield_check_,
                                .kv_draw = draw_,
                                .kv_prof_print = prof_print_,
                            };

