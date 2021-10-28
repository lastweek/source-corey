#include <inc/stdio.h>
#include <inc/stacktrace.h>
#include <inc/stabhelper.h>
#include <inc/memlayout.h>

#ifndef STABS_BT
void
print_stacktrace(void)
{	
    uintptr_t rbp;
    __asm __volatile("movq %%rbp,%0" : "=r" (rbp));
    while(rbp > USTACKSTART && rbp) {
        uintptr_t rip = *(uintptr_t*) (rbp + sizeof(uintptr_t));
        cprintf("   0x%lx\n", rip);
        rbp = *(uintptr_t*)rbp;
    }
}

#else
void
print_stacktrace(void)
{	
    uintptr_t rbp;
    struct Eipdebuginfo info;
    __asm __volatile("movq %%rbp,%0" : "=r" (rbp));
    while(rbp > USTACKSTART) {
	uintptr_t rip = *(uintptr_t*)(rbp + sizeof(uintptr_t));
	if (!debuginfo_eip(rip, &info)) {
	    cprintf("\t%s:%d: ", info.eip_file, info.eip_line);
	    for (int j = 0; j < info.eip_fn_namelen; j++)
		cprintf("%c", info.eip_fn_name[j]);
	    cprintf("\t0x%lx\n", rip);
	} else {
	    cprintf("\t 0x%lx\n", rip);
	}
	rbp = *(uintptr_t*)rbp;	 
    }
}

#endif
