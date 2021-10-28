#include <machine/x86.h>
#include <machine/pmap.h>
#include <machine/mmu.h>
#include <machine/trapcodes.h>
#include <machine/proc.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/timer.h>
#include <kern/intr.h>
#include <dev/lapic.h>
#include <dev/apicreg.h>
#include <dev/picirq.h>
#include <dev/isareg.h>
#include <dev/nvram.h>
#include <inc/error.h>

enum { lapic_debug = 0 };

struct apic_timer_state {
    struct interval_timer it;
    uint64_t hz;
};

// Global pointer to CPU local APIC
uint8_t *lapic;

static uint32_t
apic_read(uint32_t off)
{
    return *(volatile uint32_t *) (lapic + off);
}

static void
apic_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *) (lapic + off) = val;
    apic_read(LAPIC_ID);	// Wait for the write to finish, by reading
}

static int
apic_icr_wait()
{
    uint32_t i = 100000;
    while ((apic_read(LAPIC_ICRLO) & LAPIC_DLSTAT_BUSY) != 0) {
	nop_pause();
	i--;
	if (i == 0) {
	    cprintf("apic_icr_wait: wedged?\n");
	    return -E_INVAL;
	}
    }
    return 0;
}

static int
ipi_init(uint32_t apicid)
{
    // Intel MultiProcessor spec. section B.4.1
    apic_write(LAPIC_ICRHI, apicid << LAPIC_ID_SHIFT);
    apic_write(LAPIC_ICRLO, apicid | LAPIC_DLMODE_INIT | LAPIC_LVL_TRIG |
	       LAPIC_LVL_ASSERT);
    apic_icr_wait();
    timer_delay(10 * 1000000);	// 10ms

    apic_write(LAPIC_ICRLO, apicid | LAPIC_DLMODE_INIT | LAPIC_LVL_TRIG |
	       LAPIC_LVL_DEASSERT);
    apic_icr_wait();

    return (apic_read(LAPIC_ICRLO) & LAPIC_DLSTAT_BUSY) ? -E_BUSY : 0;
}

int32_t
lapic_id(void)
{
    if (lapic)
	return apic_read(LAPIC_ID) >> LAPIC_ID_SHIFT;
    return -E_INVAL;
}

void
lapic_startap(uint32_t apicid, physaddr_t pa)
{
    // Universal Start-up Algorithm from Intel MultiProcessor spec
    int r;
    uint16_t *dwordptr;

    // "The BSP must initialize CMOS shutdown code to 0Ah ..."
    outb(IO_RTC, NVRAM_RESET);
    outb(IO_RTC + 1, NVRAM_RESET_JUMP);

    // "and the warm reset vector (DWORD based at 40:67) to point
    // to the AP startup code ..."
    dwordptr = pa2kva(0x467);
    dwordptr[0] = 0;
    dwordptr[1] = pa >> 4;

    // ... prior to executing the following sequence:"
    if ((r = ipi_init(apicid)) < 0)
	panic("unable to send init: %s", e2s(r));
    timer_delay(10 * 1000000);	// 10ms

    for (uint32_t i = 0; i < 2; i++) {
	apic_icr_wait();
	apic_write(LAPIC_ICRHI, apicid << LAPIC_ID_SHIFT);
	apic_write(LAPIC_ICRLO, LAPIC_DLMODE_STARTUP | (pa >> 12));
	timer_delay(200 * 1000);	// 200us
    }
}

static void
apic_timer_intr(void *arg)
{
}

static void
apic_timer_set_interval(void *arg, proc_id_t pid, uint64_t hz)
{
    struct apic_timer_state *ats = (struct apic_timer_state *) arg;

    if (!hz) {
	uint32_t lvt = apic_read(LAPIC_LVTT) | LAPIC_LVTT_M;
	apic_write(LAPIC_LVTT, lvt);
    } else {
	if (hz > ats->hz / 1000) {
	    cprintf("APIC: truncating interval\n");
	    hz = ats->hz / 1000;
	}
	apic_write(LAPIC_ICR_TIMER, (uint32_t) (ats->hz / hz));
	uint32_t lvt = apic_read(LAPIC_LVTT);
	lvt &= ~LAPIC_LVTT_M;
	apic_write(LAPIC_LVTT, lvt);
    }
}

int
lapic_broadcast(int self, uint32_t ino)
{
    uint32_t flag = self ? LAPIC_DEST_ALLINCL : LAPIC_DEST_ALLEXCL;
    flag |= ino == T_NMI ? LAPIC_DLMODE_NMI : 0;
    apic_write (LAPIC_ICRLO, flag | LAPIC_DLMODE_FIXED | 
		LAPIC_LVL_DEASSERT | ino);
    return apic_icr_wait();
}

int
lapic_ipi(uint32_t cp_id, uint32_t ino)
{
    apic_write (LAPIC_ICRHI, cp_id << LAPIC_ID_SHIFT);
    apic_write (LAPIC_ICRLO, LAPIC_DLMODE_FIXED | 
		LAPIC_LVL_DEASSERT | ino);
    return apic_icr_wait();
}

void
lapic_print_error(void)
{
    static const char *error[8] = {
	"Send checksum error",
	"Recieve checksum error",
	"Send accept error",
	"Recieve accept error",
	"Reserved",
	"Send illegal vector",
	"Recieve illegal vector",
	"Illegal register address"
    };

    char header = 0;

    // write once to reload ESR
    apic_write(LAPIC_ESR, 0);
    uint32_t e = apic_read(LAPIC_ESR);

    for (uint32_t i = 0; i < 8; i++) {
	if (i == 4)
	    continue;
	if (e & (1 << i)) {
	    if (!header) {
		header = 1;
		cprintf("apic error:\n");
	    }
	    cprintf(" %s\n", error[i]);
	}
    }
}

void
lapic_eoi(uint32_t irqno)
{
    if (ncpu == 1)
	return;

    apic_write(LAPIC_EOI, 0);
}

void
lapic_init(void)
{
    if (ncpu == 1)
	return;

    if (!lapic) {
	// Check if APIC is supported
	uint32_t edx;
	cpuid(0x01, 0, 0, 0, &edx);
	if (!(edx & 0x200))
	    return;

	// Get the local APIC base address, and disable caching
	uint64_t bar = read_msr(0x1B);
	physaddr_t pa = (bar >> 12) & 0x0FFFFFFFFFF;
	int r = mtrr_set(pa, PGSIZE, MTRR_BASE_UC);
	if (r < 0)
	    cprintf("lapic_init: out of MTRRs, lapic might not work..\n");
	lapic = pa2kva(pa);
    }

    uint32_t id = (apic_read(LAPIC_ID) & LAPIC_ID_MASK) >> LAPIC_ID_SHIFT;
    uint32_t v = apic_read(LAPIC_VERS);
    uint32_t vers = v & LAPIC_VERSION_MASK;
    uint32_t maxlvt = (v & LAPIC_VERSION_LVT_MASK) >> LAPIC_VERSION_LVT_SHIFT;

    apic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | (IRQ_OFFSET + IRQ_SPURIOUS));
    apic_write(LAPIC_LVERR, IRQ_OFFSET + IRQ_ERROR);

    // This is what Linux does
    if (arch_cpu() == 0) {
	apic_write(LAPIC_LVINT0, LAPIC_DLMODE_EXTINT);
	apic_write(LAPIC_LVINT1, LAPIC_DLMODE_NMI);
    } else {
	apic_write(LAPIC_LVINT0, LAPIC_DLMODE_EXTINT | LAPIC_LVT_MASKED);
	apic_write(LAPIC_LVINT1, LAPIC_DLMODE_NMI | LAPIC_LVT_MASKED);
    }
    
    if (((v >> LAPIC_VERSION_LVT_SHIFT) & 0x0FF) >= 4)
	apic_write(LAPIC_PCINT, LAPIC_LVT_MASKED);

    // Clear error status register (requires back-to-back writes).
    apic_write(LAPIC_ESR, 0);
    apic_write(LAPIC_ESR, 0);

    // Send an Init Level De-Assert to synchronise arbitration ID's.
    apic_write(LAPIC_ICRHI, 0);
    apic_write(LAPIC_ICRLO, LAPIC_DEST_ALLINCL | LAPIC_DLMODE_INIT |
	       LAPIC_LVL_TRIG | LAPIC_LVL_DEASSERT);
    while (apic_read(LAPIC_ICRLO) & LAPIC_DLSTAT_BUSY) ;

    if (lapic_debug)
	cprintf("APIC: version %d, %d LVTs, APIC ID %d\n", vers, maxlvt, id);

    if (!the_timesrc) {
	cprintf("APIC: no time source, unable to calibrate APIC timer\n");
	return;
    }

    static struct apic_timer_state ats;

    if (!ats.hz) {
	// Calibrate APIC timer
	apic_write(LAPIC_DCR_TIMER, LAPIC_DCRT_DIV1);
	apic_write(LAPIC_ICR_TIMER, 0xffffffff);

	uint64_t ccr0 = apic_read(LAPIC_CCR_TIMER);
	the_timesrc->delay_nsec(the_timesrc->arg, 10 * 1000 * 1000);
	uint64_t ccr1 = apic_read(LAPIC_CCR_TIMER);

	ats.hz = (ccr0 - ccr1) * 100;
	ats.it.arg = &ats;
	ats.it.interval_intr = &apic_timer_intr;
	ats.it.interval_time = &apic_timer_set_interval;

	cprintf("APIC: interval timer %ld MHz\n", ats.hz / (1000 * 1000));
    }

    apic_write(LAPIC_DCR_TIMER, LAPIC_DCRT_DIV1);
    apic_write(LAPIC_LVTT, LAPIC_LVT_PERIODIC |
	       LAPIC_LVTT_M | (IRQ_OFFSET + IRQ_TIMER));

    timer_interval_register(arch_cpu(), &ats.it);

    // Ack any outstanding interrupts.
    apic_write(LAPIC_EOI, 0);

    // Enable interrupts on the APIC (but not on the processor).
    apic_write(LAPIC_TPRI, 0);
}
