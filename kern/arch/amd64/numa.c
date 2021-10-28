#include <machine/param.h>
#include <machine/proc.h>
#include <machine/numa.h>
#include <kern/arch.h>
#include <dev/acpi.h>
#include <inc/error.h>

enum { fake_nodes = 1 };

struct memory_node memnode[JOS_NNODE];
uint8_t nmemnode;

void
numa_init(void)
{
    int r;
    int faked = 0;
    if (fake_nodes) {
	// Fakes ncpu nodes each with one CPU
	uint64_t size = global_npages * 4096 / ncpu;
	uint64_t baseaddr = 0;

    memnode[0].baseaddr = 0;
    memnode[0].cpu[0].p = 1;
    /*
     * we assume the discontigous memory all lies in cpu 0.
     */
    if (ncpu > 1 && size < largest_ram_start) {
        /* assign all non-discongious memory to cpu 0 */
        memnode[0].length = largest_ram_start;
        size = (global_npages * 4096 - largest_ram_start) / (ncpu -1);
        baseaddr = largest_ram_start;
    }
    else {
        memnode[0].length = size;
        baseaddr = size;
    }
	for (uint32_t i = 1; i < ncpu; i++, baseaddr += size) {
	    memnode[i].baseaddr = baseaddr;
	    memnode[i].length = size;
	    memnode[i].cpu[i].p = 1;
	}
	nmemnode = ncpu;
	faked = 1;
    } else if ((r = acpi_node_get(memnode, JOS_NNODE)) > 0) {
	nmemnode = r;
    } else {
        memnode[0].baseaddr = 0;
	memnode[0].length = global_npages << PGSHIFT;
	for (uint32_t i = 0; i < ncpu; i++)
	    memnode[0].cpu[i].p = 1;
	nmemnode = 1;
    }

    for (int i = 0; i < nmemnode; i++)
	for (uint32_t j = 0; j < ncpu; j++)
	    if (memnode[i].cpu[j].p)
		cpus[j].nodeid = i;

    if (faked)
	cprintf("numa_init faked:\n");
    else
	cprintf("numa_init:\n");

    for (int i = 0; i < nmemnode; i++) {
	cprintf(" %016lx - %016lx  ", memnode[i].baseaddr, 
		memnode[i].baseaddr + memnode[i].length);
	for (uint32_t j = 0; j < JOS_NCPU; j++)
	    cprintf("%u", memnode[i].cpu[j].p);
	cprintf("\n");
    }
    cprintf(" %u cpus total\n", ncpu);
}

void
arch_locality_fill(struct u_locality_matrix *ulm)
{
    for (int i = 0; i < nmemnode; i++)
	for (uint32_t j = 0; j < ncpu; j++)
	    if (memnode[i].cpu[j].p) {
		for (uint32_t k = 0; k < ncpu; k++)
		    ulm->distance[j][k] = memnode[i].cpu[k].p ? 1 : 2;
		ulm->distance[j][j] = 0;
	    }
    
    ulm->ncpu = ncpu;
}

uint8_t
arch_node_by_addr(uintptr_t p) 
{
    for (uint8_t i = 0; i < nmemnode; i++) {
	struct memory_node *n = &memnode[i];
	if (n->baseaddr <= p && p < n->baseaddr + n->length)
	    return i;
    }
    panic("0x%lx out-of-range", p);
}

uint8_t
arch_node_by_cpu(proc_id_t p) 
{
    return cpus[p].nodeid;
}
