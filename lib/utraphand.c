#include <machine/trapcodes.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/utraphand.h>
#include <inc/assert.h>
#include <machine/memlayout.h>

enum { print_as = 0 };

void time_tick(void);

static int
handle_pf(struct UTrapframe *utf)
{
    // XXX If fell off stack, should grow it
    return -1;
}

static void
utrapframe_print(const struct UTrapframe *utf)
{
    cprintf("rax %016lx  rbx %016lx  rcx %016lx\n",
	    utf->utf_rax, utf->utf_rbx, utf->utf_rcx);
    cprintf("rdx %016lx  rsi %016lx  rdi %016lx\n",
	    utf->utf_rdx, utf->utf_rsi, utf->utf_rdi);
    cprintf("r8  %016lx  r9  %016lx  r10 %016lx\n",
	    utf->utf_r8, utf->utf_r9, utf->utf_r10);
    cprintf("r11 %016lx  r12 %016lx  r13 %016lx\n",
	    utf->utf_r11, utf->utf_r12, utf->utf_r13);
    cprintf("r14 %016lx  r15 %016lx  rbp %016lx\n",
	    utf->utf_r14, utf->utf_r15, utf->utf_rbp);
    cprintf("rip %016lx  rsp %016lx  rflags %08lx\n",
	    utf->utf_rip, utf->utf_rsp, utf->utf_rflags);
}

void
utrap_handler(struct UTrapframe *utf)
{
    int r;
    switch(utf->utf_trap_num) {
    case T_PGFLT:
	r = handle_pf(utf);
	if (r < 0) {
	    cprintf("handle_pf: unable to resolve pf: rip 0x%lx va 0x%lx\n", 
		    utf->utf_rip, utf->utf_trap_arg);
	    if (print_as)
		as_print_current_uas();
	    print_backtrace();
	    thread_halt();
	}
	break;
    case T_DEVICE:
	r = sys_self_fp_enable();
	if (r < 0)
	    panic("unable to enable fp support: %s", e2s(r));
	break;
    case IRQ_OFFSET + IRQ_TIMER:
	time_tick();
	break;
    default:
	utrapframe_print(utf);
	panic("Unknown trap num %u", utf->utf_trap_num);
    }
}
