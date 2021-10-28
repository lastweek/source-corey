#define ENTRY(x)	\
.text; .globl x; .align 16, 0x90; .type x, @function; x:

// data = %rdi, s = %rsi
ENTRY(write_prefetch)
    xor  %r8d, %r8d
    xor  %rax, %rax
    test %rsi, %rsi
    je 2f
    1:
    mov  %rax, %rcx
    incb (%rcx, %rdi, 1)		// inc data
    decb (%rcx, %rdi, 1)		// dec data
    add  %eax, %r8d
    lea 0x1000(%rcx), %rax
    cmp %rax, %rsi
    jg 1b
    2:
    mov %r8d, %eax
    retq

// data = %rdi, s = %rsi
ENTRY(read_prefetch)
    xor  %r8d, %r8d
    xor  %rax, %rax
    test %rsi, %rsi
    je 2f
    1:
    mov  %rax, %rcx
    movsbl (%rcx, %rdi, 1), %eax	// read data
    add  %eax, %r8d
    lea 0x1000(%rcx), %rax
    cmp %rax, %rsi
    jg 1b
    2:
    mov %r8d, %eax
    retq

// a = %rdi, b = %rsi, %rdx
ENTRY(ins_test)
    pushq %rbp
    pushq %rbx
    mov %rdx, %r10
    mov $0, %r11
    1:
    add $1, %rbx
    dec %r9
    inc %r8
    inc %rax

    dec %r10
    jnz 1b

    popq %rbx
    popq %rbp
    retq
