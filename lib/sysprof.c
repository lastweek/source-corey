#include <machine/x86.h>
#include <inc/kdebug.h>
#include <inc/sysprof.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/lib.h>
#include <inc/syscall.h>

/* AMD vendor code */
static const uint32_t amd_ebx = 0x68747541; /* "htuA" */
static const uint32_t amd_edx = 0x69746e65; /* "itne" */
static const uint32_t amd_ecx = 0x444d4163; /* "DMAc" */

/* Intel vendor code */
static const uint32_t int_ebx = 0x756e6547; /* "uneG" */
static const uint32_t int_edx = 0x49656e69; /* "Ieni" */
static const uint32_t int_ecx = 0x6c65746e; /* "letn" */

struct sysprof_handler {
    void (*prof_prog_l3miss)(uint64_t n);
    void (*prof_prog_cmdcnt)(uint64_t n);
    void (*prof_prog_latcnt)(uint64_t n);
};

static struct sysprof_handler *the_handler;

static void
amd_setup_cnt(uint64_t cnt, uint64_t sel, uint64_t unit)
{
    /* Intentionally avoid setting PES_CNT_TRSH_SHIFT, which has the
     * side effect that only one event per cycle is counted on AMD.
     */
    uint64_t val = 
	PES_EVT(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN;
    assert(sys_debug(kdebug_hw, cnt, val, 0, 0, 0, 0) == 0);
}

#if 0
static void
intel_setup_cnt(uint64_t cnt, uint64_t sel, uint64_t unit)
{
    /* Intentionally avoid setting PES_CNT_TRSH_SHIFT, which has the
     * side effect that only one event per cycle is counted on AMD.
     */
    uint64_t val = 
	PES_EVT_INTEL(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN;
    assert(sys_debug(kdebug_hw, cnt, val, 0, 0, 0, 0) == 0);
}
#endif

static void
amd_prog_l3miss(uint64_t n)
{
    assert(n < 3);
    
    if (core_env->pid % 4)
	return;

    uint64_t cores = 0x0F << 4;
    amd_setup_cnt(n,
		  L3_CACHE_MISSES,
		  L3_CACHE_MISSES_EXCL | 
		  L3_CACHE_MISSES_SHAR | 
		  L3_CACHE_MISSES_MOD |
		  cores);
}

static void
amd_prog_cmdcnt(uint64_t n)
{
    assert(n < 3);
    
    if (core_env->pid % 4)
	return;

    uint64_t unit = 0xFF;
    amd_setup_cnt(n, 0x1e3, unit);
}

static void
amd_prog_latcnt(uint64_t n)
{
    assert(n < 3);
    
    if (core_env->pid % 4)
	return;

    uint64_t unit = 0xFF;
    amd_setup_cnt(n, 0x1e2, unit);
}

struct sysprof_handler amd_handler = {
    .prof_prog_l3miss = &amd_prog_l3miss,
    .prof_prog_cmdcnt = &amd_prog_cmdcnt,
    .prof_prog_latcnt = &amd_prog_latcnt,
};

static void
int_prog_l3miss(uint64_t n)
{
}

static void
int_prog_cmdcnt(uint64_t n)
{
}

static void
int_prog_latcnt(uint64_t n)
{
}

struct sysprof_handler int_handler = {
    .prof_prog_l3miss = &int_prog_l3miss,
    .prof_prog_cmdcnt = &int_prog_cmdcnt,
    .prof_prog_latcnt = &int_prog_latcnt,
};

void
sysprof_init(void)
{
    if (the_handler)
	return;

    uint32_t ebx, ecx, edx;
    cpuid(0, 0, &ebx, &ecx, &edx);
    if (ebx == amd_ebx && ecx == amd_ecx && edx == amd_edx)
	the_handler = &amd_handler;
    else if (ebx == int_ebx && ecx == int_ecx && edx == int_edx)
	the_handler = &int_handler;
    else
	panic("unknown vendor: %08x %08x %08x", ebx, ecx, edx);
}

uint64_t 
sysprof_rdpmc(uint32_t n)
{
    return read_pmc(n);
}

void
sysprof_prog_l3miss(uint32_t n)
{
    the_handler->prof_prog_l3miss(n);
}

void
sysprof_prog_cmdcnt(uint32_t n)
{
    the_handler->prof_prog_cmdcnt(n);
}

void
sysprof_prog_latcnt(uint32_t n)
{
    the_handler->prof_prog_latcnt(n);
}
