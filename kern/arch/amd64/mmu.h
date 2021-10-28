#ifndef JOS_MACHINE_MMU_H
#define JOS_MACHINE_MMU_H

#include  <machine/param.h>

#ifndef __ASSEMBLER__
# include <inc/types.h>
# include <inc/intmacro.h>
#else /* __ASSEMBLER__ */
# define UINT64(x) x
# define CAST64(x) (x)
#endif /* __ASSEMBLER__ */
#define ONE UINT64 (1)

#include <machine/mmu-x86.h>

/*
 * AMD64-specific bits
 */

/* Page directory and page table constants. */
#define NPTBITS	    9		/* log2(NPTENTRIES) */
#define NPTLVLS	    3		/* page table depth -1 */
#define PD_SKIP	    6		/* Offset of pd_lim in Pseudodesc */

#ifndef __ASSEMBLER__
/* Pseudo-descriptors used for LGDT, LLDT and LIDT instructions. */
struct Pseudodesc {
  uint16_t pd__garbage1;
  uint16_t pd__garbage2;
  uint16_t pd__garbage3;
  uint16_t pd_lim;		/* Limit */
  uint64_t pd_base;		/* Base address */
} __attribute__((packed));

struct Tss {
  char tss__ign1[4];
  uint64_t tss_rsp[3];		/* Stack pointer for CPL 0, 1, 2 */
  uint64_t tss_ist[8];		/* Note: tss_ist[0] is ignored */
  char tss__ign2[10];
  uint16_t tss_iomb;		/* I/O map base */
  uint8_t tss_iopb[];
} __attribute__ ((packed));

struct Gatedesc {
  uint64_t gd_lo;
  uint64_t gd_hi;
};

struct Trapframe_aux {
};

struct Trapframe {
  /* callee-saved registers except %rax and %rsi */
  uint64_t tf_rcx;
  uint64_t tf_rdx;
  uint64_t tf_rdi;
  uint64_t tf_r8;
  uint64_t tf_r9;
  uint64_t tf_r10;
  uint64_t tf_r11;

  /* caller-saved registers */
  uint64_t tf_rbx;
  uint64_t tf_rbp;
  uint64_t tf_r12;
  uint64_t tf_r13;
  uint64_t tf_r14;
  uint64_t tf_r15;

  /* for use by trap_{ec,noec}_entry_stub */
  union {
    uint64_t tf_rsi;
    uint64_t tf__trapentry_rip;
  };

  /* saved by trap_{ec,noec}_entry_stub */
  uint64_t tf_rax;

  /* hardware-saved registers */
  uint32_t tf_err;
  uint32_t tf__pad1;
  uint64_t tf_rip;
  uint16_t tf_cs;
  uint16_t tf_ds;	// not saved/restored by hardware
  uint16_t tf_es;	// not saved/restored by hardware
  uint16_t tf_fs;	// not saved/restored by hardware
  uint64_t tf_rflags;
  uint64_t tf_rsp;
  uint16_t tf_ss;
  uint16_t tf_gs;	// not saved/restored by hardware
  uint16_t tf__pad3[2];
};

struct VTrapframe {
    union {
	struct {
	    uint64_t vtf_rax;
	    uint64_t vtf_rcx;
	    uint64_t vtf_rdx;
	    uint64_t vtf_rbx;
	    uint64_t vtf_rsp;
	    uint64_t vtf_rbp;
	    uint64_t vtf_rsi;
	    uint64_t vtf_rdi;
	    uint64_t vtf_r8;
	    uint64_t vtf_r9;
	    uint64_t vtf_r10;
	    uint64_t vtf_r11;
	    uint64_t vtf_r12;
	    uint64_t vtf_r13;
	    uint64_t vtf_r14;
	    uint64_t vtf_r15;
	};
	uint64_t vtf_genreg[16];
    };

    uint64_t vtf_rflags;    
    uint64_t vtf_prev_rip;
    uint64_t vtf_next_rip;
};
#endif

#ifdef __ASSEMBLER__ 
#if !JOS_AMD64_OFFSET_HACK

/*
 * These #defines need to be synchronized with struct Trapeframe.
 * It is best to #define JOS_AMD64_OFFSET_HACK 1.  This is a workaround
 * for buggy versions of GNU as.
 */

#define tf_rcx 0
#define tf_rdx 8
#define tf_rdi 16
#define tf_r8  24
#define tf_r9  32
#define tf_r10 40
#define tf_r11 48
#define tf_rbx 56
#define tf_rbp 64
#define tf_r12 72
#define tf_r13 80
#define tf_r14 88
#define tf_r15 96
#define tf_rsi 104
#define tf__trapentry_rip 104
#define tf_rax 112
#define tf_err 120
#define tf__pad1 124
#define tf_rip 128
#define tf_cs 136
#define tf_ds 138
#define tf_es 140
#define tf_fs 142
#define tf_rflags 144
#define tf_rsp 152
#define tf_ss 160
#define tf_gs 162
#define tf__pad2 164
#define tf__pag3 166
#define tf_size 168

#endif
#endif

#endif /* !JOS_MACHINE_MMU_H */
