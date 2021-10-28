#ifndef JOS_MACHINE_X86_H
#define JOS_MACHINE_X86_H

#include <inc/types.h>
#include <machine/mmu.h>
#include <machine/x86-common.h>

X86_INST_ATTR void lcr0(uint64_t val);
X86_INST_ATTR uint64_t rcr0(void);
X86_INST_ATTR uint64_t rcr2(void);
X86_INST_ATTR void lcr3(uint64_t val);
X86_INST_ATTR uint64_t rcr3(void);
X86_INST_ATTR void lcr4(uint64_t val);
X86_INST_ATTR uint64_t rcr4(void);
X86_INST_ATTR void lcr8(uint64_t val);
X86_INST_ATTR uint64_t rcr8(void);
X86_INST_ATTR uint64_t read_rflags(void);
X86_INST_ATTR void write_rflags(uint64_t eflags);
X86_INST_ATTR uint64_t read_rbp(void);
X86_INST_ATTR uint64_t read_rsp(void);
X86_INST_ATTR uint64_t read_tscp(uint32_t *auxp);
X86_INST_ATTR void monitor(uint64_t rax, uint32_t ecx, uint32_t edx);
X86_INST_ATTR void mwait(uint32_t ecx, uint32_t edx);
X86_INST_ATTR uint8_t cmpxchg(uint64_t *memp, uint64_t rax, uint64_t newv);
X86_INST_ATTR uint8_t cmpxchg16b(uint64_t *memp, uint64_t rdc, uint64_t rax, 
				 uint64_t rcx, uint64_t rbx);

void
lcr0(uint64_t val)
{
	__asm __volatile("movq %0,%%cr0" : : "r" (val));
}

uint64_t
rcr0(void)
{
	uint64_t val;
	__asm __volatile("movq %%cr0,%0" : "=r" (val));
	return val;
}

uint64_t
rcr2(void)
{
	uint64_t val;
	__asm __volatile("movq %%cr2,%0" : "=r" (val));
	return val;
}

void
lcr3(uint64_t val)
{
	__asm __volatile("movq %0,%%cr3" : : "r" (val));
}

uint64_t
rcr3(void)
{
	uint64_t val;
	__asm __volatile("movq %%cr3,%0" : "=r" (val));
	return val;
}

void
lcr4(uint64_t val)
{
	__asm __volatile("movq %0,%%cr4" : : "r" (val));
}

uint64_t
rcr4(void)
{
	uint64_t cr4;
	__asm __volatile("movq %%cr4,%0" : "=r" (cr4));
	return cr4;
}

void
lcr8(uint64_t val)
{
	__asm __volatile("movq %0,%%cr8" : : "r" (val));
}

uint64_t
rcr8(void)
{
	uint64_t cr8;
	__asm __volatile("movq %%cr8,%0" : "=r" (cr8));
	return cr8;
}

uint64_t
read_rflags(void)
{
        uint64_t rflags;
        __asm __volatile("pushfq; popq %0" : "=r" (rflags));
        return rflags;
}

void
write_rflags(uint64_t rflags)
{
        __asm __volatile("pushq %0; popfq" : : "r" (rflags));
}

uint64_t
read_rbp(void)
{
        uint64_t rbp;
        __asm __volatile("movq %%rbp,%0" : "=r" (rbp));
        return rbp;
}

uint64_t
read_rsp(void)
{
        uint64_t rsp;
        __asm __volatile("movq %%rsp,%0" : "=r" (rsp));
        return rsp;
}

uint64_t
read_tscp(uint32_t *auxp)
{
	uint32_t a, d, c;
	__asm __volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
	if (auxp)
	    *auxp = c;
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

void 
monitor(uint64_t rax, uint32_t ecx, uint32_t edx)
{
    __asm volatile("monitor" : : "a" (rax), "c" (ecx), "d" (edx));
}

void 
mwait(uint32_t ecx, uint32_t edx)
{
    __asm volatile("mwait" : : "c" (ecx), "d" (edx));    
}

uint8_t
cmpxchg(uint64_t *memp, uint64_t rax, uint64_t newv)
{
    uint8_t z;
    __asm__ __volatile(
		       JOS_ATOMIC_LOCK "cmpxchgq %3, %1; setz %2"
		       :  "=a" (rax), "+m" (*memp), "=r" (z)
		       : "q" (newv), "0" (rax)
		       : "cc");
    return z;
}

uint8_t
cmpxchg16b(uint64_t *memp, uint64_t rax, uint64_t rdx, 
	   uint64_t rbx, uint64_t rcx)
{
    uint8_t z;
    __asm __volatile(
		     JOS_ATOMIC_LOCK "cmpxchg16b %3; setz %2" 
		     : "=a" (rax), "=d" (rdx), "=r" (z), "+m" (*memp)
		     : "a" (rax), "d" (rdx), "b" (rbx), "c" (rcx)
		     : "memory", "cc" );
    return z;
}

#define READ_DR(n)						\
X86_INST_ATTR uint64_t read_dr##n(void);			\
uint64_t							\
read_dr##n(void)						\
{								\
	uint64_t val;						\
	__asm __volatile("movq %%dr" #n ",%0" : "=r" (val));	\
	return val;						\
}

#define WRITE_DR(n)						\
X86_INST_ATTR void write_dr##n(uint64_t v);			\
void								\
write_dr##n(uint64_t v)						\
{								\
	__asm __volatile("movq %0,%%dr" #n : : "r" (v));	\
}

#define RW_DR(n) READ_DR(n) WRITE_DR(n)

RW_DR(0)
RW_DR(1)
RW_DR(2)
RW_DR(3)
RW_DR(4)
RW_DR(5)
RW_DR(6)
RW_DR(7)

#undef RW_DR
#undef READ_DR
#undef WRITE_DR

#endif /* !JOS_MACHINE_X86_H */
