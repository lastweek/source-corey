#include <machine/pmap.h>
#include <machine/x86.h>
#include <machine/trap.h>
#include <machine/multiboot.h>
#include <machine/trapcodes.h>
#include <machine/boot.h>
#include <machine/mp.h>
#include <machine/proc.h>
#include <machine/numa.h>
#include <machine/irq.h>
#include <machine/perfmon.h>
#include <kern/arch.h>
#include <kern/timer.h>
#include <kern/uinit.h>
#include <kern/sysmon.h>
#include <kern/processor.h>
#include <kern/device.h>
#include <dev/sercons.h>
#include <dev/cgacons.h>
#include <dev/sercons.h>
#include <dev/lapic.h>
#include <dev/kclock.h>
#include <dev/picirq.h>
#include <dev/ioapic.h>
#include <dev/acpi.h>
#include <dev/pci.h>
#include <dev/lptcons.h>
#include <inc/error.h>

struct spinlock debug_lock;

uint64_t ap_stacktop;

static volatile int reinit_done;

// .data persists across reinit
static struct sysx_info sxi_copy __attribute__((section (".data")));
static uint64_t lower_kb __attribute__((section (".data")));
static uint64_t upper_kb __attribute__((section (".data")));
char boot_cmdline[256] __attribute__((section (".data")));
char boot_args[PGSIZE] __attribute__((section (".data")));

static void
flush_tlb_hard(void)
{
    uint64_t cr3 = rcr3();
    uint64_t cr4 = rcr4();

    lcr4(cr4 & ~CAST64(CR4_PGE));
    lcr3(cr3);
    lcr4(cr4);
}

static void
seg_init(void)
{
    uint32_t i = arch_cpu();
    
    /* Move gdt to kernel memory and reload */
    gdtdesc[i].pd_base = (uintptr_t) &gdt[i];
    lgdt(&gdtdesc[i].pd_lim);
    
    /* Load TSS */
    gdt[i][(GD_TSS >> 3)] = (SEG_TSSA | SEG_P | SEG_A | SEG_BASELO (&tss[i])
			  | SEG_LIM (sizeof (tss[i]) - 1));
    gdt[i][(GD_TSS >> 3) + 1] = SEG_BASEHI (&tss[i]);
    ltr(GD_TSS);
}

static void
bss_init(void)
{
    extern char ebss[], sbss[];
    memset(sbss, 0, ebss - sbss);
}

static void
bootothers(void)
{
    extern uint8_t _binary_boot_bootother_start[],
	_binary_boot_bootother_size[];
    uint64_t size = (uint64_t) _binary_boot_bootother_size;

    if (size > PGSIZE - 4)
	panic("bootother too big: %lu\n", size);

    uint8_t *code = pa2kva(APBOOTSTRAP);
    memmove(code, _binary_boot_bootother_start, size);

    for (struct cpu * c = cpus; c < cpus + ncpu; c++) {
	if (c == cpus + arch_cpu())	// We've started already.
	    continue;

	ap_stacktop = KSTACKTOP(c - cpus);

	// Pass %eip and pgmap to the 32-bit bootstrap code
	*(uint32_t *) (code - 4) = (uint32_t) RELOC(start_ap);
	*(uint32_t *) (code - 8) = (uint32_t) kva2pa(&bootpml4[c - cpus]);
	lapic_startap(c->apicid, APBOOTSTRAP);

	// Wait for cpu to get through bootstrap.
	while (c->booted == 0) ;
    }
    // Don't need the APBOOTSTRAP page any more
    page_free(code);
}

void __attribute__ ((noreturn))
init_ap(void)
{
    // Nuke identical physical mappings
    bootpml4[arch_cpu()].pm_ent[0] = 0;
    flush_tlb_hard();
    lidt(&idtdesc.pd_lim);

    mtrr_ap_init();
    lapic_init();
    seg_init();
    perfmon_init();

    cpuid(0, 0, 0, 0, 0);	// memory barrier
    cpus[arch_cpu()].booted = 1;

    while (cpus[arch_bcpu()].booted == 0) ;

    processor_run();
    abort();
}

static void
init_bsp(void)
{
    idt_init();
    sercons_init();
    lptcons_init();
    cgacons_init();
    mtrr_init();
    acpi_init();		// Just SRAT
    mem_init(lower_kb, upper_kb, sxi_copy.e820_map, sxi_copy.e820_nents);

    mp_init();
    numa_init();
    page_init();
    cpu_init_pmaps();
    seg_init();

    pic_init();
    ioapic_init();
    irq_init();
    pit_init();
    lapic_init();

    pci_init();
    perfmon_init();
    sysmon_init();
}

void __attribute__ ((noreturn))
init(uint32_t start_eax, uint32_t start_ebx)
{
    bss_init();
    memset(boot_args, 0, array_size(boot_args));
    spin_init(&debug_lock);

    struct sysx_info *sxi = 0;

    if (start_eax == MULTIBOOT_EAX_MAGIC) {
	struct multiboot_info *mbi =
	    (struct multiboot_info *) pa2kva(start_ebx);

	if ((mbi->flags & MULTIBOOT_INFO_CMDLINE)) {
	    char *cmdline = pa2kva(mbi->cmdline);
	    strncpy(&boot_cmdline[0], cmdline, sizeof(boot_cmdline) - 1);
	}

	if ((mbi->flags & MULTIBOOT_INFO_MEMORY)) {
	    lower_kb = mbi->mem_lower;
	    upper_kb = mbi->mem_upper;
	}
    }

    if (start_eax == SYSXBOOT_EAX_MAGIC) {
	// Make a copy of sysx_info because we free low mem
	memcpy(&sxi_copy, pa2kva(start_ebx), sizeof(sxi_copy));
	sxi = &sxi_copy;

	char *cmdline = pa2kva(sxi->cmdline);
	strncpy(&boot_cmdline[0], cmdline, sizeof(boot_cmdline) - 1);
	upper_kb = sxi->extmem_kb;
    }
    // Our boot sector passes in the upper memory size this way
    if (start_eax == DIRECT_BOOT_EAX_MAGIC)
	upper_kb = start_ebx;

    init_bsp();
    bootothers();

    // Nuke identical physical mappings and load CPU pgmap.
    bootpml4[arch_cpu()].pm_ent[0] = 0;
    lcr3(kva2pa(&bootpml4[arch_cpu()]));
    flush_tlb_hard();

    user_init();
    cpus[arch_cpu()].booted = 1;
    cprintf("=== josmp ready, calling processor_run ===\n");
    processor_run();
}

static void __attribute__((noreturn))
reinit_helper(void)
{
    pmap_set_current(0);
    reinit_done = 0;
    cpus[arch_cpu()].booted = 0;

    if (arch_cpu()) {
	while (!reinit_done)
	    arch_pause();
	init_ap();
    }

    for (struct cpu * c = cpus; c < cpus + ncpu; c++)
	while(c->booted)
	    arch_pause();

    picirq_setmask_8259A(0xFFFF & ~(1<<IRQ_SLAVE));
    device_shutdown();

    bss_init();
    spin_init(&debug_lock);

    init_bsp();

    reinit_done = 1;
    user_init();
    cpus[arch_cpu()].booted = 1;

    cprintf("=== josmp ready, calling processor_run ===\n");
    processor_run();
}

void
reinit_local(void)
{
    // If responding to an NMI we need to execute an iret to re-enable NMIs.
    struct Trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.tf_rip = (uint64_t)reinit_helper;
    tf.tf_rflags = 0;
    tf.tf_cs = read_cs();
    tf.tf_ss = read_ss();
    tf.tf_ds = read_ds();
    tf.tf_es = read_es();
    tf.tf_fs = read_fs();
    tf.tf_gs = read_gs();
    // AMD64 ABI requires 16-byte alignment before "call" instruction
    tf.tf_rsp = KSTACKTOP(arch_cpu()) - 8;
    memset((void *)tf.tf_rsp, 0, 8);
    
    trapframe_pop(&tf);
}

void
arch_reinit(void)
{
    arch_broadcast(0, T_NMI);
    reinit_local();
}
