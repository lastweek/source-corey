#ifndef SBW_PMC_H
#define SBW_PMC_H

#include <stdint.h>
#include "bench.h"

#define PMC_ENABLE 1

#if PMC_ENABLE

static __inline uint64_t
pmc_refills(void)
{
    return read_pmc(1);
}

// Data cache refills from the L2
static __inline uint64_t
pmc_refills_l2(void)
{
    return read_pmc(0);
}

// Data cache, instruction cache, or L2 cache refills from northbridge
static __inline uint64_t
pmc_refills_nb(void)
{
    return read_pmc(1);
}

static __inline uint64_t
pmc_ret_ins(void)
{
    return read_pmc(2);
}

static __inline uint64_t
pmc_l2_miss(void)
{
    return read_pmc(3);
}

static __inline uint64_t
pmc_l3_miss(void)
{
    return read_pmc(3);
}

void pmc_init(uint32_t c);

#else

#define pmc_refills() 0
#define pmc_refills_l2() 0
#define pmc_refills_nb() 0
#define pmc_ret_ins() 0
#define pmc_init() do { } while (0)

#endif

#endif
