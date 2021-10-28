#ifndef JOS_KERN_ARCH_H
#define JOS_KERN_ARCH_H

#include <machine/types.h>
#include <machine/param.h>
#include <machine/pmap.h>
#include <machine/memlayout.h>
#include <machine/proc.h>
#include <inc/context.h>

struct Processor;
struct Address_tree;

/*
 * Page table (Pagemap) handling
 */
int  page_map_alloc(struct Pagemap **pm_store, int interior, proc_id_t pid)
     __attribute__ ((warn_unused_result));
void page_map_free(struct Pagemap *pm);

/* Traverse [first .. last]; clamps last down to ULIM-PGSIZE */
typedef void (*page_map_traverse_cb)(const void *arg, ptent_t *ptep, void *va);
#define PM_TRAV_CREATE    0x01
#define PM_TRAV_LINK	  0x02
void page_map_invalidate(struct Pagemap *pgmap, const void *first, const void *last);

/*
 * Page map manipulation
 */
void pmap_set_current(struct Pagemap *pm);
int  as_arch_putpage(struct Pagemap *pmap, void *va, void *pp, uint32_t flags,
                     proc_id_t pid);

int  as_arch_putinterior(struct Pagemap *pmap, void *va, struct Pagemap *pimap, 
			 proc_id_t pid);

int  check_user_access(struct Address_tree *at, const void *ptr, 
		       uint64_t nbytes, uint32_t reqflags)
     __attribute__ ((warn_unused_result));

/* Invalidates TLB entries, clears page-table entries */
void as_arch_page_invalidate_cb(const void *arg, ptent_t *ptep, void *va);

/* Physical address handling */
void *pa2kva(physaddr_t pa);
physaddr_t kva2pa(void *kva);
ppn_t pa2ppn(physaddr_t pa);
physaddr_t ppn2pa(ppn_t pn);

/*
 * Miscellaneous
 */
extern char boot_cmdline[];
extern char boot_args[];
void machine_reboot(void) __attribute__((noreturn));
uint64_t karch_get_tsc(void);
void arch_pause(void);
void irq_arch_enable(uint8_t irqno, proc_id_t pid);
void irq_arch_disable(uint8_t irqno, proc_id_t pid);
void trapframe_print(const struct Trapframe *tf);

void arch_fp_init(struct Fpregs *fpregs);

void arch_reinit(void) __attribute__((noreturn));

/*
 * MultiProcessor
 */
uint32_t arch_cpu(void);
uint32_t arch_bcpu(void);
void     arch_locality_fill(struct u_locality_matrix *ulm);
uint8_t  arch_node_by_addr(uintptr_t p);
uint8_t  arch_node_by_cpu(proc_id_t p);
int	 arch_ipi(proc_id_t pid, uint32_t ino);
int	 arch_broadcast(int self, uint32_t ino);
void	 arch_tlbflush_mp(proc_id_t pid);
void	 arch_halt_mp(proc_id_t pid);

extern struct memory_node memnode[];
extern uint8_t nmemnode;

extern uint32_t ncpu;
extern struct cpu *bcpu;   // Bootstrap CPU
extern struct cpu cpus[];

int  processor_arch_vector(struct Processor *ps, const struct u_context *cx)
     __attribute__ ((warn_unused_result));
void processor_arch_run(struct Processor *ps)
     __attribute__((__noreturn__));
int  processor_arch_utrap(struct Processor *ps, uint32_t trapno, uint64_t arg)
     __attribute__((warn_unused_result));
int  processor_arch_pending(struct Processor *ps, uint32_t trapno)
     __attribute__((warn_unused_result));
int  processor_arch_is_masked(struct Processor *ps) 
     __attribute__((warn_unused_result));

#endif
