#include <machine/x86.h>
#include <machine/proc.h>
#include <machine/trapcodes.h>
#include <dev/isareg.h>
#include <dev/timerreg.h>
#include <dev/kclock.h>
#include <kern/lib.h>
#include <kern/timer.h>
#include <kern/arch.h>

enum { pit_hz = 100 };

struct pit_state {
    struct time_source pit_timesrc;
    int pit_tval;
    uint64_t pit_ticks;
};

unsigned
mc146818_read(unsigned reg)
{
    outb(IO_RTC, reg);
    return inb(IO_RTC + 1);
}

void
mc146818_write(unsigned reg, unsigned datum)
{
    outb(IO_RTC, reg);
    outb(IO_RTC + 1, datum);
}

static uint64_t
pit_get_ticks(void *arg)
{
    struct pit_state *ps = arg;
    return ps->pit_ticks;
}

static int
pit_gettick(void)
{
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
    int lo = inb(TIMER_CNTR0);
    int hi = inb(TIMER_CNTR0);
    return (hi << 8) | lo;
}

static void
pit_delay(void *arg, uint64_t nsec)
{
    struct pit_state *ps = arg;
    uint64_t usec = nsec / 1000;
    int tick_start = pit_gettick();

    // This obtuse code comes from NetBSD sys/arch/amd64/isa/clock.c
    int t_sec = usec / 1000000;
    int t_usec = usec % 1000000;
    int ticks = t_sec * TIMER_FREQ +
		t_usec * (TIMER_FREQ / 1000000) +
		t_usec * ((TIMER_FREQ % 1000000) / 1000) / 1000 +
		t_usec * (TIMER_FREQ % 1000) / 1000000;

    while (ticks > 0) {
	int tick_now = pit_gettick();
	if (tick_now > tick_start)
	    ticks -= ps->pit_tval - (tick_now - tick_start);
	else
	    ticks -= tick_start - tick_now;
	tick_start = tick_now;
    }
}

static void
pit_intr(void *arg)
{
}

static void
pit_set_interval(void *arg, proc_id_t pid, uint64_t hz)
{
    // Chaning the timesrc frequency may do bad things
    if (!hz)
	irq_arch_disable(IRQ_TIMER, 0);
    else
	irq_arch_enable(IRQ_TIMER, 0);
}

void
pit_init(void)
{
    static struct pit_state pit_state;
    if (the_timesrc)
	return;

    /* initialize 8253 clock to interrupt pit_hz times/sec */
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_RATEGEN | TIMER_16BIT);

    pit_state.pit_tval = TIMER_DIV(pit_hz);
    outb(IO_TIMER1, pit_state.pit_tval % 256);
    outb(IO_TIMER1, pit_state.pit_tval / 256);
    
    pit_state.pit_timesrc.type = time_source_pit;
    pit_state.pit_timesrc.freq_hz = pit_hz;
    pit_state.pit_timesrc.ticks = &pit_get_ticks;
    pit_state.pit_timesrc.delay_nsec = &pit_delay;
    pit_state.pit_timesrc.arg = &pit_state;
    if (!the_timesrc)
	the_timesrc = &pit_state.pit_timesrc;

    if (ncpu > 1)
	return;
	
    static struct interval_timer pit_it = {
	.arg = &pit_state, 
	.interval_intr = &pit_intr, 
	.interval_time = &pit_set_interval 
    };

    timer_interval_register(0, &pit_it);

    cprintf("8259A: interval timer fixed at %u Hz\n", pit_hz);
}
