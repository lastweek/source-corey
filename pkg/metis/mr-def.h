#ifndef MR_DEF_H
#define MR_DEF_H

/* common data structures exposed to users */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    void *data;
    size_t length;
} map_arg_t;

typedef struct {
    void *key;
    void *val;
} keyval_t;

typedef struct {
    keyval_t *data;
    size_t length;
} final_data_t;

typedef struct {
    unsigned len;
    unsigned alloc_len;
    keyval_t *arr;
} keyval_arr_t;

/* keep key in the same position with keyval_t */
typedef struct {
    int hash;
    void *key;
    void **vals;
    unsigned len;
    unsigned alloc_len;
} keyvals_t;

typedef struct {
    unsigned len;
    unsigned alloc_len;
    keyvals_t *arr;
} keyvals_arr_t;

typedef int (*splitter_t) (void *, unsigned, map_arg_t *);
typedef void (*map_t) (map_arg_t *);
typedef void (*reduce_t) (void *, void **, size_t);
typedef int (*combine_t) (void *, void **, size_t);
typedef unsigned (*partition_t) (void *, int);

typedef int (*key_cmp_t) (const void *, const void *);
typedef int (*out_cmp_t) (const keyval_t *, const keyval_t *);
typedef void (*tv_callback_t)(void *val, void *arg);
typedef void (*draw_callback_t)(keyvals_t *kvs, int row, int col);

#endif
