#ifndef _GLIBC_27_H
#define _GLIBC_27_H

#define _hidden_strong_alias(original, alias)				\
  .globl alias;		\
  .hidden alias;				\
  .set alias,original

#define hidden_def(name)	_hidden_strong_alias (name, __GI_##name)
#define libc_hidden_def(name) hidden_def(name)
#define libc_hidden_builtin_def(name) libc_hidden_def(name)
#define hidden_proto(name,attr)
#define libc_hidden_proto(name,attrs...) hidden_proto (name, attrs)

/* Define an entry point visible from C.  */
#define	ENTRY(name)							      \
  .globl name;				      \
  .type name,@function;			      \
  .align 16,0x90;							      \
  name:								      \
  .cfi_startproc;

/* ELF style label name*/
#define L(name)	.L##name

/*1/2 L1*/
#define __x86_64_data_cache_size_half(x) $16384
/*1/2 L2*/
#define __x86_64_shared_cache_size_half(x) $1048576
/*whether the hardware support prefetchw.Hardware dependent...*/

#define cfi_rel_offset(reg, off)	.cfi_rel_offset reg, off
#define cfi_restore(reg)		.cfi_restore reg

#define END(name)							      \
  .cfi_endproc;								      \
  .size name,.-name;


#endif
