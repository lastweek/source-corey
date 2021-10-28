#ifndef JOS_KERN_INTR_H
#define JOS_KERN_INTR_H

#include <machine/types.h>
#include <inc/queue.h>
#include <kern/processor.h>

struct interrupt_handler {
    void (*ih_func) (struct Processor *, void *);
    void *ih_arg;
    LIST_ENTRY(interrupt_handler) ih_link;
};

void	irq_handler(struct Processor *ps, uint32_t irqno);
void	irq_register(uint32_t irq, struct interrupt_handler *ih);

#endif
