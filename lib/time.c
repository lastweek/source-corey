#include <machine/x86.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/error.h>
#include <inc/assert.h>
#include <inc/utrap.h>
#include <inc/arch.h>

#include <sys/time.h>
#include <time.h>

libc_hidden_proto(gettimeofday)

// in lieu of a real time implementation

// make gcc happy
void time_tick(void);

uint64_t ticks;
uint64_t int_hz;
uint64_t def_hz = 100;

/*
 * timer_convert() assumes both a and b are on the order of 1<<32.
 */
static uint64_t
timer_convert(uint64_t n, uint64_t a, uint64_t b)
{
    uint64_t hi = n >> 32;
    uint64_t lo = n & 0xffffffff;

    uint64_t hi_hz = hi * a;
    uint64_t hi_b = hi_hz / b;
    uint64_t hi_hz_carry = hi_hz - hi_b * b;

    uint64_t lo_hz = lo * a + (hi_hz_carry << 32);
    uint64_t lo_b = lo_hz / b;

    return (hi_b << 32) + lo_b;
}

uint64_t
time_nsec(void)
{
    if (!int_hz) {
	cprintf("time_nsec: initializing hz to %lu\n", def_hz);
	time_init(def_hz);
    }
    return timer_convert(ticks, NSEC_PER_SECOND, int_hz);
}

void
time_tick(void)
{
    if (ticks == UINT64(~0))
	panic("tick overflow");
    ticks++;
}

void
time_init(uint64_t hz)
{
    int r = sys_processor_set_interval(processor_current(), hz);
    if (r < 0)
	panic("unable to set interval: %s", e2s(r));
    int_hz = hz;
}

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
    uint64_t nsec = time_nsec();
    tv->tv_sec = nsec / NSEC_PER_SECOND;
    tv->tv_usec = (nsec % NSEC_PER_SECOND) / 1000;
    return 0;
}

uint64_t
time_cycles_to_nsec(uint64_t cycles)
{
    return timer_convert(cycles, NSEC_PER_SECOND, core_env->cpufreq);
}

void
time_delay_cycles(uint64_t c)
{
    uint64_t s = arch_read_tsc();
    while (arch_read_tsc() - s < c);
}

libc_hidden_def(gettimeofday)
