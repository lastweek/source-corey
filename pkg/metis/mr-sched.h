#ifndef __MR_SCHED_H_
#define __MR_SCHED_H_

/* application interface. each map-reduce application should include this file.*/

#include "mr-def.h"
#include "mr-prof.h"

/* user configured map-reduce parameters  */
typedef struct {
    /* imperative parameters to map reducer */
    void *task_data;		/* input data */
    final_data_t *final_results;	/* output data, allocated by users. */
    size_t data_size;		/* total # of bytes of data */
    int unit_size;		/* # of bytes for one element (maybe, on average) */
    map_t map_func;		/* map func. must be user defined */
    key_cmp_t key_cmp;		/* comparison func. must be user defined. */

    /* Optional arguments: must be zero (or NULL) if not used */
    partition_t part_func;	/* partition func. uses hash function */
    reduce_t reduce_func;	/* reduce func. emits pair for each val. */
    combine_t local_reduce_func;	/* local reduce func */
    splitter_t split_func;	/* splitter func. */

    size_t split_size;		/* # of unit for a splite block */
    int nr_cpus;		/* # of cpus to use */

    int hash_slots;		/* # of hbb slots per hbb entry */
    /* optimization arguments */
    int presplit_map_tasks;	/* #map tasks before splitting. should be zero when not splitting input in advance. */
    int reduce_tasks;
    out_cmp_t out_cmp;
    int keep_reduce_array;
} mr_param_t;

/*
 * public functions for use by applications.
 */
extern void print_phase_time(void);
extern int mr_run_scheduler(mr_param_t * param);
extern void emit(void *key, void *val);
extern void emit_intermediate(void *key, void *val, int key_size);
extern void finit_mr(void);

#endif
