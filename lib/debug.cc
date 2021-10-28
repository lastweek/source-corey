extern "C" {
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/stabhelper.h>
#include <inc/memlayout.h>
}
#include <inc/backtracer.hh>

#ifndef STABS_BT
void
print_backtrace(void)
{
    backtracer bt;
    for (int i = 0; i < bt.backtracer_depth(); i++) {
	void *addr = bt.backtracer_addr(i);
	cprintf("  %p\n", addr);
    }
}

#else
void
print_backtrace(void)
{
    backtracer bt;
    struct Eipdebuginfo info;
    for (int i = 0; i < bt.backtracer_depth(); i++) {
	void *addr = bt.backtracer_addr(i);
	cprintf("cpu:%d, address %p:\n", processor_current_procid(), addr);
	if (!debuginfo_eip((uintptr_t) addr, &info)) {
	    cprintf("\t%s:%d: ", info.eip_file, info.eip_line);
	    for (int j = 0; j < info.eip_fn_namelen; j++)
		cprintf("%c", info.eip_fn_name[j]);
	    cprintf("+%lx\n", (uintptr_t) addr - info.eip_fn_addr);
	}
    }
}

#endif
