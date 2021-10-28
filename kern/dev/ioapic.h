#ifndef JOS_DEV_IOAPIC_H
#define JOS_DEV_IOAPIC_H

#include <machine/types.h>
#include <inc/proc.h>

// kva of io apic
extern uint8_t *ioapicva;
extern uint32_t ioapicid;

void ioapic_init(void);
void ioapic_enable(uint32_t irq, proc_id_t pid);
void ioapic_disable(uint32_t irq, proc_id_t pid);

#endif
