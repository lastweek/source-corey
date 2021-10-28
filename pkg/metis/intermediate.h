#ifndef INTERMEDIATE_H
#define INTERMEDIATE_H

#include "mr-def.h"

int mbks_init(int rows, int cols, int nslots);
void map_put(int row, int col, unsigned hash, void *key, void * val);
void setutils(key_cmp_t fn, combine_t combiner, out_cmp_t outcmp);

/* reduce data for map buckets, and put them into reduce buckets */
void do_reduce_task(int col, keyval_arr_t *rbks, reduce_t reducer, int kra);

/* intermediate for reduce phase */
keyval_arr_t * rbks_init(int cols);
void put_key_val(keyval_arr_t * kv_arr, void *key, void *val);

void set_progress(int total, int finished);
void im_prof_print(void);
#endif
