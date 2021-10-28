#include <machine/trap.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
#include <machine/trapcodes.h>
#include <machine/x86.h>
#include <machine/proc.h>
#include <machine/utrap.h>
#include <machine/irq.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/syscall.h>
#include <kern/lockmacro.h>
#include <kern/uaccess.h>
#include <kern/intr.h>
#include <kern/prof.h>
#include <dev/picirq.h>
#include <dev/lapic.h>
#include <inc/segment.h>
#include <inc/error.h>
#include <inc/share.h>
#include <inc/stack.h>
#include <inc/setjmp.h>

enum { pf_debug = 0 };
enum { trap_debug = 0 };
enum { trap_detail_debug = 0 };
enum { irq_trace = 0 };

static struct {
    char trap_entry_code[16] __attribute__ ((aligned(16)));
} trap_entry_stubs[256];

void
idt_init(void)
{
    int i;
    extern char trap_ec_entry_stub[],
	trap_noec_entry_stub[], trap_entry_stub_end[];

    assert((size_t) (trap_entry_stub_end - trap_ec_entry_stub) <=
	   sizeof(trap_entry_stubs[0].trap_entry_code));
    assert((size_t) (trap_entry_stub_end - trap_noec_entry_stub) <=
	   sizeof(trap_entry_stubs[0].trap_entry_code));

#define	SET_TRAP_GATE(i, dpl)					\
	SETGATE(idt[i], SEG_IG, GD_KT,				\
		&trap_entry_stubs[i].trap_entry_code[0], dpl)
#define	SET_TRAP_CODE(i, ec_prefix)				\
	memcpy(&trap_entry_stubs[i].trap_entry_code[0],		\
	       trap_##ec_prefix##_entry_stub,			\
	       sizeof(trap_entry_stubs[i].trap_entry_code))

    for (i = 0; i < 0x100; i++) {
	SET_TRAP_CODE(i, noec);
	SET_TRAP_GATE(i, 0);
    }

    // Allow syscalls and breakpoints from ring 3
    SET_TRAP_GATE(T_SYSCALL, 3);
    SET_TRAP_GATE(T_BRKPT, 3);

    // Error-code-generating traps
    SET_TRAP_CODE(T_TSS, ec);
    SET_TRAP_CODE(T_SEGNP, ec);
    SET_TRAP_CODE(T_STACK, ec);
    SET_TRAP_CODE(T_GPFLT, ec);
    SET_TRAP_CODE(T_PGFLT, ec);
    SET_TRAP_CODE(T_FPERR, ec);

    // Some chips don't bother returning an EC and the trapframe 
    // pointer might run off the top of the stack.  It is safe to
    // ignore the EC because it is always 0.
    // SET_TRAP_CODE(T_DBLFLT, ec);

    // All ready
    lidt(&idtdesc.pd_lim);
}

void
trapframe_print(const struct Trapframe *tf)
{
    cprintf("rax %016lx  rbx %016lx  rcx %016lx\n",
	    tf->tf_rax, tf->tf_rbx, tf->tf_rcx);
    cprintf("rdx %016lx  rsi %016lx  rdi %016lx\n",
	    tf->tf_rdx, tf->tf_rsi, tf->tf_rdi);
    cprintf("r8  %016lx  r9  %016lx  r10 %016lx\n",
	    tf->tf_r8, tf->tf_r9, tf->tf_r10);
    cprintf("r11 %016lx  r12 %016lx  r13 %016lx\n",
	    tf->tf_r11, tf->tf_r12, tf->tf_r13);
    cprintf("r14 %016lx  r15 %016lx  rbp %016lx\n",
	    tf->tf_r14, tf->tf_r15, tf->tf_rbp);
    cprintf("rip %016lx  rsp %016lx  cs %04x  ss %04x\n",
	    tf->tf_rip, tf->tf_rsp, tf->tf_cs, tf->tf_ss);
    cprintf("rflags %016lx  err %08x\n", tf->tf_rflags, tf->tf_err);
}

static void
page_fault(struct Processor *src, const struct Trapframe *tf, uint32_t err)
{
    void *fault_va = (void *) rcr2();
    uint32_t reqflags = 0;

    debug(pf_debug, "src %ld.%s va %lx",
	  src->ps_ko.ko_id, src->ps_ko.ko_name, (uint64_t) fault_va);

    if ((err & FEC_W))
	reqflags |= SEGMAP_WRITE;
    if ((err & FEC_I))
	reqflags |= SEGMAP_EXEC;

    if ((tf->tf_cs & 3) == 0 && 
	(!uaccess_enabled() || (uint64_t)fault_va >= ULIM)) 
    {
	cprintf("kernel page fault: va=%p\n", fault_va);
	trapframe_print(tf);
	panic("kernel page fault");
    }

    int r = processor_pagefault(src, fault_va, reqflags);
    if (r == 0 || r == -E_RESTART) {
	if (uaccess_enabled())
	    trapframe_pop(tf);
	return;
    }

    if (uaccess_enabled())
	uaccess_error();
    
    r = locked_call(processor_utrap, src, T_PGFLT, 1,
		    (uintptr_t) fault_va);
    if (r == 0 || r == -E_RESTART)
	return;
    
    cprintf("user page fault: "
	    "va=%p: rip=0x%lx, rsp=0x%lx: %s\n",
	    fault_va, tf->tf_rip, tf->tf_rsp, e2s(r));
    abort();
}

static void
trap_dispatch(int trapno, const struct Trapframe *tf)
{
    int64_t r;

    if (trapno == T_NMI) {
	uint8_t reason = inb(0x61);
	if (reason == 0x30 || reason == 0x20 || 
	    reason == 0x2c || reason == 0x3c)
	{
	    reinit_local();
	}
	panic("NMI, reason code 0x%x\n", reason);
    }

    struct Processor *ps = processor_sched();
    if (!ps) {
	trapframe_print(tf);
	panic("trap %d while CPU %u idle", trapno, arch_cpu());
    }

    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + JOS_MAX_IRQS) {
	uint32_t irqno = trapno - IRQ_OFFSET;

	if (irq_trace)
	    cprintf("trap_dispatch: irqno %u cpu %u\n", irqno, arch_cpu());

	irq_eoi(irqno);
	irq_handler(ps, irqno);
	return;
    }

    if (ps->ps_mode == ps_mode_vm)
	panic("VM exception slipped through");

    switch (trapno) {
    case T_SYSCALL:
	r = kern_syscall(ps, tf->tf_rdi, tf->tf_rsi, tf->tf_rdx, tf->tf_rcx,
			 tf->tf_r8, tf->tf_r9, tf->tf_r10, tf->tf_r11);
	if (r != -E_RESTART)
	    ps->ps_tf.tf_rax = r;
	else
	    ps->ps_tf.tf_rip -= 2;
	break;

    case T_PGFLT:
	page_fault(ps, tf, tf->tf_err);
	break;

    case T_TLBFLUSH:
	lapic_eoi(0);
	break;

    case T_HALT:
	lapic_eoi(0);
	if (ps->ps_halt)
	    processor_halt(ps);
	break;

    default:
	r = locked_call(processor_utrap, ps, trapno, 1, 0);
	if (r != 0 && r != -E_RESTART) {
	    cprintf("Unknown trap %u, cannot utrap: %s.  Trapframe:\n",
		    trapno, e2s(r));
	    trapframe_print(tf);
	    processor_halt(ps);
	}
    }
}

void __attribute__ ((__noreturn__, no_instrument_function))
trap_handler(struct Trapframe *tf, uint64_t trampoline_rip)
{
    uint64_t trap0rip = (uint64_t) & trap_entry_stubs[0].trap_entry_code[0];
    uint32_t trapno = (trampoline_rip - trap0rip) /
	sizeof(trap_entry_stubs[0].trap_entry_code);

    tf->tf_ds = read_ds();
    tf->tf_es = read_es();
    tf->tf_fs = read_fs();
    tf->tf_gs = read_gs();

    // Only save trapeframe when not in the middle of a uaccess
    struct Processor *ps = processor_sched();
    if (ps && !uaccess_enabled()) {
	ps->ps_tf = *tf;
	if (ps->ps_fp_enabled) {
	    lcr0(rcr0() & ~CR0_TS);
	    // CPUs currently can't be shared by processors
	    //fxsave(&ps->ps_fpreg);
	}
    }

    if (trap_debug) {
	cprintf("trap_handler: trapno %u\n", trapno);
	if (ps)
	    cprintf("trap_handler: ps %ld.%s\n", ps->ps_ko.ko_id,
		    ps->ps_ko.ko_name);
	if (trap_detail_debug)
	    trapframe_print(tf);
    }

    uint64_t s = read_tsc();
    trap_dispatch(trapno, tf);
    prof_trap(trapno, read_tsc() - s);

    processor_run();
}

int
processor_arch_vector(struct Processor *ps, const struct u_context *uc)
{
    if (uc->uc_mode == ps_mode_vm)
	panic("XXX vmm flux");

    memset(&ps->ps_tf, 0, sizeof(ps->ps_tf));

    ps->ps_tf.tf_rflags = FL_IF;
    ps->ps_tf.tf_rip = (uintptr_t) uc->uc_entry;
    ps->ps_tf.tf_rsp = (uintptr_t) uc->uc_stack;
    ps->ps_tf.tf_rdi = uc->uc_arg[0];
    ps->ps_tf.tf_rsi = uc->uc_arg[1];
    ps->ps_tf.tf_rdx = uc->uc_arg[2];
    ps->ps_tf.tf_rcx = uc->uc_arg[3];
    ps->ps_tf.tf_r8 = uc->uc_arg[4];
    ps->ps_tf.tf_r9 = uc->uc_arg[5];

    ps->ps_tf.tf_cs = GD_UT_NMASK | 3;
    ps->ps_tf.tf_ss = GD_UD | 3;
    ps->ps_tf.tf_ds = GD_UD | 3;
    ps->ps_tf.tf_es = GD_UD | 3;
    ps->ps_tf.tf_fs = GD_UD | 3;
    ps->ps_tf.tf_gs = GD_UD | 3;

    static_assert(u_context_narg == 6);

    return 0;
}

int
processor_arch_utrap(struct Processor *ps, uint32_t trapno, uint64_t arg)
{
    if (ps->ps_mode == ps_mode_vm)
	panic("The VM should have taken the IRQ");

    void *stacktop;
    uint64_t rsp = ps->ps_tf.tf_rsp;
    if (rsp > ps->ps_at->at_utrap_stack_base &&
	rsp <= ps->ps_at->at_utrap_stack_top) {
	// Skip red zone (see ABI spec)
	stacktop = (void *) (uintptr_t) rsp - 128;
    } else {
	// Skip UTrap pending
	stacktop = (void *) (uintptr_t) ps->ps_at->at_utrap_stack_top - 16;
    }

    struct UTrapframe t_utf;
    t_utf.utf_trap_num = trapno;
    t_utf.utf_trap_arg = arg;
#define UTF_COPY(r) t_utf.utf_##r = ps->ps_tf.tf_##r
    UTF_COPY(rax);
    UTF_COPY(rbx);
    UTF_COPY(rcx);
    UTF_COPY(rdx);
    UTF_COPY(rsi);
    UTF_COPY(rdi);
    UTF_COPY(rbp);
    UTF_COPY(rsp);
    UTF_COPY(r8);
    UTF_COPY(r9);
    UTF_COPY(r10);
    UTF_COPY(r11);
    UTF_COPY(r12);
    UTF_COPY(r13);
    UTF_COPY(r14);
    UTF_COPY(r15);
    UTF_COPY(rip);
    UTF_COPY(rflags);
#undef UTF_COPY

    struct UTrapframe *utf = stacktop - sizeof(*utf);

    int r = uaccess_start();
    if (r < 0) {
	if ((uintptr_t) utf <= ps->ps_at->at_utrap_stack_base)
	    cprintf("thread_arch_utrap: utrap stack overflow\n");
	return r;
    }
    memcpy(utf, &t_utf, sizeof(*utf));
    uaccess_stop();
    ps->ps_tf.tf_rsp = (uintptr_t) utf;
    ps->ps_tf.tf_rip = ps->ps_at->at_utrap_entry;
    ps->ps_tf.tf_cs = GD_UT_MASK;
    
    return 0;
}

int
processor_arch_pending(struct Processor *ps, uint32_t trapno)
{
    if (ps->ps_mode == ps_mode_vm)
	panic("The VM should have taken the IRQ");

    uint64_t *utp =
	(uint64_t *) (uintptr_t) (ps->ps_at->at_utrap_stack_top - 16);


    int r = uaccess_start();
    if (r < 0)
	return r;
    utp[0] = utp[0] | (1 << trapno);
    utp[1] = utp[1] | (1 << trapno);
    uaccess_stop();
    return 0;
}

int
processor_arch_is_masked(struct Processor *ps)
{
    if (ps->ps_mode == ps_mode_vm)
	return 0;
    return ps->ps_tf.tf_cs == GD_UT_MASK;
}

void
processor_arch_run(struct Processor *ps)
{
    if (ps->ps_mode == ps_mode_vm)
	panic("XXX vmm flux");
    else if (ps->ps_mode == ps_mode_reg) {
	if (ps->ps_fp_enabled) {
	    // CPUs currently can't be shared by processors
	    //fxrstor(&ps->ps_fpreg);
	    lcr0(rcr0() & ~CR0_TS);
	}
	trapframe_pop(&ps->ps_tf);
    } else
	panic("bad mode: %u\n", ps->ps_mode);
}

#if JOS_AMD64_OFFSET_HACK
static void __attribute__ ((used))
    trap_field_symbols(void)
{
#define TF_DEF(field)							\
  __asm volatile (".globl\t" #field "\n\t.set\t" #field ",%0"		\
		:: "m" (*(int *) offsetof (struct Trapframe, field)))
    TF_DEF(tf_rax);
    TF_DEF(tf_rcx);
    TF_DEF(tf_rdx);
    TF_DEF(tf_rsi);
    TF_DEF(tf_rdi);
    TF_DEF(tf_r8);
    TF_DEF(tf_r9);
    TF_DEF(tf_r10);
    TF_DEF(tf_r11);
    TF_DEF(tf_rbx);
    TF_DEF(tf_rbp);
    TF_DEF(tf_r12);
    TF_DEF(tf_r13);
    TF_DEF(tf_r14);
    TF_DEF(tf_r15);
    TF_DEF(tf_err);
    TF_DEF(tf_rip);
    TF_DEF(tf_cs);
    TF_DEF(tf_ds);
    TF_DEF(tf_es);
    TF_DEF(tf_fs);
    TF_DEF(tf_rflags);
    TF_DEF(tf_rsp);
    TF_DEF(tf_ss);
    TF_DEF(tf_gs);
    TF_DEF(tf__trapentry_rip);
}
#endif
