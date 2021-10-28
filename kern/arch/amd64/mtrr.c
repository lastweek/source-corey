#include <machine/pmap.h>
#include <machine/x86.h>
#include <inc/error.h>
#include <inc/intmacro.h>

struct mtrr_var 
{
    uint8_t valid;
    uint64_t base;
    uint64_t mask;
};

#define NMTRRS (uint32_t)32
static struct mtrr_var mtrrs[NMTRRS];

int
mtrr_set(physaddr_t base, uint64_t nbytes, uint32_t type)
{
    uint64_t new_base = base | type;
    uint64_t new_mask = (MTRR_MASK_FULL & ~(nbytes - 1)) | MTRR_MASK_VALID;

    uint32_t cnt = JMIN(read_msr(MTRR_CAP) & MTRR_CAP_VCNT_MASK, NMTRRS);
    for (uint32_t i = 0; i < cnt; i++) {
	uint64_t i_base = read_msr(MTRR_BASE(i));
	uint64_t i_mask = read_msr(MTRR_MASK(i));

	if (i_base == new_base && i_mask == new_mask) {
	    cprintf("mtrr_set: dup: base %"PRIx64", mask %"PRIx64"\n",
		    i_base, i_mask);
	    return 0;
	}

	if (i_mask & MTRR_MASK_VALID)
	    continue;

	mtrrs[i].valid = 1;
	mtrrs[i].base = new_base;
	mtrrs[i].mask = new_mask;

	write_msr(MTRR_BASE(i), new_base);
	write_msr(MTRR_MASK(i), new_mask);
	return 0;
    }

    if (cnt > NMTRRS)
	cprintf("mtrr_set: %u unsupported MTRRS\n", cnt - NMTRRS);
    return -E_NO_MEM;
}

void
mtrr_ap_init(void)
{
    uint32_t cnt = JMIN(read_msr(MTRR_CAP) & MTRR_CAP_VCNT_MASK, NMTRRS);
    for (uint32_t i = 0; i < cnt; i++) {
	if (mtrrs[i].valid) {
	    write_msr(MTRR_BASE(i), mtrrs[i].base);
	    write_msr(MTRR_MASK(i), mtrrs[i].mask);
	}
    }
}

void
mtrr_init(void)
{
    int r;
    r = mtrr_set(0x0, PGSIZE, MTRR_BASE_UC);
    if (r < 0)
	cprintf("mtrr_init: set failed for [0x%x, 0x%x)\n", 0x0, PGSIZE);
}
