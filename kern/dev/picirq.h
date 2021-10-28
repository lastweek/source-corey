/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PICIRQ_H
#define JOS_KERN_PICIRQ_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

// I/O Addresses of the two 8259A programmable interrupt controllers
#define IO_PIC1		0x20	// Master (IRQs 0-7)
#define IO_PIC2		0xA0	// Slave (IRQs 8-15)

#ifndef __ASSEMBLER__

#include <inc/types.h>
#include <machine/x86.h>

void pic_init(void);
void picirq_eoi(int irqno);
void picirq_enable(uint32_t irq);
void picirq_disable(uint32_t irq);

void picirq_setmask_8259A(uint16_t mask);

#endif // !__ASSEMBLER__

#endif // !JOS_KERN_PICIRQ_H
