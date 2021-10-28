#include <machine/proc.h>
#include <machine/pmap.h>
#include <kern/arch.h>

/*
 * Boot page tables
 */

#define PTATTR __attribute__ ((aligned (4096), section (".data")))

#define DO_2(_start, _macro)						\
  _macro (((_start) + 0)) _macro (((_start) + 1))

#define DO_4(_start, _macro)						\
  DO_2 ((_start) + 0, _macro) DO_2 ((_start) + 2, _macro)

#define DO_8(_start, _macro)						\
  DO_4 ((_start) + 0, _macro) DO_4 ((_start) + 4, _macro)

#define DO_16(_start, _macro)						\
  DO_8 ((_start) + 0, _macro) DO_8 ((_start) + 8, _macro)

#define DO_32(_start, _macro)						\
  DO_16 ((_start) + 0, _macro) DO_16 ((_start) + 16, _macro)

#define DO_64(_start, _macro)						\
  DO_32 ((_start) + 0, _macro) DO_32 ((_start) + 32, _macro)

#define DO_128(_start, _macro)						\
  DO_64 ((_start) + 0, _macro) DO_64 ((_start) + 64, _macro)

#define DO_256(_start, _macro)						\
  DO_128 ((_start) + 0, _macro) DO_128 ((_start) + 128, _macro)

#define DO_512(_start, _macro)						\
  DO_256 ((_start) + 0, _macro) DO_256 ((_start) + 256, _macro)

#define TRANS2MB(n) (0x200000UL * (n) | KPDE_BITS), 
#define TRANS4KB(n) (0x1000UL * (n) | KPTE_BITS),

#define PD_1GB(n) \
    struct Pagemap bootpd##n PTATTR = { .pm_ent = { DO_512 (512 * (n), TRANS2MB) } }

#define MAP_1GB(n) \
    RELOC (&bootpd##n) + KPDEP_BITS

#define DO_2CPU(_start, _macro)						\
  _macro (((_start) + 0)) _macro (((_start) + 1))

#define DO_4CPU(_start, _macro)						\
  DO_2CPU ((_start) + 0, _macro) DO_2CPU ((_start) + 2, _macro)

#if JOS_NCPU == 1
#define DO_NCPU(_macro) _macro((0))
#elif JOS_NCPU == 2
#define DO_NCPU(_macro) DO_2CPU(0, _macro)
#elif JOS_NCPU == 4
#define DO_NCPU(_macro) DO_4CPU(0, _macro)
#elif JOS_NCPU == 16
#define DO_NCPU(_macro)							\
  DO_4CPU(0, _macro) DO_4CPU(4, _macro)					\
  DO_4CPU(8, _macro) DO_4CPU(12, _macro)
#else
#error unknown JOS_NCPU
#endif    

/* bootpd0-65 identically map the first 66 GBs of physical address space. 
 * 1GB superpages are not supported by QEMU.
 */
PD_1GB(0);  PD_1GB(1);  PD_1GB(2);  PD_1GB(3);  PD_1GB(4);  PD_1GB(5); 
PD_1GB(6);  PD_1GB(7);  PD_1GB(8);  PD_1GB(9);  PD_1GB(10); PD_1GB(11);
PD_1GB(12); PD_1GB(13); PD_1GB(14); PD_1GB(15); PD_1GB(16); PD_1GB(17); 
PD_1GB(18); PD_1GB(19); PD_1GB(20); PD_1GB(21); PD_1GB(22); PD_1GB(23);
PD_1GB(24); PD_1GB(25); PD_1GB(26); PD_1GB(27); PD_1GB(28); PD_1GB(29);
PD_1GB(30); PD_1GB(31); PD_1GB(32); PD_1GB(33); PD_1GB(34); PD_1GB(35);
PD_1GB(36); PD_1GB(37); PD_1GB(38); PD_1GB(39); PD_1GB(40); PD_1GB(41);
PD_1GB(42); PD_1GB(43); PD_1GB(44); PD_1GB(45); PD_1GB(46); PD_1GB(47);
PD_1GB(48); PD_1GB(49); PD_1GB(50); PD_1GB(51); PD_1GB(52); PD_1GB(53);
PD_1GB(54); PD_1GB(55); PD_1GB(56); PD_1GB(57); PD_1GB(58); PD_1GB(59);
PD_1GB(60); PD_1GB(61); PD_1GB(62); PD_1GB(63); PD_1GB(64); PD_1GB(65); 

/* Page directory bootpds mapping the bootstrap stack (one page under KERNBASE) */
#define DATAATTR __attribute__ ((aligned (4096), section (".data")))

static char kstack[JOS_NCPU][2 * PGSIZE] DATAATTR;

#define KSTACK_MAPPING(cpu)						\
    [509 - ((cpu) * 3)] = RELOC (&kstack[cpu][0 * PGSIZE]) + KPTE_BITS, \
    [510 - ((cpu) * 3)]	= RELOC (&kstack[cpu][1 * PGSIZE]) + KPTE_BITS,


struct Pagemap bootpts PTATTR = {
  .pm_ent = {
      DO_NCPU(KSTACK_MAPPING)
  }
};

struct Pagemap bootpds PTATTR = {
  .pm_ent = {
    [511] = RELOC (&bootpts) + KPDEP_BITS, /* sic - KPDE_BITS has PS, G */
  }
};

#define BOOTPTS_KTEXT_INIT(cpu)						\
  {									\
    .pm_ent = { DO_512(0, TRANS4KB) }					\
  }, 

struct Pagemap bootpts_ktext[JOS_NCPU] PTATTR = {
    DO_NCPU(BOOTPTS_KTEXT_INIT)
};

#define BOOTPDS_KTEXT_INIT(cpu)						\
  { .pm_ent = {								\
      RELOC (&bootpts_ktext[(cpu)]) + KPDEP_BITS, TRANS2MB(1)		\
      DO_2   (2,   TRANS2MB)						\
      DO_4   (4,   TRANS2MB)						\
      DO_8   (8,   TRANS2MB)						\
      DO_16  (16,  TRANS2MB)						\
      DO_32  (32,  TRANS2MB)						\
      DO_64  (64,  TRANS2MB)						\
      DO_128 (128, TRANS2MB)						\
      DO_256 (256, TRANS2MB)						\
    }									\
  },

struct Pagemap bootpds_ktext[JOS_NCPU] PTATTR = {
    DO_NCPU(BOOTPDS_KTEXT_INIT)
};

/*
 * Map first 2GB identically at bottom of VM space (for booting).
 * Map first 2GB at KERNBASE (-2 GB), where the kernel will run.
 * Map first 66GB of physical address space at PHYSBASE.
 * Map the bootstrap stack right under KERNBASE.
 */
struct Pagemap bootpdplo PTATTR = {
  .pm_ent = {
      MAP_1GB(0),  MAP_1GB(1),  MAP_1GB(2),  MAP_1GB(3), 
      MAP_1GB(4),  MAP_1GB(5),  MAP_1GB(6),  MAP_1GB(7), 
      MAP_1GB(8),  MAP_1GB(9),  MAP_1GB(10), MAP_1GB(11), 
      MAP_1GB(12), MAP_1GB(13), MAP_1GB(14), MAP_1GB(15), 
      MAP_1GB(16), MAP_1GB(17), MAP_1GB(18), MAP_1GB(19),   
      MAP_1GB(20), MAP_1GB(21), MAP_1GB(22), MAP_1GB(23),   
      MAP_1GB(24), MAP_1GB(25), MAP_1GB(26), MAP_1GB(27),   
      MAP_1GB(28), MAP_1GB(29), MAP_1GB(30), MAP_1GB(31),   
      MAP_1GB(32), MAP_1GB(33), MAP_1GB(34), MAP_1GB(35),   
      MAP_1GB(36), MAP_1GB(37), MAP_1GB(38), MAP_1GB(39),   
      MAP_1GB(40), MAP_1GB(41), MAP_1GB(42), MAP_1GB(43),   
      MAP_1GB(44), MAP_1GB(45), MAP_1GB(46), MAP_1GB(47),   
      MAP_1GB(48), MAP_1GB(49), MAP_1GB(50), MAP_1GB(51),
      MAP_1GB(52), MAP_1GB(53), MAP_1GB(54), MAP_1GB(55),
      MAP_1GB(56), MAP_1GB(57), MAP_1GB(58), MAP_1GB(59),
      MAP_1GB(60), MAP_1GB(61), MAP_1GB(62), MAP_1GB(63),
      MAP_1GB(64), MAP_1GB(65),
  }
};

#define BOOTPDPHI_INIT(cpu)						\
  {  .pm_ent = {							\
       [509] = RELOC (&bootpds) + KPDEP_BITS,				\
       [510] = RELOC (&bootpds_ktext[(cpu)]) + KPDEP_BITS,		\
       MAP_1GB(1),							\
    }									\
  },

struct Pagemap bootpdphi[JOS_NCPU] PTATTR = {
  DO_NCPU(BOOTPDPHI_INIT)
};

#define BOOTPML4_INIT(cpu)						\
  {  .pm_ent = {							\
       RELOC (&bootpdplo) + KPDEP_BITS,					\
       [256] = RELOC (&bootpdplo) + KPDEP_BITS,				\
       [511] = RELOC (&bootpdphi[(cpu)]) + KPDEP_BITS,			\
     }									\
  },

struct Pagemap bootpml4[JOS_NCPU] PTATTR = {
  DO_NCPU(BOOTPML4_INIT)
};

/*
 * Boot segments
 */
#define TSS_INIT(cpu)							\
  { .tss_rsp = { KSTACKTOP(cpu), KERNBASE, KERNBASE },			\
    .tss_iomb = offsetof (struct Tss, tss_iopb),			\
  },
    
struct Tss tss[JOS_NCPU] = {
  DO_NCPU(TSS_INIT)
};

#define GDT_INIT(cpu)							\
  { [GD_KT  >> 3] = SEG64 (SEG_X|SEG_R, 0),				\
    [GD_TSS >> 3]	= 0, 0,						\
    [GD_UD  >> 3] = SEG64 (SEG_W, 3),					\
    [GD_UT_NMASK >> 3] = SEG64 (SEG_X|SEG_R, 3),			\
    [GD_UT_MASK  >> 3] = SEG64 (SEG_X|SEG_R, 3),			\
  },

uint64_t gdt[JOS_NCPU][7] = {
  DO_NCPU(GDT_INIT)
};

#define GDTDESC_INIT(cpu)						\
  { .pd_lim = sizeof (gdt[(cpu)]) - 1,					\
    .pd_base = RELOC (&gdt[(cpu)])					\
  },

struct Pseudodesc gdtdesc[JOS_NCPU] = {
  DO_NCPU(GDTDESC_INIT)
};

struct Gatedesc idt[0x100];

struct Pseudodesc idtdesc = {
  .pd_lim = sizeof (idt) - 1,
  .pd_base = CAST64 (&idt)
};
