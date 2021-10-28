#ifndef JOS_MACHINE_PMAP_H
#define JOS_MACHINE_PMAP_H

#ifdef JOS_KERNEL
#include <machine/mmu.h>
#include <machine/memlayout.h>
#include <machine/boot.h>
#ifndef __ASSEMBLER__
#include <kern/lib.h>
#include <inc/intmacro.h>
#endif /* !__ASSEMBLER__ */
#endif /* JOS_KERNEL */

#if !defined(__ASSEMBLER__) && defined(JOS_KERNEL)
typedef uint64_t ptent_t;

struct Pagemap {
    ptent_t pm_ent[NPTENTRIES];
};

/* bootdata.c */
extern struct Pagemap bootpml4[JOS_NCPU];

extern struct Tss tss[JOS_NCPU];
extern uint64_t gdt[JOS_NCPU][7];
extern struct Pseudodesc gdtdesc[JOS_NCPU];
extern struct Gatedesc idt[0x100];
extern struct Pseudodesc idtdesc;

/* mtrr.c */
int  mtrr_set(physaddr_t base, uint64_t nbytes, uint32_t type)
    __attribute__ ((warn_unused_result));
void mtrr_init(void);
void mtrr_ap_init(void);

/* pmap.c */
void pmap_set_current_arch(struct Pagemap *pm);

/* page.c */
void mem_init(uint64_t lower_kb, uint64_t upper_kb, 
	      struct e820entry *map, uint8_t n);
void page_init(void);

struct Gdt 
{
    uint64_t null;
    uint64_t ktext;
    uint64_t tss0;
    uint64_t tss1;
    uint64_t udata;
    uint64_t ut_nmask;
    uint64_t ut_mask;
};
#endif /* !__ASSEMBLER__ && JOS_KERNEL */

#define GD_NULL     (0x00 | 0x00)       /* Null segment selector */
#define GD_KT	    (0x08 | 0x00)	/* Kernel text */
#define GD_TSS	    (0x10 | 0x00)	/* Task segment selector */
#define GD_TSS2	    (0x18 | 0x00)	/* TSS is a 16-byte descriptor */
#define GD_UD	    (0x20 | 0x03)	/* User data segment for iretq */
#define GD_UT_NMASK (0x28 | 0x03)	/* User text, traps not masked */
#define GD_UT_MASK  (0x30 | 0x03)	/* User text, traps masked */

#define KPDEP_BITS (PTE_P|PTE_W)
#define KPDE_BITS  (KPDEP_BITS|PTE_PS|PTE_G)
#define KPTE_BITS  (KPDEP_BITS|PTE_G)

#endif /* !JOS_MACHINE_PMAP_H */
