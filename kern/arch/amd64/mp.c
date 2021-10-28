// Intel MultiProcessor Specification:
// http://www.intel.com/design/pentium/datashts/24201606.pdf

#include <machine/trapcodes.h>
#include <machine/proc.h>
#include <machine/pmap.h>
#include <machine/mp.h>
#include <machine/x86.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>
#include <inc/error.h>

enum { mp_debug = 0 };

uint32_t ncpu;
struct cpu cpus[JOS_NCPU];
struct cpu *bcpu;

struct mp_fptr {
    uint8_t signature[4];	// "_MP_"
    uint32_t physaddr;		// phys addr of MP config table
    uint8_t length;		// 1
    uint8_t specrev;		// [14]
    uint8_t checksum;		// all bytes must add up to 0
    uint8_t type;		// MP system config type
    uint8_t imcrp;
    uint8_t reserved[3];
};

struct mp_conf {
    uint8_t signature[4];	// "PCMP"
    uint16_t length;		// total table length
    uint8_t version;		// [14]
    uint8_t checksum;		// all bytes must add up to 0
    uint8_t product[20];	// product id
    uint32_t oemtable;		// OEM table pointer
    uint16_t oemlength;		// OEM table length
    uint16_t entry;		// entry count
    uint32_t lapicaddr;		// address of local APIC
    uint16_t xlength;		// extended table length
    uint8_t xchecksum;		// extended table checksum
    uint8_t reserved;
};

struct mp_proc {
    uint8_t type;		// entry type (0)
    uint8_t apicid;		// local APIC id
    uint8_t version;		// local APIC verison
    uint8_t flags;		// CPU flags
    uint8_t signature[4];	// CPU signature
    uint32_t feature;		// feature flags from CPUID instruction
    uint8_t reserved[8];
};

struct mp_ioapic {
    uint8_t type;		// entry type (2)
    uint8_t apicno;		// I/O APIC id
    uint8_t version;		// I/O APIC version
    uint8_t flags;		// I/O APIC flags
    uint32_t addr;		// I/O APIC address
};

struct mp_bus {
    uint8_t type;		// entry type(1)
    uint8_t busid;		// bus id
    char busstr[6];		// string which identifies the type of this bus
};

struct mp_intr mp_iointr[256];
struct mp_intr mp_lintr[256];

#define MP_PROC    0x00		// One per processor
#define MP_BUS     0x01		// One per bus
#define MP_IOAPIC  0x02		// One per I/O APIC
#define MP_IOINTR  0x03		// One per bus interrupt source
#define MP_LINTR   0x04		// One per system interrupt source

#define IOAPICPA   0xFEC00000	// Default physical address of IO APIC

#define MP_FLAGS_BOOT    0x02	// This proc is the bootstrap processor.

static uint8_t
sum(uint8_t * a, uint32_t length)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < length; i++)
	s += a[i];
    return s;
}

static struct mp_fptr *
mp_search1(physaddr_t pa, int len)
{
    uint8_t *start = (uint8_t *) pa2kva(pa);
    for (uint8_t * p = start; p < (start + len); p += sizeof(struct mp_fptr)) {
	if ((memcmp(p, "_MP_", 4) == 0)
	    && (sum(p, sizeof(struct mp_fptr)) == 0))
	    return (struct mp_fptr *) p;
    }
    return 0;
}

static struct mp_fptr *
mp_search(void)
{
    struct mp_fptr *ret;
    uint8_t *bda;
    physaddr_t pa;

    bda = (uint8_t *) pa2kva(0x400);
    if ((pa = ((bda[0x0F] << 8) | bda[0x0E]) << 4)) {
	if ((ret = mp_search1(pa, 1024)))
	    return ret;
    } else {
	pa = ((bda[0x14] << 8) | bda[0x13]) * 1024;
	if ((ret = mp_search1(pa - 1024, 1024)))
	    return ret;
    }
    return mp_search1(0xF0000, 0x10000);
}

static void
mp_print(void)
{
    cprintf("mp table:\n");
    for (uint32_t i = 0; i < ncpu; i++)
	cprintf(" cpu: %4u apic id: %4u\n", i, cpus[i].apicid);
    cprintf(" local apic va: %lx\n", (uint64_t) lapic);

    cprintf("iointr table:\n");
    for (uint32_t i = 0; mp_iointr[i].type; i++)
	cprintf(" source: %4u irq: %4u type: %4u flag: 0x%04x"
		" dest id: %4u dest pin: %4u\n",
		mp_iointr[i].src_bus_id, mp_iointr[i].src_bus_irq,
		mp_iointr[i].intr_type, mp_iointr[i].intr_flag,
		mp_iointr[i].dst_id, mp_iointr[i].dst_intin);

    cprintf(" ioapic id: %u\n", ioapicid);

    cprintf("lintr table:\n");
    for (uint32_t i = 0; mp_lintr[i].type; i++)
	cprintf(" source: %4u irq: %4u type: %4u flag: 0x%04x"
		" dest id: %4u dest pin: %4u\n",
		mp_lintr[i].src_bus_id, mp_lintr[i].src_bus_irq,
		mp_lintr[i].intr_type, mp_lintr[i].intr_flag,
		mp_lintr[i].dst_id, mp_lintr[i].dst_intin);
}

uint32_t
arch_cpu(void)
{
    return (KSTACKTOP(0) - read_rsp()) / (3 * PGSIZE);
}

uint32_t
arch_bcpu(void)
{
    if (ncpu == 1)
	return 0;

    assert(bcpu);
    return bcpu - cpus;
}

int
arch_ipi(proc_id_t pid, uint32_t ino)
{
    if (pid >= ncpu)
	return -E_INVAL;

    return lapic_ipi(cpus[pid].apicid, ino);
}

int
arch_broadcast(int self, uint32_t ino)
{
   return lapic_broadcast(self, ino);
}

void
arch_tlbflush_mp(proc_id_t pid)
{
    assert(arch_ipi(pid, T_TLBFLUSH) == 0);
}

void
arch_halt_mp(proc_id_t pid)
{
    assert(arch_ipi(pid, T_HALT) == 0);    
}

void
mp_init(void)
{
    // default values
    bcpu = &cpus[0];
    ncpu = 1;

    struct mp_fptr *fptr = mp_search();
    if (!fptr) {
	cprintf("mp_init: no MP floating pointer found\n");
	return;
    }
    if (fptr->physaddr == 0 || fptr->type != 0) {
	cprintf("mp_init: legacy MP configurations not supported\n");
	return;
    }

    struct mp_conf *conf = pa2kva(fptr->physaddr);
    if ((memcmp(conf->signature, "PCMP", 4) != 0) ||
	(sum((uint8_t *) conf, conf->length) != 0) ||
	(conf->version != 1 && conf->version != 4)) {
	cprintf("mp_init: bad or unsupported configuration\n");
	return;
    }
    ncpu = 0;

    int r = mtrr_set(conf->lapicaddr, PGSIZE, MTRR_BASE_UC);
    if (r < 0)
	cprintf("mp_init: out of MTRRs, lapic might not work..\n");
    lapic = pa2kva(conf->lapicaddr);

    struct mp_proc *proc;
    struct mp_ioapic *ioapic_conf = 0;
    struct mp_bus *bus = 0;
    uint8_t niointr = 0;
    uint8_t nlintr = 0;

    char apicid_avail[JOS_NCPU * 4];
    memset(apicid_avail, 0, sizeof(apicid_avail));
    uint32_t bapicid = 0;

    uint8_t *start = (uint8_t *) (conf + 1);
    for (uint8_t * p = start; p < ((uint8_t *) conf + conf->length);) {
	switch (*p) {
	case MP_PROC:
	    proc = (struct mp_proc *) p;
	    p += sizeof(*proc);
	    apicid_avail[proc->apicid] = 1;
	    if (mp_debug)
		cprintf("mp_init: apic id %d detected\n", proc->apicid);
	    if (proc->flags & MP_FLAGS_BOOT)
		bapicid = proc->apicid;
	    continue;
	case MP_IOAPIC:
	    ioapic_conf = (struct mp_ioapic *) p;
	    ioapicid = ioapic_conf->apicno;
	    p += sizeof(*ioapic_conf);
	    if (mp_debug)
		cprintf("mp_init: io apic flag %d, apic no %d\n",
			ioapic_conf->flags, ioapic_conf->apicno);
	    continue;
	case MP_IOINTR:
	    memcpy(&mp_iointr[niointr], p, 8);
	    niointr++;
	    p += 8;
	    continue;
	case MP_LINTR:
	    memcpy(&mp_lintr[nlintr], p, 8);
	    nlintr++;
	    p += 8;
	    continue;
	case MP_BUS:
	    bus = (struct mp_bus *) p;
	    if (mp_debug) {
		cprintf("mp_init: bus id %d, bus name %c%c%c%c%c%c\n",
			bus->busid, bus->busstr[0], bus->busstr[1],
			bus->busstr[2], bus->busstr[3], bus->busstr[4],
			bus->busstr[5]);
	    }
	    p += 8;
	    continue;
	default:
	    panic("unknown config type %x\n", *p);
	}
    }

    if (fptr->imcrp) {
	// force NMI and 8259 signals to APIC
	outb(0x22, 0x70);
	outb(0x23, 0x01);
    }
    for (uint32_t i = 0; i < array_size(apicid_avail); i++) {
	if (apicid_avail[i]) {
	    if (ncpu == JOS_NCPU) {
		cprintf("mp_init: ignoring CPU\n");
		break;
	    }

	    cpus[ncpu].cpuid = ncpu;
	    cpus[ncpu].apicid = i;
	    if (bapicid == i)
		bcpu = &cpus[ncpu];
	    ncpu++;
	}
    }

    physaddr_t ioapicpa;
    if (ioapic_conf)
	ioapicpa = ioapic_conf->addr;
    else
	ioapicpa = IOAPICPA;

    r = mtrr_set(ioapicpa, PGSIZE, MTRR_BASE_UC);
    if (r < 0)
	cprintf("mp_init: out of MTRRs, ioapic might not work..\n");
    ioapicva = pa2kva(ioapicpa);

    if (mp_debug)
	mp_print();
}
