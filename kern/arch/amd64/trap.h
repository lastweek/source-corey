#ifndef JOS_MACHINE_TRAP_H
#define JOS_MACHINE_TRAP_H

#include <machine/mmu.h>
#include <machine/types.h>

void idt_init(void);

void init(uint32_t start_eax, uint32_t start_ebx) __attribute__((noreturn));

void init_ap(void) __attribute__((noreturn));

void reinit_local(void) __attribute__((noreturn));

// Low-level trapframe jump in locore.S
void trapframe_pop(const struct Trapframe *) __attribute__((__noreturn__));

void trap_handler(struct Trapframe *tf, uint64_t trampoline_rip)
    __attribute__((__noreturn__));

#endif
