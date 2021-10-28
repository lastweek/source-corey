#include <kern/intr.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <inc/queue.h>
#include <inc/intmacro.h>
#include <inc/error.h>

LIST_HEAD(ih_list, interrupt_handler);
struct ih_list irq_handlers[JOS_MAX_IRQS];
static uint64_t irq_warnings[JOS_MAX_IRQS];

// XXX When an interrupt is enabled by a libOS, they should 
// provide the Processor obj that is used to handle the interrupt.

void
irq_handler(struct Processor *ps, uint32_t irqno)
{
    if (irqno >= JOS_MAX_IRQS)
	panic("irq_handler: invalid IRQ %d", irqno);

    if (LIST_FIRST(&irq_handlers[irqno]) == 0) {
	irq_warnings[irqno]++;
	if (IS_POWER_OF_2(irq_warnings[irqno]))
	    cprintf("IRQ %d not handled (%"PRIu64")\n",
		    irqno, irq_warnings[irqno]);
    }

    struct interrupt_handler *ih;
    LIST_FOREACH(ih, &irq_handlers[irqno], ih_link)
	ih->ih_func(ps, ih->ih_arg);
}

void
irq_register(uint32_t irq, struct interrupt_handler *ih)
{
    if (irq >= JOS_MAX_IRQS)
	panic("irq_register: invalid IRQ %d", irq);

    LIST_INSERT_HEAD(&irq_handlers[irq], ih, ih_link);
}
