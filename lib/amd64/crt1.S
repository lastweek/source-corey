#include <machine/asm.h>

ENTRY(_start)
        .cfi_startproc

	// Terminate stack unwinding
	pushq	$0

	// AMD64 requires 16-byte stack alignment before the call
	pushq	$0

	// Save the Share id passed by uinit.c
	movq	%rdi, %r12
		
	call	setup_env
		
	// crtbegin, crtend, and whatever else
	call	_init

        movq    $main, %rdi
        call    libmain
        .cfi_endproc
