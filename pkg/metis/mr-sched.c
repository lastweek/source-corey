#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifndef __WIN__
#include <pthread.h>
#include <sys/unistd.h>
#include <unistd.h>
#include <strings.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/resource.h>
#else
#include <time.h>
#include "mr-common.h"
#endif

#include "mr-config.h"
#include "mr-sched.h"
#include "intermediate.h"
#include "bench.h"

enum { profile_instrument = 0 };
enum { profile_merge = 0 };
enum { map_prefault = 0 };
enum { reduce_prefault = 0 };
enum { profile_memusage = 0 };

enum { print_map_time = 0 };
enum { print_phase_times = 0 };

#ifdef JOS_USER
#include <inc/compiler.h>
#include "mr-thread.h"
#include <inc/locality.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/sysprof.h>
#include "mr-prof.h"

struct bench_results josmp_results;
uint64_t core_cmd_lat;

#else
#include "pmc.h"
#define USE_PMC
#include "ummap.h"
#include <sys/resource.h>

#define RUSAGE_START()			\
    struct rusage rs;			\
    getrusage(RUSAGE_SELF, &rs);

#define RUSAGE_END()			\
    struct rusage re;			\
    getrusage(RUSAGE_SELF, &re);	\
    printf("min fault: %ld, maj fault %ld\n", re.ru_minflt - rs.ru_minflt, re.ru_majflt - rs.ru_majflt);

#endif
/* internal worker management. */
typedef enum {
    MAP_PREFAULT,
    MAP,
    REDUCE_PREFAULT,
    REDUCE,
    MERGE,
    MR_PHASES,
} task_type_t;

static void *map_prefault_worker(void *);
static void *map_worker(void *);
static void *reduce_prefault_worker(void *);
static void *reduce_worker(void *);
static void *merge_worker(void *);

typedef void *(*worker_t) (void *);
static worker_t worker_pool[MR_PHASES] = {
    map_prefault_worker,
    map_worker,
    reduce_prefault_worker,
    reduce_worker,
    merge_worker,
};

enum { phase_timing = 1 };
enum { dram = 0, nb, l3miss, inst, l2miss, tsc, tsc_insert, insert_cnt, pmccnt};

static int * JSHARED_ATTR lcpu_to_pcpu = (int *) amd_pcpus;
static int JSHARED_ATTR main_pcpu;

#ifdef USE_PMC
static uint64_t JSHARED_ATTR pmcs[MR_PHASES][mr_nr_cpus][pmccnt];
static int JSHARED_ATTR pmc_inited[mr_nr_cpus];
static JTLS uint64_t ltsc_insert = 0;
static JTLS uint64_t linsert_cnt = 0;
#endif

typedef struct {
#ifndef __WIN__
    pthread_t tid;
#else
    HANDLE tid;
#endif
    int cur_task;		/* for use in reduce phase. */
} thread_info_t;

typedef struct {
    int *task_idx;		/* shared among each mr thread. */
} th_arg_t;

#ifndef __WIN__
static JTLS int cur_reduce_task;
/* the logical cpu current thread running. can only be used after mr_thread_init_pool*/
JTLS int cur_lcpu = 0;
JTLS int cur_row = 0;
#else
static __declspec(thread) int cur_reduce_task;
/* the logical cpu current thread running. can only be used after mr_thread_init_pool*/
static __declspec(thread) int cur_lcpu = 0;
#endif

typedef struct {
    mr_param_t mr_fixed;

    int nr_merge_cpus;
    int merge_tasks;
    size_t split_pos;
    map_arg_t *map_task_arr;	/* pre-splitted array stored here. */
    int lock_free_sched;	/* local-free scheduling? */
    keyval_arr_t *reduce_results;	/* store reduce results. also store merge results. */
#ifndef __WIN__
    pthread_mutex_t split_mu;
#else
    HANDLE split_mu;
#endif
} mr_state_t;

static mr_state_t JSHARED_ATTR mr_state;
#ifdef PROF_ENABLED
static uint64_t JSHARED_ATTR app_tot[16];

JTLS uint64_t app_tsc_start = 0;
JTLS uint64_t app_tsc_tot = 0;

static void
prof_init()
{
    app_tsc_tot = 0;
}

static void
prof_end()
{
    app_tot[cur_lcpu] += app_tsc_tot;
}

static void
prof_print()
{
    uint64_t tot = 0;
    for (int i = 0; i < 16; i++) {
        printf("%d\t%ld ms\n", i, app_tot[i] * 1000 / get_cpu_freq());
        tot += app_tot[i];
    }
    printf("Average spent in application is %ld\n", tot * 1000 / (mr_state.mr_fixed.nr_cpus * get_cpu_freq()));
}
#else

#define prof_init()
#define prof_end()
#define prof_print()

#endif

#ifndef USE_PMC

#define PMC_START(cid)
#define PMC_END(phase, cid)
#define print_pmc()

#else

/***************************************************
 * START performance counter related routines START*
 ***************************************************/
#define PMC_START(cid)						\
	if (!pmc_inited[cid]) {					\
	    pmc_inited[cid] = 1;				\
	    pmc_init(cid);					\
	}							\
	uint64_t nb_start = pmc_refills_nb();			\
	uint64_t tsc_start = read_tsc();			\
	uint64_t l2miss_start = pmc_l2_miss();			\
	uint64_t dram_start = pmc_dram();			\
	uint64_t inst_start = pmc_ret_ins();			\
	ltsc_insert = 0;					\
	linsert_cnt = 0;

#define PMC_END(phase, cid)					\
	pmcs[phase][cid][nb] = pmc_refills_nb() - nb_start;	\
	pmcs[phase][cid][tsc] = read_tsc() - tsc_start;		\
	pmcs[phase][cid][l2miss] = pmc_l2_miss() - l2miss_start;\
	pmcs[phase][cid][dram] = pmc_dram() - dram_start;	\
	pmcs[phase][cid][inst] = pmc_ret_ins() - inst_start;	\
	pmcs[phase][cid][tsc_insert] = ltsc_insert;		\
	pmcs[phase][cid][insert_cnt] = linsert_cnt;

static void
print_phase(int phase)
{
    for (int i = 0; i < mr_nr_cpus; i++) {
        printf("%d\t%ld\t%ld\t%ld\t", i, pmcs[phase][i][dram] / 1000,
        pmcs[phase][i][nb] / 1000,
	pmcs[phase][i][l2miss] / 1000);

        uint64_t s = pmcs[phase][i][tsc] * 1000000/get_cpu_freq();
	if (s == 0)
	    s = 1;
        printf("%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%4.2f\n", 1000 * pmcs[phase][i][dram] / s,
        pmcs[phase][i][nb] * 1000/ s,
	pmcs[phase][i][l2miss] * 1000 / s,
	pmcs[phase][i][inst] / 1000,
	pmcs[phase][i][tsc_insert] * 1000000/get_cpu_freq(),
	pmcs[phase][i][insert_cnt] / 1000,
	s, pmcs[phase][i][inst] / (double)pmcs[phase][i][tsc]);
    }
}

static void
print_pmc()
{
    printf("MAP\n");
    printf("cpu\tdram\tnb\tl2ms\t");
    printf("dram/ms\tnb/ms\tl2ms/ms\tinst\tins,us\tinss,k\ttot,us\tIPC\n");
    print_phase(MAP);
    printf("REDUCE\n");
    printf("cpu\tdram\tnb\tl2ms\t");
    printf("dram/ms\tnb/ms\tl2ms/ms\tinst\tins,us\tinss,k\ttot,us\tIPC\n");
    print_phase(REDUCE);
    printf("MERGE\n");
    printf("cpu\tdram\tnb\tl2ms\t");
    printf("dram/ms\tnb/ms\tl2ms/ms\tinst\tins,us\tinss,k\ttot,us\tIPC\n");
    print_phase(MERGE);
}
#endif

/***************************************************
 * END performance counter related routines END   *
 ***************************************************/
#ifndef JOS_USER
/******************************************
 *START   thread related functions   START*
 *****************************************/

/* not sure if we need mb(). read intel memory ordering docs. */
#ifndef __WIN__
#define barrier() __asm__ __volatile__("mfence": : :"memory")
#define pause() __asm__("pause")
#else
#define pause() __asm pause
#define barrier() __asm volatile ("mfence": : :"memory")
#endif

/* passing thread information among processors. indexed using pysical cpu id. */
typedef struct {
    void *arg;
    void *(*start_routine) (void *);
    volatile char ready;
#ifndef __WIN__
    pthread_t real_tid;
#else
    HANDLE real_tid;
#endif
    volatile char running;
} __attribute__ ((aligned(JOS_CLINE))) thread_pool_t;

thread_pool_t JSHARED_ATTR thread_pool[mr_nr_cpus];

static int JSHARED_ATTR mr_procs_init = 0;

#ifndef __WIN__
static inline int
mr_thread_create(int pcpu, void *(*start_routine) (void *), void *arg)
{
#else
static inline int
mr_thread_create(int pcpu, void *(*start_routine) (void *), void *arg)
{
#endif
    assert(mr_procs_init != 0);

    if (pcpu == main_pcpu) {
        tprintf("running  thread (%p %p) on pcpu %d \n",
		thread_pool[pcpu].start_routine, thread_pool[pcpu].arg, pcpu);
	start_routine(arg);
    } else {
	while (thread_pool[pcpu].running)
	    pause();
	thread_pool[pcpu].arg = arg;
	thread_pool[pcpu].start_routine = start_routine;
	thread_pool[pcpu].ready = 1;

	while (thread_pool[pcpu].ready)
	    pause();
	tprintf("creating thread (%p %p) on pcpu %d \n",
		thread_pool[pcpu].start_routine, thread_pool[pcpu].arg, pcpu);
    }
    return 0;
}

static inline int
mr_thread_join(int pcpu)
{
    while (thread_pool[pcpu].running)
        pause();
    tprintf("joined thread on cpu %d\n", pcpu);
    return 0;
}

#ifndef __WIN__
static void * __attribute__ ((noreturn))
mr_thread_dispatcher(void *args)
{
#else
DWORD WINAPI
mr_thread_dispatcher(LPVOID args)
{
#endif
    int lcpu = (int) (size_t) args;
    int pcpu = lcpu_to_pcpu[lcpu];
#ifndef __WIN__
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(pcpu, &cpu_set);
    echeck(sched_setaffinity(0, sizeof(cpu_set), &cpu_set));
#else
    DWORD_PTR dwThreadAffinityMask = 0;
    dwThreadAffinityMask |= 1 << pcpu;
#ifdef SET_AFFINITY
    SetThreadAffinityMask(GetCurrentThread(), dwThreadAffinityMask);
#endif
#endif
    cur_lcpu = lcpu;		/* record the logical cpu id in thread local area for future uses. */
    for (;;) {
        while (!(thread_pool[pcpu].ready))
            pause();
        thread_pool[pcpu].running = 1;
        thread_pool[pcpu].ready = 0;

        thread_pool[pcpu].start_routine(thread_pool[pcpu].arg);
        thread_pool[pcpu].running = 0;
    }
}

static inline void
mr_init_threadpool(int nr_procs)
{
    if (mr_procs_init)
	return;
    cur_lcpu = 0;

#ifndef __WIN__
    pthread_attr_t attr;
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    main_pcpu = lcpu_to_pcpu[cur_lcpu];	/*use cpu nr_procs - 1 for main pcpu */
    CPU_SET(main_pcpu, &cpu_set);
    echeck(sched_setaffinity(0, sizeof(cpu_set), &cpu_set));
    /* init thread attribute. thread must be scheduled systemwide */
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#else
    main_pcpu = lcpu_to_pcpu[cur_lcpu];	/*use cpu nr_procs - 1 for main pcpu */
    DWORD_PTR dwThreadAffinityMask = 1 << main_pcpu;
#ifdef SET_AFFINITY
    DWORD_PTR ret =
	SetThreadAffinityMask(GetCurrentThread(), dwThreadAffinityMask);
    if (!ret)
	printf("set affinity in mr_init_threadpool fail!\n");
#endif
#endif

    mr_procs_init = 1;
    memset(&thread_pool, 0, sizeof(thread_pool));

    for (int i = 0; i < nr_procs && i < mr_nr_cpus; i++) {
	int pcpu = lcpu_to_pcpu[i];
	if (pcpu == main_pcpu) {
#ifndef __WIN__
	    thread_pool[pcpu].real_tid = pthread_self();
#else
	    thread_pool[pcpu].real_tid = GetCurrentThread();
#endif
	    continue;
	}
#ifndef __WIN__
	echeck(pthread_create
	       ((pthread_t *) & thread_pool[pcpu].real_tid, &attr,
		mr_thread_dispatcher, (void *) (size_t) i) != 0);
#else
	thread_pool[pcpu].real_tid =
	    CreateThread(NULL, 0, mr_thread_dispatcher, (void *) (size_t) i,
			 0, NULL);
#endif
    }

#ifndef __WIN__
    pthread_attr_destroy(&attr);
#endif
}

static void *__attribute__((noreturn))
finalize_thread(void __attribute__ ((unused)) * args)
{
#ifndef __WIN__
    pthread_exit(NULL);
#else
    ExitThread(NULL);
#endif
}

static inline void
mr_finit_threadpool(int nr_procs)
{
    int pcpu;
    for (int i = 0; i < nr_procs; i++) {
	pcpu = lcpu_to_pcpu[i];
	if (pcpu == main_pcpu)
	    continue;
	echeck(mr_thread_create(pcpu, finalize_thread, NULL));
    }
    for (int i = 0; i < nr_procs; i++) {
	pcpu = lcpu_to_pcpu[i];

	if (pcpu != main_pcpu)
#ifndef __WIN__
	    echeck(pthread_join(thread_pool[pcpu].real_tid, NULL));
#else
	    WaitForSingleObject(thread_pool[pcpu].real_tid, INFINITE);
#endif
    }
    memset(&thread_pool, 0, sizeof(thread_pool));
    mr_procs_init = 0;
}

/******************************************
 *END    thread related functions   END****
 *****************************************/
#endif
/* supporting functions */

/* use the djb2 hash function */
static unsigned
default_hasher(void *key, int key_size)
{
    size_t hash = 5381;
    char *str = (char *) key;
    int i;

    for (i = 0; i < key_size; i++)
	hash = ((hash << 5) + hash) + ((unsigned) str[i]);
    return hash % ((unsigned) (-1));
}

/* treate the data as an array with element size unit_size
 * thread-safe, should be protected by split_mu.
 */
static int
default_splitter(void *data, unsigned nr_units, map_arg_t * ma)
{
    size_t split_pos = mr_state.split_pos;
    char *mr_data = (char *) data;
    int unit_size = mr_state.mr_fixed.unit_size;
    size_t data_size = mr_state.mr_fixed.data_size;
    /* no more data to split */
    if (split_pos >= data_size)
	return 0;
    ma->data = (void *) &mr_data[split_pos];
    /* if not enough data */
    if ((split_pos + unit_size * nr_units) > data_size)
	ma->length = (data_size - split_pos) / unit_size;
    else
	ma->length = nr_units;
    mr_state.split_pos += nr_units * unit_size;
    return 1;
}

static inline int
    get_nr_cpus(void)
 {
#ifdef JOS_USER
     struct u_locality_matrix ulm;
     sys_locality_get(&ulm);
     return ulm.ncpu;
#else
     static int nr_cpus = 0;
     if (nr_cpus == 0) {
         /* Returns number of processors available to process (based on affinity mask) */
#ifndef __WIN__
         nr_cpus = 16;
         return nr_cpus;
#else
         HANDLE curProcess = GetCurrentProcess();
         DWORD_PTR processAffinityMask;
         DWORD_PTR systemAffinityMask;

         if (!
             (GetProcessAffinityMask
              (curProcess, &processAffinityMask, &systemAffinityMask))) {
             printf("error when getting affinity mask\n");
             return -1;
         }

         for (int i = 0; i < mr_nr_cpus; i++) {
             if (processAffinityMask & (1 << i)) {
                 nr_cpus++;
             }
         }
#endif
     }
     return nr_cpus;
#endif
 }

/*********************************************************
 *START map-reduce scheduler related functions      START*
 ********************************************************/
 static void *
     map_worker(void *arg)
 {
#ifdef JOS_USER
     int i;

     cur_lcpu = core_env->pid;
     for (i = 0; i < JOS_NCPU; i++) {
         if ((unsigned int)lcpu_to_pcpu[i] == core_env->pid) {
             cur_row = i;
             break;
         }
     }
     uint64_t jstart = read_tsc();
     dprintf("map_worker: cpu %d cur_row %d\n",
             cur_lcpu, cur_row);
#else
     RUSAGE_START();
#endif
     memusage_t usage;
     if (profile_memusage)
         ummap_init_usage(&usage);
     PMC_START(cur_lcpu);
     prof_init();
     int *task_idx = (int *) arg;
     map_t map_func = mr_state.mr_fixed.map_func;
     int num_tasks = 0;
     int total = mr_state.mr_fixed.presplit_map_tasks / mr_state.mr_fixed.nr_cpus;
     set_progress(total, 0);
     if (mr_state.lock_free_sched) {
         int total_map_tasks = mr_state.mr_fixed.presplit_map_tasks;
         map_arg_t *tsk_arr = mr_state.map_task_arr;
         while (1) {
             int cur_task = atomic_add32_ret(task_idx);
             if (cur_task >= total_map_tasks)
                 break;
             map_func(&tsk_arr[cur_task]);
             num_tasks++;
             set_progress(total, num_tasks);
         }
     } else {
#if 1
         printf("only support lock free scheduling\n");
#else
         splitter_t split_func = mr_state.mr_fixed.split_func;
         void *task_data = mr_state.mr_fixed.task_data;
         size_t split_size = mr_state.mr_fixed.split_size;

         while (1) {
             map_arg_t ma;
             int ret;
#ifndef __WIN__
             pthread_mutex_lock(&mr_state.split_mu);
             ret = split_func(task_data, split_size, &ma);
             pthread_mutex_unlock(&mr_state.split_mu);
#else
             DWORD dwWaitResult =
                 WaitForSingleObject(mr_state.split_mu, INFINITE);
             switch (dwWaitResult) {
             case WAIT_OBJECT_0:
                 __try {
                     ret = split_func(task_data, split_size, &ma);
                 }

                 __finally {
                     if (!ReleaseMutex(mr_state.split_mu)) {
                         printf("release mutex error!\n");
                         return;
                     }
                 }
                 break;

             case WAIT_ABANDONED:
                 return;
             }
#endif
             if (ret == 0)
                 break;
             map_func(&ma);
             num_tasks++;
             set_progress(total, num_tasks);
         }
#endif
     }
#ifndef __WIN__
     dprintf("total %d map tasks executed in cpu %d\n",
             num_tasks, cur_lcpu);
#else
     dprintf("total %d map tasks executed in thread %ld(%d)\n",
             num_tasks, reinterpret_cast < int >(GetCurrentThread()),
             cur_lcpu);
#endif
     prof_end();
     PMC_END(MAP, cur_lcpu);
     if (profile_memusage)
         ummap_print_usage(&usage);
#ifdef JOS_USER
     if (print_map_time) {
         uint64_t jtime = read_tsc() - jstart;
         printf("cpu %d:time of is %d\n", core_env->pid, jtime * 1000 / get_cpu_freq());
     }
#else
     RUSAGE_END();
#endif
     if (profile_instrument)
         im_prof_print();

     return 0;
 }

static void *
map_prefault_worker(void *arg)
{
#ifdef JOS_USER
    cur_lcpu = core_env->pid;
#endif
#ifndef JOS_USER
    RUSAGE_START();
#endif
    ummap_prefault(400 * 1024 * 1024);
#ifndef JOS_USER
    RUSAGE_END();
#endif
    return 0;
}

static void *
reduce_prefault_worker(void *arg)
{
    uint64_t segsize = 3300;
#ifdef JOS_USER
    cur_lcpu = core_env->pid;
#endif
    if (cur_lcpu == main_pcpu)
	segsize = 4600;
    segsize *= (1024 * 1024);
    return (void *)ummap_prefault(segsize);
}

static void *
reduce_worker(void *arg)
{
#ifdef JOS_USER
    cur_lcpu = core_env->pid;

    uint64_t cmdcnt0 = 0, cmdcnt1 = 0;
    uint64_t latcnt0 = 0, latcnt1 = 0;

    if (core_env->pid == 0) {
	cmdcnt0 = sysprof_rdpmc(0);
	latcnt0 = sysprof_rdpmc(1);
    }
#endif

    memusage_t usage;
    if (profile_memusage)
	ummap_init_usage(&usage);
    uint64_t s = read_tsc();
    PMC_START(cur_lcpu);
    prof_init();
    int *task_idx = (int *) arg;
    int num_tasks = 0;
    while (1) {
	int cur_task = atomic_add32_ret(task_idx);
	if (cur_task >= mr_state.mr_fixed.reduce_tasks)
	    break;
	cur_reduce_task = cur_task;
	do_reduce_task(cur_task, mr_state.reduce_results,
		       mr_state.mr_fixed.reduce_func, mr_state.mr_fixed.keep_reduce_array);
	num_tasks++;
	dprintf("thread : %d, num of tasks : %d\n",cur_lcpu,cur_task);
    }
#ifndef __WIN__
    dprintf("total %d reduce tasks executed in cpu %d\n",
	    num_tasks, cur_lcpu);
#else
    dprintf("total %d reduce tasks executed in thread %ld(%d)\n",
	    num_tasks, (int) (GetCurrentThread()), cur_lcpu);
#endif
    prof_end();
    PMC_END(REDUCE, cur_lcpu);
#ifdef JOS_USER
    if (core_env->pid == 0) {
	cmdcnt1 = sysprof_rdpmc(0);
	latcnt1 = sysprof_rdpmc(1);
    if (cmdcnt1 - cmdcnt0)
        core_cmd_lat = (latcnt1 - latcnt0) / (cmdcnt1 - cmdcnt0);
    }
#endif

    if (profile_memusage)
	ummap_print_usage(&usage);
    if (profile_instrument)
        im_prof_print();
    uint64_t t = read_tsc() - s;
    if (profile_instrument)
        printf("reduce worker %d, using %ld ms\n", cur_lcpu, t * 1000 / get_cpu_freq());
    //print_pmc();

    return 0;
}

static void *
merge_worker(void * __attribute__ ((unused)) arg)
{
#ifdef JOS_USER
    //cur_lcpu = core_env->pid;
    cur_lcpu = cur_row;
#endif
    memusage_t usage;
    int i;

    if (profile_memusage)
	ummap_init_usage(&usage);
    uint64_t s = read_tsc();
    PMC_START(cur_lcpu);
    prof_init();
    int lcpu = cur_lcpu;
    keyval_arr_t *kv_arr = mr_state.reduce_results;
    int nr_merge_cpus = mr_state.nr_merge_cpus;
    key_cmp_t key_cmp = mr_state.mr_fixed.key_cmp;

    int nr_tasks = mr_state.merge_tasks / nr_merge_cpus;
    int remainder = mr_state.merge_tasks % nr_merge_cpus;
    size_t nr_keyvals = 0;
    keyval_t *min_keyval = NULL;

    nr_tasks += (lcpu < remainder);
    /*
     * Merge nr_tasks keyval arrays into one array.
     * The keyval array are accessed in num_merge_cpus patterns.
     */

    /* get the total numer of the key/val pairs */
    for (i = 0; i < nr_tasks; i++)
	nr_keyvals += kv_arr[lcpu + i * nr_merge_cpus].len;
    if (nr_keyvals) {
	keyval_t *res_arr =
	    (keyval_t *) malloc(nr_keyvals * sizeof(keyval_t));
	uint32_t *task_pos = (uint32_t *) calloc(nr_tasks, sizeof(uint32_t));
	size_t nr_sorted = 0;
	while (nr_sorted < nr_keyvals) {
	    int min_idx = 0;
	    for (i = 0; i < nr_tasks; i++) {
		keyval_arr_t *cur_arr = &kv_arr[lcpu + i * nr_merge_cpus];
		if (task_pos[i] == cur_arr->len)
		    continue;
		if (!mr_state.mr_fixed.out_cmp) {
		    if (min_keyval == NULL
		        || key_cmp(min_keyval->key, cur_arr->arr[task_pos[i]].key) > 0) {
		        min_keyval = &cur_arr->arr[task_pos[i]];
		        min_idx = i;
		    }
		}
		else if (min_keyval == NULL
		         || mr_state.mr_fixed.out_cmp(min_keyval, &cur_arr->arr[task_pos[i]]) > 0) {
		        min_keyval = &cur_arr->arr[task_pos[i]];
		        min_idx = i;
	        }
	    }
	    res_arr[nr_sorted].key = min_keyval->key;
	    res_arr[nr_sorted].val = min_keyval->val;
	    task_pos[min_idx]++;
	    min_keyval = NULL;
	    nr_sorted++;
	}
	if (task_pos) {
	    free(task_pos);
	    task_pos = NULL;
	}
	if (kv_arr[lcpu].arr) {
	    free(kv_arr[lcpu].arr);
	    kv_arr[lcpu].arr = NULL;
	}
	kv_arr[lcpu].arr = res_arr;
	kv_arr[lcpu].len = nr_keyvals;
	kv_arr[lcpu].alloc_len = nr_keyvals;
	dprintf("merge_worker: cpu %d total_cpu %d (tasks %d : nr-kvs %ld)\n",
		lcpu, mr_state.nr_merge_cpus, mr_state.merge_tasks,
		nr_keyvals);
    }
    for (i = 1; i < nr_tasks; i++)
        free(kv_arr[lcpu + i * nr_merge_cpus].arr);
    prof_end();
    PMC_END(MERGE, cur_lcpu);
    if (profile_memusage)
	ummap_print_usage(&usage);
    uint64_t t = read_tsc() - s;
    if (profile_merge)
	printf("merge phase is %ld ms\n", t * 1000 / get_cpu_freq());
    return 0;
}

static inline void
serial_split_input(unsigned map_tasks)
{
     unsigned unit_size = mr_state.mr_fixed.unit_size;
     size_t data_size = mr_state.mr_fixed.data_size;
     unsigned units_per_task = data_size / (unit_size * map_tasks);
     assert(units_per_task);

     map_arg_t *map_task_arr =
         (map_arg_t *) malloc((map_tasks + 1) * sizeof(map_arg_t));
     uint32_t i;
     for (i = 0; i < map_tasks + 1; i++)
         if (mr_state.mr_fixed.split_func( mr_state.mr_fixed.task_data,
                                           units_per_task,
                                           &map_task_arr[i]) == 0)
             break;
     mr_state.mr_fixed.presplit_map_tasks = i;
     mr_state.map_task_arr = map_task_arr;
     mr_state.lock_free_sched = 1;
}

 static void dummy_print(void){
     static int i = 0;
     dprintf("dummy_print %d\n",i ++);
 }

/* setup map-reduce  */
static inline int
setup_mr(mr_param_t * param)
{
    dprintf("in setup_mr\n"); 
    assert(!param->final_results->data);
    assert(!param->final_results->length);
    memcpy(&mr_state.mr_fixed, param, sizeof(mr_state.mr_fixed));
    int nr_avail_cpus = get_nr_cpus();
    if (mr_state.mr_fixed.part_func == NULL)
        mr_state.mr_fixed.part_func = default_hasher;

    if (!mr_state.mr_fixed.nr_cpus || mr_state.mr_fixed.nr_cpus > nr_avail_cpus)
        mr_state.mr_fixed.nr_cpus = nr_avail_cpus;	/* use all available cpus. */
    assert(mr_state.mr_fixed.task_data);
    if (mr_state.mr_fixed.split_func == NULL)
        mr_state.mr_fixed.split_func = default_splitter;
    dprintf("in setup_mr 1\n"); 
    echeck_ret(mr_state.mr_fixed.final_results != NULL);
    echeck_ret(mr_state.mr_fixed.map_func != NULL);
    if (!mr_state.mr_fixed.reduce_tasks)
        mr_state.mr_fixed.reduce_tasks = default_reduce_tasks;

#ifndef JOS_USER
#ifndef __WIN__
    pthread_mutex_init(&mr_state.split_mu, NULL);
#else
    mr_state.split_mu = CreateMutex(NULL, false, NULL);
#endif
#endif
    /* do pre-split if user specifies # of split map tasks. */
    if (mr_state.mr_fixed.split_size <= 0)
        mr_state.mr_fixed.split_size = mr_l1_cache_size / param->unit_size;

    dprintf("beofre serial_split_input map_tasks %d\n",
            mr_state.mr_fixed.presplit_map_tasks);
    if (mr_state.mr_fixed.presplit_map_tasks > 0)
        serial_split_input(mr_state.mr_fixed.presplit_map_tasks);
    dprintf("after serial_split_input map_tasks %d\n",
            mr_state.mr_fixed.presplit_map_tasks);
#ifdef JOS_USER
    dprintf("calling mr_init_processors\n");
    dummy_print();
    mr_init_processors(PFORK_SHARE_HEAP);
    main_pcpu = core_env->pid;
#else
    mr_init_threadpool(mr_state.mr_fixed.nr_cpus);
#endif
    setutils(mr_state.mr_fixed.key_cmp, mr_state.mr_fixed.local_reduce_func, mr_state.mr_fixed.out_cmp);
    /* init buckets for map phase. */
    assert(!mbks_init(mr_state.mr_fixed.nr_cpus, mr_state.mr_fixed.reduce_tasks,
                      mr_state.mr_fixed.hash_slots));
    /* init buckets for reduce phase*/
    mr_state.reduce_results = rbks_init(mr_state.mr_fixed.reduce_tasks);

    return 0;
}

static inline int
run_mr_task(task_type_t type)
{
    int i;
    int nr_cpus = mr_state.mr_fixed.nr_cpus;
    int *task_pos = (int *) calloc(1, sizeof(int));
#ifdef JOS_USER
    uint64_t tid[JOS_NCPU];
    void *ret;
    if (profile_kern) {
        sys_debug(kdebug_prof, kprof_reset, 0, 0, 0, 0, 0);
        sys_debug(kdebug_prof, kprof_enable, 0, 0, 0, 0, 0);
    }
#endif
    dprintf("running mr task %d \n", type);

    if (type == MERGE)
        nr_cpus = mr_state.nr_merge_cpus;

    for (i = 0; i < nr_cpus; i++) {
#ifdef JOS_USER
        echeck_ret(mthread_create(&tid[i], lcpu_to_pcpu[i],
                                  worker_pool[type], (void *) task_pos));
#else
        if (lcpu_to_pcpu[i] == main_pcpu)
            continue;
        echeck_ret(mr_thread_create(lcpu_to_pcpu[i],
                                    worker_pool[type], (void *) task_pos));
#endif
    }
#ifndef JOS_USER
    echeck_ret(mr_thread_create(main_pcpu,
                                worker_pool[type], (void *) task_pos));
#endif
    for (i = 0; i < nr_cpus; i++) {
#ifdef JOS_USER
        echeck_ret(mthread_join(tid[i], &ret));
#else
        int pcpu = lcpu_to_pcpu[i];
        if (pcpu == main_pcpu)
            continue;
        echeck_ret(mr_thread_join(pcpu));
#endif
    }
#ifdef JOS_USER
    if (profile_kern)
        sys_debug(kdebug_prof, kprof_print, 0, 0, 0, 0, 0);
#endif
    free(task_pos);
    return 0;
}

#ifndef __WIN__
static uint64_t JSHARED_ATTR total_map_time;
static uint64_t JSHARED_ATTR total_reduce_time;
static uint64_t JSHARED_ATTR total_merge_time;
#endif

static inline int
run_mr(void)
{
    uint64_t start_time, map_time, reduce_time, merge_time;
    if (map_prefault) {
        start_time = read_tsc();
        echeck_ret(run_mr_task(MAP_PREFAULT));
        if (phase_timing) {
            uint64_t prefault_time = read_tsc() - start_time;
            printf("map prefault time is %ld\n", prefault_time * 1000 / get_cpu_freq());
        }
    }
    if (phase_timing)
        start_time = read_tsc();
    echeck_ret(run_mr_task(MAP));
    if (mr_state.lock_free_sched)
        free(mr_state.map_task_arr);
    if (phase_timing) {
        map_time = read_tsc() - start_time;
        if (print_phase_times)
            printf("map time is %ld\n", map_time * 1000 / get_cpu_freq());
        start_time = read_tsc();
    }
    if (reduce_prefault) {
        echeck_ret(run_mr_task(REDUCE_PREFAULT));
        if (phase_timing) {
            uint64_t prefault_time = read_tsc() - start_time;
            printf("reduce prefault time is %ld\n", prefault_time * 1000 / get_cpu_freq());
            start_time = read_tsc();
        }
    }
    echeck_ret(run_mr_task(REDUCE));
    if (phase_timing) {
        reduce_time = read_tsc() - start_time;
        start_time = read_tsc();
    }
    mr_state.merge_tasks = mr_state.mr_fixed.reduce_tasks;
    mr_state.nr_merge_cpus = mr_state.mr_fixed.nr_cpus;
    while (mr_state.nr_merge_cpus >= mr_state.merge_tasks)
        mr_state.nr_merge_cpus /= 2;
    do {
        dprintf("run_mr merge : nr_cpus %d merge_tasks %d\n",
                mr_state.nr_merge_cpus, mr_state.merge_tasks);
        echeck_ret(run_mr_task(MERGE));
        mr_state.merge_tasks /= 2;
        if (mr_state.merge_tasks > mr_state.nr_merge_cpus) {
            mr_state.merge_tasks = mr_state.nr_merge_cpus;
        }
        mr_state.nr_merge_cpus /= 2;
    } while (mr_state.merge_tasks > 1 && mr_state.nr_merge_cpus > 0);
    mr_state.mr_fixed.final_results->data = mr_state.reduce_results[0].arr;
    mr_state.mr_fixed.final_results->length = mr_state.reduce_results[0].len;
    free(mr_state.reduce_results);
    if (phase_timing) {
        merge_time = read_tsc() - start_time;
        start_time = read_tsc();
    }
    total_map_time += map_time;
    total_reduce_time += reduce_time;
    total_merge_time += merge_time;
    return 0;
}

/*
 * exported public functions.
 */
#ifndef __WIN__
void
print_phase_time(void)
{
    print_pmc();
    prof_print();
    uint64_t total_time = total_map_time + total_reduce_time + total_merge_time;
    mr_debug(print_phase_times, "%d\t%ld\t%ld\t%ld\t%ld\t",
	     mr_state.mr_fixed.nr_cpus, total_map_time * 1000 / get_cpu_freq(),
	     total_reduce_time * 1000 / get_cpu_freq(),
	     total_merge_time * 1000 / get_cpu_freq(),
	     total_time * 1000 / get_cpu_freq());
#ifdef JOS_USER
    josmp_results.times[mr_state.mr_fixed.nr_cpus - 1].map +=
	total_map_time * 1000000 / get_cpu_freq();
    josmp_results.times[mr_state.mr_fixed.nr_cpus - 1].reduce +=
	total_reduce_time * 1000000 / get_cpu_freq();
    josmp_results.times[mr_state.mr_fixed.nr_cpus - 1].merge +=
	total_merge_time * 1000000 / get_cpu_freq();
    josmp_results.times[mr_state.mr_fixed.nr_cpus - 1].tot +=
	total_time * 1000000 / get_cpu_freq();
#endif
}
#endif

/* entry point of map-reudce */
int
mr_run_scheduler(mr_param_t * param)
{
    memset(&mr_state, 0, sizeof(mr_state_t));
    echeck_ret(setup_mr(param));
    echeck_ret(run_mr());
    return 0;
}

void
finit_mr(void)
{
#ifdef JOS_USER
    mr_finalize_processors();
#else
    mr_finit_threadpool(mr_state.mr_fixed.nr_cpus);
#endif
}

void
emit_intermediate(void *key, void *val, int key_size)
{
    //uint64_t start = read_tsc();
    unsigned reduce_pos = mr_state.mr_fixed.part_func(key, key_size);
    unsigned hash_val = reduce_pos;
    reduce_pos %= mr_state.mr_fixed.reduce_tasks;
    map_put(cur_row, reduce_pos, hash_val, key, val);
    //ltsc_insert += read_tsc() - start;
    //linsert_cnt ++;
}

void
emit(void *key, void *val)
{
    keyval_arr_t *kv_arr = &mr_state.reduce_results[cur_reduce_task];
    put_key_val(kv_arr, key, val);
}
