#include "kvhandler.h"
#include <string.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

enum { init_keyvals_cnt = 8 };
enum { init_vals_len = 10 };

static key_cmp_t JSHARED_ATTR keycmp = NULL;
static out_cmp_t JSHARED_ATTR outcmp = NULL;
static JTLS int reduce_reduced_pos = -1;

void
kv_setutils(key_cmp_t fn, out_cmp_t out_cmp)
{
    keycmp = fn;
    outcmp = out_cmp;
}

keyval_arr_t *
kv_rbks_init(int cols)
{
    return (keyval_arr_t *) calloc(cols, sizeof(keyval_arr_t));
}

static void 
reduce_sorted(keyval_arr_t * kv_arr, reduce_t reducer)
{
    uint32_t i;
 
    reduce_reduced_pos = 0;
    uint32_t start = 0;
    uint32_t alloc_len = init_vals_len;
    void **vals = (void *)malloc(sizeof(void *) * alloc_len);
    while (start < kv_arr->len) {
        uint32_t end;
        for (end = start + 1; end < kv_arr->len; end++)
	    if (keycmp(kv_arr->arr[start].key, kv_arr->arr[end].key))
	        break;
	if (end - start > alloc_len) {
	    while (end - start > alloc_len)
	        alloc_len *= 2;
	    vals = realloc(vals, alloc_len * sizeof(void *));
	}
	if ((int)end - 1 == reduce_reduced_pos) {
	    start = end;
	    reduce_reduced_pos ++;
	    continue;
	}
	for (i = start; i < end; i++)
	    vals[i - start] = kv_arr->arr[i].val;
	reducer(kv_arr->arr[start].key, vals, end - start);
	start = end;
    }
    kv_arr->len = reduce_reduced_pos;
    free(vals);
    reduce_reduced_pos = -1;
}

static void
reduce_put_reduced(keyval_arr_t *kv_arr, void *key, void *val)
{
    kv_arr->arr[reduce_reduced_pos].key = key;
    kv_arr->arr[reduce_reduced_pos].val = val;
    reduce_reduced_pos ++;
}

#define OPTIMIZED_REDUCE

#ifdef OPTIMIZED_REDUCE
void
kv_put_key_val(keyval_arr_t * kv_arr, void *key, void *val)
{
    if (reduce_reduced_pos >= 0) {
        reduce_put_reduced(kv_arr, key, val);
        return;
    }
    if (kv_arr->len == kv_arr->alloc_len) {
	if (kv_arr->len == 0) {
	    kv_arr->alloc_len = init_keyvals_cnt;
	    kv_arr->arr = (keyval_t *) malloc(kv_arr->alloc_len * sizeof(keyval_t));
	} else {
	    kv_arr->alloc_len *= 2;
	    kv_arr->arr = (keyval_t *) realloc(kv_arr->arr, kv_arr->alloc_len * sizeof(keyval_t));
	}
    }
    kv_arr->arr[kv_arr->len].key = key;
    kv_arr->arr[kv_arr->len].val = val;
    kv_arr->len++;
}

static int
cmp_outcmp(const void *kv1, const void *kv2)
{
    return outcmp(((keyval_t *)kv1), ((keyval_t *)kv2));
}

static int
cmp_keyonly(const void *kv1, const void *kv2)
{
    return keycmp(((keyval_t *)kv1)->key, ((keyval_t *)kv2)->key);
}

typedef int (*qsortcmp)(const void *, const void *);

void 
kv_reduce(keyval_arr_t * kv_arr, reduce_t reducer, int nenabled)
{
    if (nenabled > 1) {
        qsort(kv_arr->arr, kv_arr->len, sizeof(keyval_t), cmp_keyonly);
        if (reducer)
            reduce_sorted(kv_arr, reducer);
        if (outcmp)
            qsort(kv_arr->arr, kv_arr->len, sizeof(keyval_t), cmp_outcmp);
    }
    else if (outcmp)
        qsort(kv_arr->arr, kv_arr->len, sizeof(keyval_t), (qsortcmp)outcmp);
    else
        qsort(kv_arr->arr, kv_arr->len, sizeof(keyval_t), cmp_keyonly);
}

#else

void
kv_put_key_val(keyval_arr_t * kv_arr, void *key, void *val)
{
    if (reduce_reduced_pos >= 0) {
        reduce_put_reduced(kv_arr, key, val);
        return;
    }
    /* if array is full */
    if (kv_arr->len == kv_arr->alloc_len) {
	if (kv_arr->len == 0) {
	    kv_arr->alloc_len = init_keyvals_cnt;
	    kv_arr->arr = (keyval_t *) malloc(kv_arr->alloc_len * sizeof(keyval_t));
	} else {
	    kv_arr->alloc_len *= 2;
	    kv_arr->arr = (keyval_t *) realloc(kv_arr->arr,
				     kv_arr->alloc_len * sizeof(keyval_t));
	}
    }
    /* do binary search */
    int start = -1, end = kv_arr->len, idx = end - 1;
    while ((end - start) > 1) {
	if (keycmp(key, kv_arr->arr[idx].key) < 0)
	    end = idx;
	else
	    start = idx;
	idx = (start + end) / 2;
    }
    /* need move key/val */
    if (idx < 0)
	idx = 0;
    if (idx < kv_arr->len)
	memmove(&kv_arr->arr[idx + 1], &kv_arr->arr[idx],
		(kv_arr->len - idx) * sizeof(keyval_t));
    kv_arr->arr[idx].key = key;
    kv_arr->arr[idx].val = val;
    kv_arr->len++;
}

void 
kv_reduce(keyval_arr_t * kv_arr, reduce_t reducer)
{
    reduce_sorted(kv_arr, reducer);
}
#endif

