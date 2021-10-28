#ifndef JOS_MACHINE_IRQ_H
#define JOS_MACHINE_IRQ_H

#include <machine/types.h>

void irq_init(void);
void irq_eoi(uint32_t irqno);

#endif
