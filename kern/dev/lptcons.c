#include <machine/x86.h>
#include <kern/console.h>
#include <kern/arch.h>
#include <kern/intr.h>
#include <kern/lib.h>
#include <kern/prof.h>
#include <dev/lptcons.h>

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
    inb(0x84);
    inb(0x84);
    inb(0x84);
    inb(0x84);
}

/***** Parallel port output code *****/

static void
lpt_putc(void *arg, int c, cons_source src)
{
    int i;

    for (i = 0; !(inb(0x378 + 1) & 0x80) && i < 12800; i++)
	delay();
    outb(0x378 + 0, c);
    outb(0x378 + 2, 0x08 | 0x04 | 0x01);
    outb(0x378 + 2, 0x08);
}

static void
lpt_intr(struct Processor *ps, void *arg)
{
    // do nothing
}

void
lptcons_init(void)
{
    static struct interrupt_handler ih = {.ih_func = &lpt_intr };
    irq_register(7, &ih);

    int lpt_enable = 1;
    if (strstr(&boot_cmdline[0], "lpt=off"))
	lpt_enable = 0;

    if (lpt_enable) {
	uint64_t start = karch_get_tsc();
	lpt_putc(0, '\n', cons_source_kernel);
	if (karch_get_tsc() - start > 0x100000) {
	    lpt_enable = 0;
	    cprintf("Parallel port too slow, disabling.\n");
	}
    }

    static struct cons_device cd = { .cd_output = &lpt_putc };
    if (lpt_enable)
	cons_register(&cd);
}
