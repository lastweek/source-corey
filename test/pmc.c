#include "bench.h"
#include "pmc.h"
#include "pmcreg.h"

#include <libperfctr.h>
#include <string.h>
#include <errno.h>

#if PMC_ENABLE

void
pmc_init(uint32_t cid)
{
    if (cid % 4)
	return;

    // Just program event counter 0 for remote refills
    struct vperfctr *self = vperfctr_open();
    if(!self)
	eprint("vperfctr_open failed: %s\n", strerror(errno));

    struct vperfctr_control vc;;
    memset(&vc, 0, sizeof vc);
    struct perfctr_cpu_control *c = &vc.cpu_control;
    
    c->tsc_on = 1;
    c->pmc_map[0] = 0;
    c->pmc_map[1] = 1;
    c->pmc_map[2] = 2;
    c->pmc_map[3] = 3;
    c->nractrs = 4;
    
    // pmc0 counts the number of data cache reills statisfied from
    // the L2 cache.  
    uint64_t sel = 0x042;
    uint64_t unit = 0x2 | 0x4 | 0x8 | 0x10;
    uint64_t val = 
	PES_EVT(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN | 
	(1 << PES_CNT_TRSH_SHIFT);

    c->evntsel[0] = val & 0xFFFFFFFF;
    c->evntsel_high[0] = val >> 32;

    // pmc1 counts the number of responses from the northbridge 
    // (DRAM, L3 or another processor's cache) for data cache, instruction cache, 
    // or L2 cache refill requests.  This count measures amount of data moved
    // into a core's private caches.
    //sel = 0x06c;
    //unit = 0x01 | 0x02 | 0x04;

    // l2
    //sel = 0x07e;
    //unit = 0x02;

    // l3
    sel = 0x4e1;
    unit = 0x7 | (0x10 << (cid % 4));

    val = 
	PES_EVT(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN | 
	(1 << PES_CNT_TRSH_SHIFT);

    c->evntsel[1] = val & 0xFFFFFFFF;
    c->evntsel_high[1] = val >> 32;

    // pmc2 counts the number of instructions retired.
    sel = 0x0c0;
    unit = 0;

    val = 
	PES_EVT(sel) | 
	PES_OS_MODE | 
	PES_USR_MODE | 
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN | 
	(1 << PES_CNT_TRSH_SHIFT);

    c->evntsel[2] = val & 0xFFFFFFFF;
    c->evntsel_high[2] = val >> 32;

#if 0
    // pmc3 counts the number of L2 cache misses
    sel = 0x07e;
    unit = 0x0F;

    val =
	PES_EVT(sel) |
	PES_OS_MODE |
	PES_USR_MODE |
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN |
	(1 << PES_CNT_TRSH_SHIFT);

    c->evntsel[3] = val & 0xFFFFFFFF;
    c->evntsel_high[3] = val >> 32;
#endif

    sel = 0x4e1;
    unit = 0x0F7;
    
    val =
	PES_EVT(sel) |
	PES_OS_MODE |
	PES_USR_MODE |
	(unit << PES_UNIT_SHIFT) |
	PES_CNT_EN;
    
    c->evntsel[3] = val & 0xFFFFFFFF;
    c->evntsel_high[3] = val >> 32;

    if(vperfctr_control(self, &vc) < 0)
	eprint("vperfctr_control failed: %s\n", strerror(errno));
}

#endif
