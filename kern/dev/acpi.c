// Advanced Configuration and Power Interface Specification 3.0b:
// http://www.acpi.info/DOWNLOADS/ACPIspec30b.pdf

#include <kern/arch.h>
#include <kern/lib.h>
#include <dev/acpi.h>
#include <inc/error.h>

enum { node_print = 0 };

struct rsdp {
    uint8_t signature[8];
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t revision;
    uint32_t rsdtaddr;
    uint32_t length;
    uint64_t xsdtaddr;
    uint8_t extchecksum;
    uint8_t reserved[3];
};

struct header {
    uint8_t signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t oemtableid[8];
    uint32_t oemrevision;
    uint32_t creatorid;
    uint32_t creatorrevision;
};

struct rsdt {
    struct header hdr;
    uint32_t entry[];
};

struct xsdt {
    struct header hdr;
    uint64_t entry[];
} __attribute__((packed));

struct srat_apicaff {
    uint8_t type;
    uint8_t length;
    uint8_t proxdomain0;
    uint8_t apicid;
    uint32_t flags;
    uint8_t sapiceid;
    uint8_t proxdomain1[3];
    uint8_t reserved;
} __attribute__((packed));

struct srat_memaff {
    uint8_t type;
    uint8_t length;
    uint32_t proxdomain;
    uint16_t reserved0;
    uint64_t membaseaddr;
    uint64_t memlength;
    uint32_t reserved1;
    uint32_t flags;
    uint64_t reserved2;
} __attribute__((packed));

struct srat {
    struct header hdr;
    uint8_t reserved[12];
    uint8_t entry[];
} __attribute__((packed));

struct memory_node node[JOS_NNODE];
uint32_t           node_count;

static uint8_t
sum(uint8_t *a, uint32_t length)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < length; i++)
	s += a[i];
    return s;
}

static struct rsdp *
rsdp_search1(physaddr_t pa, int len)
{
    uint8_t *start = (uint8_t *)pa2kva(pa);
    for (uint8_t *p = start; p < (start + len); p += 16) {
	if ((memcmp(p, "RSD PTR ", 8) == 0) && (sum(p, 20) == 0))
	    return (struct rsdp *)p;
    }
    return 0;
}

static struct rsdp *
rsdp_search(void)
{
    struct rsdp *ret;
    uint8_t *bda;
    physaddr_t pa;
    
    bda = (uint8_t *)pa2kva(0x400);
    if ((pa = ((bda[0x0F] << 8) | bda[0x0E]) << 4)) {
	if ((ret = rsdp_search1(pa, 1024)))
	    return ret;
    } 
    return rsdp_search1(0xE0000, 0x20000);
}

static void __attribute__((unused))
xsdt_print(struct xsdt *xsdt)
{
    cprintf("xsdt: \n");
    uint32_t n = (xsdt->hdr.length - sizeof(*xsdt)) / 8;
    for (uint32_t i = 0; i < n; i++) {
	struct header *h = pa2kva(xsdt->entry[i]);
	cprintf(" %u 0x%lx %c%c%c%c\n", i, xsdt->entry[i],
		h->signature[0], h->signature[1], h->signature[2],
		h->signature[3]);
    }
}

int
acpi_node_get(struct memory_node *mn, uint32_t n)
{
    if (!node_count)
	return -E_NOT_FOUND;
    if (n < node_count) 
	return -E_NO_SPACE;

    memcpy(mn, node, sizeof(*mn) * node_count);
    return node_count;
}

static void
acpi_srat_read(struct srat *srat)
{
    /* Assume that APIC ids go from 0 to ncpu - 1.  Also assume that 
     * memory affinity ranges are not interleaved.  The ACPI spec. does 
     * NOT make this guarantee.  Additionally we merge all memory affinity
     * ranges beloning to the same node, which may cause a memory_node to
     * include a range of the physical AS not mapped to memory.
     */

    uint32_t n = srat->hdr.length - sizeof(*srat);
    int32_t max = -1;

    for (uint32_t i = 0; i < n; ) {
	uint8_t *ptr = &srat->entry[i];
	if (*ptr) {
	    struct srat_memaff *m = (struct srat_memaff *) ptr;
	    if (m->flags & 0x1) {
		if (m->proxdomain >= JOS_NNODE)
		    cprintf("srat_read: bad proxdomin %u\n", m->proxdomain);
		else {
		    if (node[m->proxdomain].length)
			node[m->proxdomain].length = 
			    (m->membaseaddr + m->memlength) - 
			    node[m->proxdomain].baseaddr;
		    else {
			node[m->proxdomain].baseaddr = m->membaseaddr;
			node[m->proxdomain].length = m->memlength;
		    }
		    if ((int32_t)m->proxdomain > max)
			max = m->proxdomain;
		}
	    }
	    i += m->length;
	} else {
	    struct srat_apicaff *a = (struct srat_apicaff *) ptr;
	    if (a->flags & 0x1) {
		uint32_t prox = a->proxdomain0 | (a->proxdomain1[0] << 8) | 
		    (a->proxdomain1[1] << 16) | (a->proxdomain1[2] << 24) ;
		if (prox >= JOS_NNODE)
		    cprintf("srat_read: bad proxdomin %u\n", prox);
		else
		    node[prox].cpu[a->apicid].p = 1;
	    }
	    i += a->length;
	}
    }

    if (max < 0) {
	cprintf("srat_read: invalid table\n");
	return;
    }
    node_count = max + 1;

    if (node_print)
	for (uint32_t i = 0; i < node_count; i++) {
	    cprintf(" %016lx - %016lx  ", node[i].baseaddr, 
		    node[i].baseaddr + node[i].length);
	    for (uint32_t j = 0; j < JOS_NCPU; j++)
		cprintf("%u", node[i].cpu[j].p);
	    cprintf("\n");
	}
}

void
acpi_init(void)
{
    struct srat *srat = 0;
    struct rsdp *rsdp = rsdp_search();
    
    if (!rsdp)
	return;

    if (rsdp->xsdtaddr) {
	struct xsdt *xsdt = pa2kva(rsdp->xsdtaddr);
	if (sum((uint8_t *)xsdt, xsdt->hdr.length)) {
	    cprintf("acpi_init: bad xsdt checksum\n");
	    return;
	}

	uint32_t n = xsdt->hdr.length > sizeof(*xsdt) ? 
	    (xsdt->hdr.length - sizeof(*xsdt)) / 8 : 0;
	for (uint32_t i = 0; i < n; i++) {
	    struct header *h = pa2kva(xsdt->entry[i]);
	    if (memcmp(h->signature, "SRAT", 4) == 0)
		srat = (struct srat *)h;
	}
    } else {
	struct rsdt *rsdt = pa2kva(rsdp->rsdtaddr);
	if (sum((uint8_t *)rsdt, rsdt->hdr.length)) {
	    cprintf("acpi_init: bad rsdt checksum\n");
	    return;
	}

	uint32_t n = rsdt->hdr.length > sizeof(*rsdt) ? 
	    (rsdt->hdr.length - sizeof(*rsdt)) / 8 : 0;
	for (uint32_t i = 0; i < n; i++) {
	    struct header *h = pa2kva(rsdt->entry[i]);
	    if (memcmp(h->signature, "SRAT", 4) == 0)
		srat = (struct srat *)h;
	}
    }

    if (srat)
	acpi_srat_read(srat);
}
