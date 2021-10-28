#include "intermediate.h"
#include "kvhandler.h"
#include <assert.h>
#include "bench.h"
#ifdef JOS_USER
#include "mr-config.h"
#include <inc/compiler.h>
#include <inc/sysprof.h>
#include <inc/lib.h>
#else
#include "pmc.h"
#endif

const kvhandler_t *handlers[] = {
    &openhbb,
    &append,
    &btreekv,
    //&rbt2,
    //&closehbb,
};

enum { index_openhbb = 0, index_append, index_btree, index_closehbb, index_rbt2 };
enum { def_ihandler = index_openhbb };

static int JSHARED_ATTR enabled[sizeof(handlers) / sizeof(handlers[0])];
static int JSHARED_ATTR ihandler = def_ihandler;
static int ** JSHARED_ATTR ihs = NULL;
static JTLS int total = 0;
static JTLS int finished = 0;

enum { profile_kv_reduce = 0 };

static JTLS uint64_t kv_reduce_time = 0;
#ifdef JOS_USER
static JTLS struct sysprof_arg kv_reduce_cache;
#else
static JTLS uint64_t kv_reduce_l1 = 0;
static JTLS uint64_t kv_reduce_l2 = 0;
static JTLS uint64_t kv_reduce_l3 = 0;
static JTLS uint64_t kv_reduce_inst = 0;
#endif

#define NO_SWITCH

#define IHANDLER(row, col) ihs[row][col]

int 
mbks_init(int rows, int cols, int nslots)
{
    int i, j;

    ihs = (int **)malloc(rows * sizeof(int *));
    for (i = 0; i < rows; i++) {
        ihs[i] = (int *)malloc(cols * sizeof(int));
	for (j = 0; j < cols; j++)
	    IHANDLER(i, j) = def_ihandler;
    }
    enabled[def_ihandler] = 1;
    for (i = 0; (unsigned )i < sizeof(handlers) / sizeof(handlers[0]); i++)
        handlers[i]->kv_mbks_init(rows, cols, nslots);
    return 0;
}

void
set_progress(int total_, int finished_)
{
    total = total_;
    finished = finished_;
}

static void
map_put_kvs(keyvals_t *kvs, int row, int col)
{
    handlers[IHANDLER(row, col)]->kv_map_put_kvs(row, col, kvs);
}

void 
map_put(int row, int col, unsigned hash, void *key, void * val)
{
    handlers[IHANDLER(row, col)]->kv_map_put(row, col, hash, key, val);
#ifdef NO_SWITCH
    return;
#endif
    if (ihandler != IHANDLER(row, col)) {
        int old = IHANDLER(row, col);
        IHANDLER(row, col) = ihandler;
        if (ihandler != index_append)
	    handlers[old]->kv_draw(row, col, map_put_kvs);
    }
    if (!handlers[IHANDLER(row, col)]->kv_yield_check)
        return;
    int yield = handlers[IHANDLER(row, col)]->kv_yield_check(row, total, finished);
    switch (yield) {
        case YIELD_APPEND:
	    enabled[index_append] = 1;
	    ihandler = index_append;
	    break;
	case YIELD_BALANCED:
	    ihandler = index_btree;
	    enabled[index_btree] = 1;
	    break;
	default:
	    break;
    }
}

void 
setutils(key_cmp_t fn, combine_t combiner, out_cmp_t out_cmp)
{
    uint32_t i;

    for (i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
        handlers[i]->kv_setutils(fn, combiner);
    kv_setutils(fn, out_cmp);
}

/* reduce data for map buckets, and put them into reduce buckets */
void 
do_reduce_task(int col, keyval_arr_t *rbks, reduce_t reducer, int kra)
{
#ifndef NO_SWITCH
    if (ihandler == index_append)
	handlers[index_openhbb]->kv_do_reduce_task(col, &rbks[col], reducer, kra);
#endif
    handlers[ihandler]->kv_do_reduce_task(col, &rbks[col], reducer, kra);
    // This can be optimized when the data structure has never switched
    uint64_t s = 0;
#ifdef JOS_USER
    //struct sysprof_arg sarg;
#else
    uint64_t sl1, sl2, sl3, sinst;
#endif
    if (profile_kv_reduce) {
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
    kv_reduce(&rbks[col], reducer, 1);
    if (profile_kv_reduce) {
        kv_reduce_time += (read_tsc() - s);
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
}

void 
put_key_val(keyval_arr_t * kv_arr, void *key, void *val)
{
    kv_put_key_val(kv_arr, key, val);
}

keyval_arr_t *
rbks_init(int cols)
{
    return kv_rbks_init(cols);
}

void
im_prof_print(void)
{
    if (handlers[ihandler]->kv_prof_print)
	handlers[ihandler]->kv_prof_print();
    if (profile_kv_reduce) {
#ifdef JOS_USER
        printf("cpu %d: kv_reduce: %lu(ms), %lu, %lu, %lu, %lu\n", 
	        core_env->pid,
	        kv_reduce_time * 1000 / get_cpu_freq(), 
	        kv_reduce_cache.amd_cache.l1_miss_cnt,
	        kv_reduce_cache.amd_cache.l2_miss_cnt,
	        kv_reduce_cache.amd_cache.l3_miss_cnt,
	        kv_reduce_cache.amd_cache.inst_ret);
#else
        printf("kv_reduce: %14lu(ms), %14lu, %14lu, %14lu, %14lu\n", 
	        kv_reduce_time * 1000 / get_cpu_freq(), 
	        kv_reduce_l1, kv_reduce_l2, kv_reduce_l3, kv_reduce_inst);
#endif
    }
}
