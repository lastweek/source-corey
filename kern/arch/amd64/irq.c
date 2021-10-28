#include <machine/irq.h>
#include <machine/trapcodes.h>
#include <machine/proc.h>
#include <kern/lib.h>
#include <kern/intr.h>
#include <kern/lockmacro.h>
#include <kern/arch.h>
#include <dev/picirq.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>

static void
irq_pic_spurious_handler(struct Processor *ps, void *x)
{
    uint32_t irqno = (uint32_t)(uintptr_t)x;
    cprintf("IRQ %u: spurious, ignoring\n", irqno);
    
    if (ps->ps_mode == ps_mode_vm)
	panic("XXX vmm flux");
}

static void
irq_timer_handler(struct Processor *ps, void *x)
{
    uint32_t irqno = (uint32_t)(uintptr_t)x;
    uint32_t trapno = IRQ_OFFSET + irqno;

    if (ps->ps_mode == ps_mode_vm)
	panic("XXX vmm flux");
    else {
	int r = locked_call(processor_utrap, ps, trapno, 0, 0);
	if (r < 0) {
	    cprintf("Unknown trap %u, cannot utrap: %s.  Trapframe:\n",
		    trapno, e2s(r));
	    trapframe_print(&ps->ps_tf);
	    processor_halt(ps);
	}
    }
}

static void 
irq_apic_handler(struct Processor *ps, void *x)
{
    uint32_t irqno = (uint32_t)(uintptr_t)x;
    if (irqno == IRQ_SPURIOUS) {
	cprintf("apic_intr_handler: spurious IRQ\n");
	return;
    }
    if (irqno == IRQ_ERROR) {
	lapic_print_error();
	return;
    }
}

void
irq_eoi(uint32_t irqno)
{
    // We route ExtInt through LVTINT0, so we always write LAPIC EOI
    lapic_eoi(irqno);

    // The IRQ may have originated from the 8259A PIC
    if (irqno != IRQ_TIMER || ncpu == 1)
        picirq_eoi(irqno);
}

void
irq_arch_enable(uint8_t irq, proc_id_t pid)
{
    picirq_enable(irq);
    if (ncpu)
	ioapic_enable(irq, pid);    
}

void
irq_arch_disable(uint8_t irq, proc_id_t pid)
{
    picirq_disable(irq);
    if (ncpu)
	ioapic_disable(irq, pid);    
}

void
irq_init(void)
{
    static struct interrupt_handler irq_pic_spurious = { 
	.ih_func = irq_pic_spurious_handler, 
	.ih_arg = (void*)IRQ_PIC_SPURIOUS 
    };
    static struct interrupt_handler irq_timer = { 
	.ih_func = irq_timer_handler,
	.ih_arg = (void*)IRQ_TIMER
    };
    static struct interrupt_handler irq_apic_error_handler = { 
	.ih_func = irq_apic_handler,
	.ih_arg = (void*)IRQ_ERROR
    };
    static struct interrupt_handler irq_apic_spurious_handler = { 
	.ih_func = irq_apic_handler,
	.ih_arg = (void*)IRQ_SPURIOUS
    };
    
    irq_register(IRQ_PIC_SPURIOUS, &irq_pic_spurious);
    irq_register(IRQ_TIMER, &irq_timer);
    irq_register(IRQ_ERROR, &irq_apic_error_handler);
    irq_register(IRQ_SPURIOUS, &irq_apic_spurious_handler);
}
