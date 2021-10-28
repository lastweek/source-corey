OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
	/* sreplicate and ereplicate must be page aligned */
	PROVIDE(sreplicate = 0xffffffff80100000);	

	/* The 0x1e0 link offset corresponds to the offset of the text section
	 * in the resulting ELF kernel file.  This way, the kernel ELF binary
	 * can be loaded at exactly 0x100000, without any further relocation.
	 */
	. = 0xffffffff801001e0;

	.text : {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* Page align so sections replicated across nodes can be mapped in 
	 * local pagemaps without mapping any data pages.
	 */
	. = ALIGN(0x1000);
	PROVIDE(ereplicate = .);

	PROVIDE(sdata = .);
	.data : {
		*(.data)
	}
	PROVIDE(edata = .);

	PROVIDE(sbss = .);	
	.bss : {
		*(.bss)
	}
	PROVIDE(ebss = .);
	PROVIDE(end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
