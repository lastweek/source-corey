#include <machine/trapcodes.h>
#include <kern/prof.h>
#include <kern/arch.h>
#include <kern/lib.h>
#include <inc/syscall_num.h>

enum { prof_print_count_threshold = 100 };
enum { prof_print_cycles_threshold = UINT64(10000000) };

static int prof_enable;

struct entry {
    uint64_t count;
    uint64_t time;
    struct spinlock lock;
};

struct entry sysc_table[NSYSCALLS];
struct entry trap_table[NTRAPS];

void
prof_syscall(uint64_t num, uint64_t time)
{
    if (!prof_enable)
	return;

    if (num >= NSYSCALLS)
	return;

    spin_lock(&sysc_table[num].lock);
    sysc_table[num].count++;
    sysc_table[num].time += time;
    spin_unlock(&sysc_table[num].lock);
}

void
prof_trap(uint64_t num, uint64_t time)
{
    if (!prof_enable)
	return;

    if (num >= NTRAPS)
	return;

    spin_lock(&sysc_table[num].lock);
    trap_table[num].count++;
    trap_table[num].time += time;
    spin_unlock(&sysc_table[num].lock);
}

void
prof_reset(void)
{
    memset(sysc_table, 0, sizeof(sysc_table));
    memset(trap_table, 0, sizeof(trap_table));
}

static void
print_entry(struct entry *tab, int i, const char *name)
{
    if (tab[i].count > prof_print_count_threshold ||
	tab[i].time > prof_print_cycles_threshold)
	cprintf("%3d cnt%12"PRIu64" tot%12"PRIu64" avg%12"PRIu64" %s\n",
		i,
		tab[i].count, tab[i].time, tab[i].time / tab[i].count, name);
}

void
prof_print(void)
{
    cprintf("prof_print: syscalls\n");
    for (int i = 0; i < NSYSCALLS; i++)
	print_entry(&sysc_table[0], i, syscall2s(i));
    
    cprintf("prof_print: traps\n");
    for (int i = 0; i < NTRAPS; i++)
	print_entry(&trap_table[0], i, "trap");

    prof_reset();
}

void
prof_set_enable(int e)
{
    prof_enable = e;
}
