#ifndef __MR_CONFIG_H_
#define __MR_CONFIG_H_

#ifdef __WIN__
#define  __attribute__(x) /*NOTHING*/
#endif
/* NOTICE: please include this file:
 * *before* any user-defined include files and
 * *after* any system include files.
 */
/*
 * machine configurations
 */
enum { mr_nr_cpus = 16 };
enum { mr_l2_cache_size = 1024 * 1024 * 1 };
enum { mr_l1_cache_size = 64 * 1024 };
enum { default_reduce_tasks = 256 };

/* transforming logical CPU id to physical CPU id  */
static const int intl_pcpus[] = {
    0, 8, 4, 12,
    1, 9, 5, 13,
    2, 10, 6, 14,
    3, 11, 7, 15,
};
#if 1
static const int amd_pcpus[] = {
    0, 1, 2, 3,
    4, 5, 6, 7,
    8, 9, 10, 11,
    12, 13, 14, 15,
};
#else
static const int amd_pcpus[] = {
    0, 4, 8, 12,
    1, 5, 9, 13,
    2, 6, 10, 14,
    3, 7, 11, 15,
};
#endif
/* debugging configuration. */

#ifndef inline
#define __inline inline
#endif

enum { dg_general = 0 };
enum { dg_thread = 0 };

#ifdef __WIN__
#define mr_debug(flag, x, ...)                	\
        do {                                  	\
            if (flag){                 		\
                printf(x, __VA_ARGS__);         \
            }                                   \
        }while(0)

#define dprintf(x, ...) mr_debug(dg_general, x, __VA_ARGS__)
#define tprintf(x, ...) mr_debug(dg_thread, x, __VA_ARGS__)

#else
#define mr_debug(flag, x...)                	\
        do {                                    \
            if (flag){                 		\
                printf(x);                      \
            }                                   \
        }while(0)

#define dprintf(x...) mr_debug(dg_general, x)
#define tprintf(x...) mr_debug(dg_thread, x)
#endif


#define mr_assert(expr)                         	\
    do{                                         	\
        int __r = (expr);                       	\
        if (!__r){                                  	\
            fprintf(stderr, "assert failed %s", #expr); \
            return -1;                                  \
        }                                               \
    }while (0)

#define echeck(expr)					\
    do {                                  		\
	int  __r = (expr);				\
	if (__r < 0){                         		\
        perror("Error at line "#expr);    		\
        exit(1);                           		\
    }                                      		\
    }while(0)

#define echeck_ret(expr)					\
    do {                                      			\
        int __r = (expr);					\
        if (__r < 0) {                                    	\
            dprintf("Error (%d) at line %s: \n", __r, #expr);   \
            return __r;                                         \
        }                                                       \
    }while (0)

#define time_start(t) gettimeofday(&(t),0)
#define time_end(t,res)                         \
    do {                                        \
        struct timeval cur;                     \
        gettimeofday(&(cur),0);                 \
        timersub(&(cur),&(t),&(res));           \
        gettimeofday(&(t),0);                   \
     }while(0)

#ifdef JOS_USER
// All times are in microseconds (us)
struct bench_results {
    unsigned int core;
    unsigned int run;
    struct {
	unsigned int map;
	unsigned int reduce;
	unsigned int merge;
	unsigned int tot;
	unsigned int tot_over;
	unsigned int cmd_lat;
    } times[JOS_NCPU];
};

extern struct bench_results josmp_results;
extern uint64_t core_cmd_lat;

#endif

#endif
