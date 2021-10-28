#ifndef JOS_MACHINE_MEMLAYOUT_H
#define JOS_MACHINE_MEMLAYOUT_H

#include <machine/mmu.h>

/*
 * Virtual memory map
 *
 *   2^64 --------->  +---------------------------------------------+
 *                    |                                             |
 *                    |   Kernel memory (all symbols point here)    |
 *                    |                                             |
 *   KERNBASE ----->  +---------------------------------------------+
 *   KSTACKTOP ---->  +---------------------------------------------+
 *                    |          Kernel stack CPU 0 (2 pages)       |
 *                    +---------------------------------------------+
 *                    |                Unmapped page                |
 *		      +---------------------------------------------+
 *                    |                      :                      |
 *                    |                      :                      |
 *                    |                      :                      |
 *		      +---------------------------------------------+
 *                    |        Kernel stack CPU N-1 (2 pages)       |
 *                    +---------------------------------------------+
 *                    |                Unmapped page                |
 *		      +---------------------------------------------+
 *                    |               Unmapped region               |
 *                    |                      :                      |
 *                    |                      :                      |
 *                    +---------------------------------------------+
 *                    |                                             |
 *                    |     All of physical memory mapped here      |
 *                    |                                             |
 *   PHYSBASE ----->  +---------------------------------------------+
 *                    |                                             |
 *                    |           2^47 to (2^64 - 2^47)             |
 *                    |         invalid virtual addresses           |
 *                    |                                             |
 *   ULIM --------->  +---------------------------------------------+
 *                    |                 user stack                  |
 *                    |                 user data                   |
 *                    |                 user text                   |
 *   0 ------------>  +---------------------------------------------+
 */

/* gcc requires that the code resides either in the bottom 2GB of the
 * virtual address space (-mcmodel=small, medium) or the top 2GB of
 * the address space (-mcmodel=kernel).  Unfortunately this means
 * that we need to duplicate the physical memory somewhere else if
 * we want to access more than 2GB of physical memory.
 */

/* AMD64 currently supports 52-bit physical addresses and 48-bit
 * virtual addresses.  We need to make sure all addresses are in 
 * canonical address form: "An address is in canonical form if the
 * address bits from the most-significant implemented bit up to bit
 * 63 are all ones or all zeros." (AMD Arch. Manual V2, 5.3.1)
 */
                                   
#define KERNBASE	UINT64 (0xffffffff80000000)
#define RELOC(x)	(CAST64 (x) - KERNBASE)

#define PHYSBASE	UINT64 (0xffff800000000000)
#define KSTACKTOP(cpu)	((KERNBASE - PGSIZE) - ((cpu) * 3 * PGSIZE))

#define ULIM		UINT64 (0x0000800000000000)

/* At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
 * IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
 * at physical address EXTPHYSMEM.
 */
#define IOPHYSMEM	0x0A0000
#define VGAPHYSMEM	0x0A0000
#define DEVPHYSMEM	0x0C0000
#define BIOSPHYSMEM	0x0F0000
#define EXTPHYSMEM	0x100000

/* User-mode (below ULIM) address space layout conventions. */
#define UASMANAGERBASE	UINT64 (0x0000000100000000)
#define UASMANAGEREND	UINT64 (0x0000300100000000)
#define ULINKSTART	UINT64 (0x0000300100000000)
#define ULINKEND	UINT64 (0x0000400100000000)
#define UMMAPSTART	UINT64 (0x0000400100000000)
#define UMMAPEND	UINT64 (0x0000500100000000)
#define UFDBASE		UINT64 (0x0000500100000000)
#define UHEAP		UINT64 (0x0000600200000000)
#define UHEAPTOP	UINT64 (0x0000600300000000)
#define USTACKTOP	UINT64 (0x0000700000000000)

#define USTACKSTART     UINT64 (0x0000700400000000)
#define USTACKEND       UINT64 (0x0000700500000000)
#define UTHREADSTART    UINT64 (0x0000700500000000)
#define UTHREADEND      UINT64 (0x0000700501000000)

#define UADDRMAPENTS	UINT64 (0x0000700600000000)
#define UADDRMAPENTSEND	UINT64 (0x0000700601000000)

#define UASTABLE	UINT64 (0x00007FFFFE000000)
#define UTHREADTABLE	UINT64 (0x00007FFFFF000000)
#define UBOOTARGS	UINT64 (0x00007FFFFFFE0000)
#define ULIBCONF	UINT64 (0x00007FFFFFFF0000)

#endif
