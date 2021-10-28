// The I/O APIC manages hardware interrupts for an SMP system.
// http://www.intel.com/design/chipsets/datashts/29056601.pdf
// See also picirq.c.

#include <dev/ioapic.h>
#include <machine/trapcodes.h>
#include <kern/arch.h>

enum { trace_irq_enable = 0 };

uint8_t *ioapicva;
uint32_t ioapicid;

#define REG_ID     0x00  // Register index: ID
#define REG_VER    0x01  // Register index: version
#define REG_TABLE  0x10  // Redirection table base

// The redirection table starts at REG_TABLE and uses
// two registers to configure each interrupt.  
// The first (low) register in a pair contains configuration bits.
// The second (high) register contains a bitmask telling which
// CPUs can serve that interrupt.
#define INT_DISABLED   0x00010000  // Interrupt disabled
#define INT_LEVEL      0x00008000  // Level-triggered (vs edge-)
#define INT_ACTIVELOW  0x00002000  // Active low (vs high)
#define INT_LOGICAL    0x00000800  // Destination is CPU id (vs APIC ID)

volatile struct ioapic *ioapic;

// IO APIC MMIO structure: write reg, then read or write data.
struct ioapic {
    uint32_t reg;
    uint32_t pad[3];
    uint32_t data;
};

static uint32_t
ioapic_read(uint32_t reg)
{
    ioapic->reg = reg;
    return ioapic->data;
}

static void
ioapic_write(uint32_t reg, uint32_t data)
{
    ioapic->reg = reg;
    ioapic->data = data;
}

void
ioapic_init(void)
{
    int i, maxintr;
    uint32_t id;
    
    if (ncpu == 1)
	return;

    ioapic = (volatile struct ioapic*)ioapicva;
    maxintr = (ioapic_read(REG_VER) >> 16) & 0xFF;
    id = ioapic_read(REG_ID) >> 24;
    if (id != ioapicid)
	cprintf("ioapic_init: id(%d) isn't equal to ioapic_id(%d); not a MP\n", id, ioapicid);

    // Mark all interrupts edge-triggered, active high, disabled,
    // and not routed to any CPUs.
    for (i = 0; i <= maxintr; i++) {
	ioapic_write(REG_TABLE+2*i, INT_DISABLED | (IRQ_OFFSET + i));
	ioapic_write(REG_TABLE+2*i+1, 0);
    }
}

void
ioapic_enable(uint32_t irq, proc_id_t cpunum)
{
    if (ncpu == 1)
	return;
    
    // Mark interrupt edge-triggered, active high,
    // enabled, and routed to the given cpunum,
    // which happens to be that cpu's APIC ID.
    if (trace_irq_enable)
        cprintf("irq %u enabled on APIC ID %u \n", irq, cpunum);

    ioapic_write(REG_TABLE+2*irq, IRQ_OFFSET + irq);
    ioapic_write(REG_TABLE+2*irq+1, cpunum << 24);
}

void
ioapic_disable(uint32_t irq, proc_id_t cpunum)
{
    if (ncpu == 1)
	return;
    
    if (irq)
	ioapic_write(REG_TABLE+2*irq, INT_DISABLED | (IRQ_OFFSET + irq));
	ioapic_write(REG_TABLE+2*irq+1, 0);
}
