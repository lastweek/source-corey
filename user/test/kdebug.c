#include <machine/x86.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <test.h>

#include <string.h>

static void __attribute__((unused))
setup_cnt(uint64_t sel, uint64_t unit)
{
    uint64_t val = 
	PES_EVT(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN;
    echeck(sys_debug(kdebug_hw, 0, val, 0, 0, 0, 0));
}

static void
intel_setup_cnt(uint64_t cnt, uint64_t sel, uint64_t unit)
{
    uint64_t val = 
	PES_EVT_INTEL(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN;
    assert(sys_debug(kdebug_hw, cnt, val, 0, 0, 0, 0) == 0);
}

void
kdebug_test(void)
{
    enum { buf_size = 30 * 256 * PGSIZE };

    void *buf = 0;
    echeck(segment_alloc(core_env->sh, buf_size, 0, 
			 &buf, SEGMAP_SHARED, "foo", core_env->pid));

    memset(buf, 0, buf_size);
    intel_setup_cnt(0, 0x3C, 0);
    uint64_t s = read_pmc(0);
    uint64_t ts = read_tsc();
    memset(buf, 0xFF, buf_size);
    uint64_t e = read_pmc(0);
    uint64_t te = read_tsc();
    cprintf("kid: e - s %ld\n", e - s);
    cprintf("kid: te - ts %ld\n", te - ts);
    
#if 0
    memset(buf, 0, buf_size);
    intel_setup_cnt(0, 
		    RETIRED_CACHE_MISS,
		    RETIRED_L1D_MISS_MASK | 
		    RETIRED_L1D_LINE_MISS_MASK |
		    RETIRED_L2_MISS_MASK | 
		    RETIRED_L2_LINE_MISS_MASK);
    uint64_t s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    uint64_t e = read_pmc(0);
    cprintf("e - s %ld\n", e - s);
#endif

#if 0
    echeck(sys_debug(kdebug_nop, 0, 0, 0, 0, 0, 0));
    
    void *buf = 0;
    echeck(segment_alloc(core_env->sh, buf_size, 0, 
			 &buf, 0, "foo", core_env->pid));

    setup_cnt(DATA_CACHE_MISSES, 0);
    uint64_t s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    uint64_t e = read_pmc(0);
    cprintf("data cache misses %ld\n", e - s);

    setup_cnt(DATA_CACHE_REFILLS, DATA_CACHE_REFILLS_SS);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache refills from L2 shared-state lines %ld\n", e - s);

    setup_cnt(DATA_CACHE_REFILLS, DATA_CACHE_REFILLS_ES);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache refills from L2 exclusive-state lines %ld\n", e - s);

    setup_cnt(DATA_CACHE_REFILLS, DATA_CACHE_REFILLS_OS);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache refills from L2 owned-state lines %ld\n", e - s);

    setup_cnt(DATA_CACHE_REFILLS, DATA_CACHE_REFILLS_MS);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache refills from L2 modified-state lines %ld\n", e - s);

    setup_cnt(DATA_CACHE_REFILLS, DATA_CACHE_REFILLS_NB);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache refills from northbrdige %ld\n", e - s);

    setup_cnt(DATA_CACHE_EVICT, 
	      DATA_CACHE_EVICT_ES | 
	      DATA_CACHE_EVICT_IS | 
	      DATA_CACHE_EVICT_SS | 
	      DATA_CACHE_EVICT_OS | 
	      DATA_CACHE_EVICT_MS);
    s = read_pmc(0);
    memset(buf, 0xFF, buf_size);
    e = read_pmc(0);
    cprintf("data cache evicts %ld\n", e - s);
#endif
}
