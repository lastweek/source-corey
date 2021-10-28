#include <inc/lib.h>
#include <inc/utrap.h>
#include <inc/memlayout.h>
#include <stddef.h>

static void __attribute__((used))
utrap_field_symbols(void)
{
#define GEN_DEF(n, v)  __asm volatile("#define " #n " %0" :: "m" (*(int *) (v)))
#define UTF_DEF(field) GEN_DEF(field, offsetof (struct UTrapframe, field))

    UTF_DEF(utf_rax);
    UTF_DEF(utf_rbx);
    UTF_DEF(utf_rcx);
    UTF_DEF(utf_rdx);

    UTF_DEF(utf_rsi);
    UTF_DEF(utf_rdi);
    UTF_DEF(utf_rbp);
    UTF_DEF(utf_rsp);

    UTF_DEF(utf_r8);
    UTF_DEF(utf_r9);
    UTF_DEF(utf_r10);
    UTF_DEF(utf_r11);

    UTF_DEF(utf_r12);
    UTF_DEF(utf_r13);
    UTF_DEF(utf_r14);
    UTF_DEF(utf_r15);

    UTF_DEF(utf_rip);
    UTF_DEF(utf_rflags);
}
