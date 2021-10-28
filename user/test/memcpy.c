#ifndef JOS_TEST
#define JOSMP 1
#define LINUX 0
#else
#define JOSMP 0
#define LINUX 1
#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#if JOSMP
#include <test.h>
#include <machine/x86.h>
#endif

enum { num_ops = 512 };
enum { max_op = 64 * 1024 * 1024 };

#if LINUX
#define X86_INST_ATTR	static __inline __attribute__((always_inline, no_instrument_function))
X86_INST_ATTR uint64_t
read_tsc(void)
{
	uint32_t a, d;
	__asm __volatile("rdtsc" : "=a" (a), "=d" (d));
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}
#endif

static uint32_t
rnd(uint32_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return *seed & 0x7fffffff;
}


void
memcpy_test(void)
{
    void *src[num_ops];
    void *dst[num_ops];
    uint32_t size[num_ops];
    uint32_t seed = 1;
    for (int i = 0; i < num_ops; i++) {
	size[i] = rnd(&seed) % max_op;
	src[i] = malloc(size[i]);
	dst[i] = malloc(size[i]);
	memset(src[i], 0, size[i]);
	memset(dst[i], 0, size[i]);
    }

    uint64_t s = read_tsc();
    for (int i = 0; i < num_ops; i++) {
	memcpy(dst[i], src[i], size[i]);
    }
    uint64_t e = read_tsc();

    printf("cycles %ld\n", e - s);
}

#if LINUX
int
main(int ac, char **av)
{
    memcpy_test();
}
#endif
