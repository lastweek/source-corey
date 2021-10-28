#ifndef JOS_INC_UTRAP_H
#define JOS_INC_UTRAP_H

#include <machine/utrap.h>

/* Assembly stubs */
void utrap_entry_asm(void);
void utrap_chain_dwarf2(void);
void utrap_ret(struct UTrapframe *utf)
    __attribute__((noreturn, JOS_UTRAP_GCCATTR));

/* C fault handler */
void utrap_entry(struct UTrapframe *utf)
    __attribute__((noreturn, JOS_UTRAP_GCCATTR));
int  utrap_init(void);
void utrap_set_handler(void (*fn)(struct UTrapframe *));

#endif
