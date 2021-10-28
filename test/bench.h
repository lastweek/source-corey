/*
 * Some helpful stuff..
 */
#ifndef BENCH_H
#define BENCH_H

#define JOS_PREFETCH	1
#define JOS_CLINE	64
#define JOS_NCPU	16

#define INLINE_ATTR	static __inline __attribute__((always_inline, no_instrument_function))

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

/*
 * Rounding operations (efficient when n is a power of 2)
 * Round down to the nearest multiple of n
 */
#define ROUNDDOWN(a, n)                         \
({                                              \
        uintptr_t __ra = (uintptr_t) (a);       \
        (__typeof__(a)) (__ra - __ra % (n));    \
})

/*
 * Round up to the nearest multiple of n
 */
#define ROUNDUP(a, n)                                                   \
({                                                                      \
        uintptr_t __n = (uintptr_t) (n);                                \
        (__typeof__(a)) (ROUNDDOWN((uintptr_t) (a) + __n - 1, __n));    \
})

#define VOIDSTAR(e) ((void *)(uintptr_t)(e))

#define errno_check(expr)                                               \
    do {                                                                \
        int __r = (expr);						\
        if (__r < 0) {                                                  \
            fprintf(stderr, "%s:%u: %s - %s\n",                         \
                                  __FILE__, __LINE__, #expr,            \
                                  strerror(errno));                     \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)

#define dprint(__exp, __frmt, __args...)				\
    do {								\
    if (__exp)								\
        printf("(debug) %s: " __frmt "\n", __FUNCTION__, ##__args);	\
    } while (0)

#define eprint(__frmt, __args...)					\
    do {								\
       fprintf(stderr, __frmt, ##__args);				\
       exit(EXIT_FAILURE);						\
    } while (0)

#define array_size(arr) (sizeof(arr) / sizeof((arr)[0]))
#define array_end(arr) ((arr) + array_size(arr))

INLINE_ATTR uint32_t rnd(uint32_t *seed);
INLINE_ATTR uint64_t read_tsc(void);
INLINE_ATTR uint64_t read_pmc(uint32_t i);
INLINE_ATTR void nop_pause(void);
INLINE_ATTR uint64_t usec(void);
INLINE_ATTR uint64_t get_cpu_freq(void);
INLINE_ATTR uint32_t get_core_count(void);
INLINE_ATTR int fill_core_array(uint32_t *cid, uint32_t n);
INLINE_ATTR int affinity_set(int cpu);
INLINE_ATTR pthread_t pthread_start(void *(*fn)(void *), uintptr_t arg);
INLINE_ATTR void prefetchw(void *a);
INLINE_ATTR void lfence(void);

uint32_t
rnd(uint32_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return *seed & 0x7fffffff;
}

uint64_t
read_tsc(void)
{
    uint32_t a, d;
    __asm __volatile("rdtsc" : "=a" (a), "=d" (d));
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}

uint64_t
read_pmc(uint32_t ecx)
{
    uint32_t a, d;
    __asm __volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (ecx));
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}

void
nop_pause(void)
{
    __asm __volatile("pause" : : );
}

uint64_t
usec(void) 
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

uint64_t
get_cpu_freq(void)
{
    FILE *fd;
    uint64_t freq = 0;
    float freqf = 0;
    char *line = NULL;
    size_t len = 0;
    
    fd = fopen("/proc/cpuinfo", "r");
    if (!fd) {
	fprintf(stderr, "failed to get cpu frequecy\n");
	perror(NULL);
	return freq;
    }
    
    while (getline(&line, &len, fd) != EOF) {
	if (sscanf(line, "cpu MHz\t: %f", &freqf) == 1) {
	    freqf = freqf * 1000000UL;
	    freq = (uint64_t)freqf;
	    break;
	}
    }
    
    fclose(fd);
    return freq;
}

uint32_t
get_core_count(void)
{
    int r = sysconf(_SC_NPROCESSORS_CONF);
    if (r < 0)
	eprint("get_core_count: error: %s\n", strerror(errno));
    return r;
}

int
fill_core_array(uint32_t *cid, uint32_t n)
{
    uint32_t z = get_core_count();
    if (n < z)
	return -1;
    
    for (uint32_t i = 0; i < z; i++)
	cid[i] = i;
    return z;
}

int
affinity_set(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

pthread_t
pthread_start(void *(*fn)(void *), uintptr_t arg)
{
    pthread_t th;
    assert(pthread_create(&th, 0, fn, (void *)arg) == 0);
    return th;
}

#if JOS_PREFETCH
void
prefetchw(void *a)
{
    __asm __volatile("prefetchw (%0)" : : "r" (a));
}
#else
void
prefetchw(void *a)
{
}
#endif

void 
lfence(void)
{
    __asm __volatile("lfence");    
}

#endif
