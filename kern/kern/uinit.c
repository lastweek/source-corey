#include <machine/param.h>
#include <machine/mmu.h>
#include <machine/memlayout.h>
#include <kern/lib.h>
#include <kern/uinit.h>
#include <kern/embedbin.h>
#include <kern/kobj.h>
#include <kern/processor.h>
#include <kern/segment.h>
#include <kern/at.h>
#include <kern/lockmacro.h>
#include <kern/pageinfo.h>
#include <inc/elf64.h>
#include <inc/error.h>
#include <inc/intmacro.h>
#include <inc/segment.h>
#include <inc/context.h>
#include <inc/bootparam.h>

#if JOS_ARCH_BITS==32
#define ARCH_ELF_CLASS	ELF_CLASS_32
#define ARCH_ELF_EHDR	Elf32_Ehdr
#define ARCH_ELF_PHDR	Elf32_Phdr
#elif JOS_ARCH_BITS==64
#define ARCH_ELF_CLASS	ELF_CLASS_64
#define ARCH_ELF_EHDR	Elf64_Ehdr
#define ARCH_ELF_PHDR	Elf64_Phdr
#define ARCH_ELF_SHDR	Elf64_Shdr
#else
#error Unknown arch
#endif

#if JOS_ARCH_ENDIAN==JOS_LITTLE_ENDIAN
#define ARCH_ELF_MAGIC	ELF_MAGIC_LE
#elif JOS_ARCH_ENDIAN==JOS_BIG_ENDIAN
#define ARCH_ELF_MAGIC	ELF_MAGIC_BE
#else
#error Unknown arch
#endif

#define assert_check(expr)			\
    do {					\
	int __r = (expr);			\
	if (__r < 0)				\
	    panic("%s: %s", #expr, e2s(__r));	\
    } while (0)

static int
elf_copyin(void *p, uint64_t offset, uint32_t count,
	   const uint8_t * binary, uint64_t size)
{
    if (offset + count > size) {
	cprintf("Reading past the end of ELF binary\n");
	return -E_INVAL;
    }

    memcpy(p, binary + offset, count);
    return 0;
}

static int
elf_copyin_str(char *p, uint32_t n, uint64_t offset,
	       const uint8_t * binary, uint64_t size)
{
    for (uint64_t i = offset; i < size; i++) {
	p[i - offset] = binary[i];
	if (binary[i] == 0)
	    return 0;
	if (i - offset - 1 == n) {
	    cprintf("String buffer too short\n");
	    return -E_INVAL;
	}
    }

    cprintf("Reading past the end of ELF binary\n");
    return -E_INVAL;
}

static void
segment_fill(struct Segment *sg, uint64_t sg_off, const uint8_t * buf,
	     uint64_t bufsize)
{
    uint64_t start_pg = sg_off / PGSIZE;
    uint64_t padsize = sg_off % PGSIZE;
    uint64_t bc = 0;

    for (int i = 0; bufsize != bc; i += PGSIZE) {
	uint64_t pad = 0;
	if (padsize > pad)
	    pad = JMIN((uint64_t) PGSIZE, padsize);

	uint64_t bytes = PGSIZE - pad;
	if (bytes > bufsize - bc)
	    bytes = bufsize - bc;

	if (buf) {
	    void *p;
	    assert_check(segment_get_page(sg,
			 (i / PGSIZE) + start_pg, &p, page_excl));
	    memset(p, 0, pad);
	    memcpy(p + pad, &buf[bc], bytes);
	    
	    struct page_info *pi = page_to_pageinfo(p);
	    pi->pi_clear = 1;
	}
	padsize -= pad;
	bc += bytes;
    }
}

static int
segment_create_embed(struct Share *sh, struct Processor *ps,
		     uint64_t segsize, uint64_t sg_off,
		     const uint8_t * buf, uint64_t bufsize,
		     struct Segment **sg_store)
{
    if (bufsize > segsize) {
	cprintf("segment_create_embed: bufsize %" PRIu64 " > segsize %" PRIu64
		"\n", bufsize, segsize);
	return -E_INVAL;
    }

    struct Segment *sg;
    int r = segment_alloc(&sg, ps->ps_ko.ko_pid);
    if (r < 0) {
	cprintf("segment_create_embed: cannot alloc segment: %s\n", e2s(r));
	return r;
    }

    r = segment_set_nbytes(sg, segsize);
    if (r < 0) {
	cprintf("segment_create_embed: cannot grow segment: %s\n", e2s(r));
	return r;
    }
    locked_void_call(kobject_set_name, &sg->sg_ko, "boostrap-sg");

    segment_fill(sg, sg_off, buf, bufsize);

    if (sg_store)
	*sg_store = sg;

    return share_import_obj(sh, (struct kobject *) sg);
}

static int
elf_add_segmap(struct Address_tree *at, uint32_t * smi, struct sobj_ref sg,
	       uint64_t start_page, uint64_t num_pages, void *va,
	       uint64_t flags)
{
    if (*smi >= N_UADDRMAP_PER_PAGE) {
	cprintf("ELF: too many segments\n");
	return -E_NO_MEM;
    }

    assert_check(darray_set_nent(&at->at_uaddrmap, N_UADDRMAP_PER_PAGE, 1));
    
    struct u_address_mapping *uam;
    assert_check(darray_get(&at->at_uaddrmap, *smi, (void **)&uam, page_excl));
    
    uam->type = address_mapping_segment;
    uam->object = sg;
    uam->start_page = start_page;
    uam->num_pages = num_pages;
    uam->flags = flags;
    uam->kslot = *smi;
    uam->va = va;
    (*smi)++;

    return 0;
}

static int
load_elf(struct Share *sh, struct Processor *ps, struct Address_tree *at,
	 const uint8_t * binary, uint64_t size)
{
    ARCH_ELF_EHDR elf;
    if (elf_copyin(&elf, 0, sizeof(elf), binary, size) < 0) {
	cprintf("ELF header unreadable\n");
	return -E_INVAL;
    }

    if (elf.e_magic != ARCH_ELF_MAGIC ||
	elf.e_ident[EI_CLASS] != ARCH_ELF_CLASS) {
	cprintf("ELF magic/class mismatch\n");
	return -E_INVAL;
    }

    ARCH_ELF_SHDR namehdr;
    if (elf_copyin(&namehdr,
		   elf.e_shoff + elf.e_shstrndx * sizeof(ARCH_ELF_SHDR),
		   sizeof(ARCH_ELF_SHDR), binary, size) < 0) {
	cprintf("ELF name header unreadable\n");
	return -E_INVAL;
    }

    Elf64_Addr share_data_addr = 0;
    Elf64_Addr share_data_size = 0;

    // Scan for magic .josmp.share.data section
    for (int i = 0; i < elf.e_shnum; i++) {
	ARCH_ELF_SHDR shdr;
	if (elf_copyin(&shdr, elf.e_shoff + i * sizeof(shdr), sizeof(shdr),
		       binary, size) < 0) {
	    cprintf("ELF section header unreadable\n");
	    return -E_INVAL;
	}

	if (shdr.sh_type != ELF_SHT_PROGBITS)
	    continue;

	char name[32];
	if (elf_copyin_str(name, 32, shdr.sh_name + namehdr.sh_offset,
			   binary, size) < 0) {
	    cprintf("ELF Unable to copy string\n");
	    return -E_INVAL;
	}

	if (strcmp(name, JSHARED_SECTION) == 0) {
	    share_data_addr = shdr.sh_addr;
	    share_data_size = shdr.sh_size;
	    break;
	}
    }

    int r;
    uint32_t segmap_i = 0;
    for (int i = 0; i < elf.e_phnum; i++) {
	ARCH_ELF_PHDR ph;
	if (elf_copyin(&ph, elf.e_phoff + i * sizeof(ph), sizeof(ph),
		       binary, size) < 0) {
	    cprintf("ELF section header unreadable\n");
	    return -E_INVAL;
	}

	if (ph.p_type != ELF_PROG_LOAD)
	    continue;

	if (ph.p_offset + ph.p_filesz > size) {
	    cprintf("ELF: section past the end of the file\n");
	    return -E_INVAL;
	}

	char *va = (char *) (uintptr_t) ROUNDDOWN(ph.p_vaddr, PGSIZE);
	uint64_t page_offset = PGOFF(ph.p_vaddr);
	uint64_t mem_pages =
	    ROUNDUP(page_offset + ph.p_memsz, PGSIZE) / PGSIZE;

	struct Segment *s;
	r = segment_create_embed(sh, ps, mem_pages * PGSIZE, page_offset,
				 binary + ph.p_offset, ph.p_filesz, &s);

	if (r < 0) {
	    cprintf("ELF: cannot create segment: %s\n", e2s(r));
	    return r;
	}

	if (share_data_size && 
	    share_data_addr == ph.p_vaddr
	    && share_data_addr + share_data_size == ph.p_vaddr + ph.p_memsz) 
	{
	    r = elf_add_segmap(at, &segmap_i,
			       SOBJ(sh->sh_ko.ko_id, s->sg_ko.ko_id), 0,
			       mem_pages, va, ph.p_flags | SEGMAP_SHARED);

	} else {
	    r = elf_add_segmap(at, &segmap_i,
			       SOBJ(sh->sh_ko.ko_id, s->sg_ko.ko_id),
			       0, mem_pages, va, ph.p_flags);
	}
	if (r < 0) {
	    cprintf("ELF: cannot map segment\n");
	    return r;
	}
    }

    // Map a stack
    int stackpages = 4;
    struct Segment *s;
    r = segment_create_embed(sh, ps, stackpages * PGSIZE, 0, 0, 0, &s);
    if (r < 0) {
	cprintf("ELF: cannot allocate stack segment: %s\n", e2s(r));
	return r;
    }
    r = elf_add_segmap(at, &segmap_i,
		       SOBJ(sh->sh_ko.ko_id, s->sg_ko.ko_id),
		       0, stackpages,
		       (void *) (uintptr_t) (USTACKTOP - stackpages * PGSIZE),
		       SEGMAP_READ | SEGMAP_WRITE);
    if (r < 0) {
	cprintf("ELF: cannot map stack segment: %s\n", e2s(r));
	return r;
    }

    // Map a boot_args
    r = segment_create_embed(sh, ps, PGSIZE, 0, 
			     (uint8_t *)boot_args, PGSIZE, &s);
    if (r < 0) {
	cprintf("ELF: cannot allocate boot_args segment: %s\n", e2s(r));
	return r;
    }
    r = elf_add_segmap(at, &segmap_i,
		       SOBJ(sh->sh_ko.ko_id, s->sg_ko.ko_id),
		       0, 1,
		       (void *) (uintptr_t) (UBOOTARGS),
		       SEGMAP_READ | SEGMAP_WRITE);
    if (r < 0) {
	cprintf("ELF: cannot map boot_args segment: %s\n", e2s(r));
	return r;
    }

    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = SOBJ(sh->sh_ko.ko_id, at->at_ko.ko_id);
    uc.uc_mode = ps_mode_reg;
    uc.uc_entry = (void *) (uintptr_t) elf.e_entry;
    uc.uc_stack = (void *) USTACKTOP;
    uc.uc_arg[0] = sh->sh_ko.ko_id;

    assert_check(locked_call(processor_vector, ps, &uc));
    return 0;
}

static int
load_img(struct Share *sh, struct Processor *ps, struct Address_tree *at,
	 const uint8_t * binary, uint64_t size)
{
    enum { low_bytes = 640 * 1024 };
    uint64_t gmem_pages = 4096;	// 16 MB
    uint64_t ext_bytes = (gmem_pages * PGSIZE) - low_bytes;

    if (gmem_pages * PGSIZE < low_bytes) {
	cprintf("IMG: less that 640KB of guest memory is not supported\n");
	return -E_INVAL;
    }

    struct setup_header *hdr;
    hdr = (struct setup_header *) &binary[SETUP_HEADER_OFFSET];

    if (hdr->boot_flag != 0xAA55) {
	cprintf("IMG: bad boot flag: %x\n", hdr->boot_flag);
	return -E_INVAL;
    }

    char *sig = (char *) &hdr->header;
    if (memcmp(sig, "HdrS", 4)) {
	cprintf("IMG: bad header signature: %c%c%c%c\n",
		sig[0], sig[1], sig[2], sig[3]);
	return -E_INVAL;
    }

    guestaddr_t kloadaddr = (uintptr_t) hdr->code32_start;
    if (kloadaddr < EXTPHYSMEM) {
	cprintf("IMG: loading kernel into low memory is not supported: %lx\n",
		kloadaddr);
	return -E_INVAL;
    }

    uint64_t setup_bytes = 512 * (hdr->setup_sects + 1);
    uint64_t kernel_off = setup_bytes;
    uint64_t kernel_bytes = size - kernel_off;
    if (kernel_bytes > ext_bytes) {
	cprintf
	    ("IMG: insufficient extended memory to load kernel: %ld > %ld\n",
	     kernel_bytes, ext_bytes);
	return -E_INVAL;
    }
    // Alloc and map 640 KB low memory segment
    struct Segment *lowsg;
    int r = segment_create_embed(sh, ps, low_bytes, 0, 0, 0, &lowsg);
    if (r < 0) {
	cprintf("IMG: cannot create low segment: %s\n", e2s(r));
	return r;
    }

    uint32_t segmap_i = 0;
    r = elf_add_segmap(at, &segmap_i,
		       SOBJ(sh->sh_ko.ko_id, lowsg->sg_ko.ko_id), 0,
		       low_bytes / PGSIZE, 0,
		       SEGMAP_EXEC | SEGMAP_WRITE | SEGMAP_READ);
    if (r < 0) {
	cprintf("IMG: cannot map low segment\n");
	return r;
    }
    // Alloc and map the VGA memory
    struct Segment *vgasg;
    r = segment_alloc(&vgasg, ps->ps_ko.ko_pid);
    if (r < 0) {
	cprintf("IMG: cannot alloc VGA segment: %s\n", e2s(r));
	return r;
    }
    locked_void_call(kobject_set_name, &vgasg->sg_ko, "VGA-sg");
    assert(share_import_obj(sh, (struct kobject *) vgasg) == 0);

    uint64_t vga_npages = (DEVPHYSMEM - VGAPHYSMEM) / PGSIZE;
    void *vga_kva[vga_npages];
    for (uint64_t i = 0; i < vga_npages; i++) {
	r = page_get_hw((i << PGSHIFT) + VGAPHYSMEM, &vga_kva[i]);
	if (r < 0) {
	    cprintf("IMG: cannot get VGA page %ld: %s\n", i, e2s(r));
	    return r;
	}
    }

    r = locked_call(segment_set_pages, vgasg, vga_kva, vga_npages);
    if (r < 0) {
	cprintf("IMG: cannot set VGA pages: %s\n", e2s(r));
	return r;
    }

    r = elf_add_segmap(at, &segmap_i,
		       SOBJ(sh->sh_ko.ko_id, vgasg->sg_ko.ko_id), 0,
		       vga_npages, (void *) VGAPHYSMEM,
		       SEGMAP_WRITE | SEGMAP_READ | SEGMAP_HW);
    if (r < 0) {
	cprintf("IMG: cannot map VGA segment\n");
	return r;
    }
    // Alloc and map extended physical memory at EXTPHYSMEM
    struct Segment *extsg;
    r = segment_create_embed(sh, ps, ext_bytes, 0, 0, 0, &extsg);
    if (r < 0) {
	cprintf("IMG: cannot create ext segment: %s\n", e2s(r));
	return r;
    }

    r = elf_add_segmap(at, &segmap_i,
		       SOBJ(sh->sh_ko.ko_id, extsg->sg_ko.ko_id), 0,
		       ext_bytes / PGSIZE, (void *) EXTPHYSMEM,
		       SEGMAP_EXEC | SEGMAP_WRITE | SEGMAP_READ);
    if (r < 0) {
	cprintf("IMG: cannot map ext segment\n");
	return r;
    }

    segment_fill(lowsg, SETUP_LOAD_ADDR, binary, setup_bytes);
    segment_fill(extsg, kloadaddr - EXTPHYSMEM, binary + kernel_off,
		 kernel_bytes);

    struct u_context uc;
    memset(&uc, 0, sizeof(uc));
    uc.uc_at = SOBJ(sh->sh_ko.ko_id, at->at_ko.ko_id);
    uc.uc_mode = ps_mode_vm;
    uc.uc_vm_nbytes = gmem_pages * PGSIZE;
    uc.uc_entry = (void *) (uintptr_t) SETUP_LOAD_ADDR + 512;

    assert_check(locked_call(processor_vector, ps, &uc));
    return 0;
}

static void
user_bootstrap(void)
{
    if (!embed_bins[0].name)
	panic("Cannot find a bootstrap OS");

    struct Share *sh;
    assert(share_alloc(&sh, ~0, 0) == 0);

    struct Processor *ps;
    assert(processor_alloc(&ps, 0) == 0);
    kobject_set_name(&ps->ps_ko, "bootstrap-ps");
    assert(processor_add_share(ps, sh) == 0);
    assert(share_import_obj(sh, (struct kobject *) ps) == 0);

    struct Address_tree *at;
    assert(at_alloc(&at, 0, ps->ps_ko.ko_pid) == 0);
    kobject_set_name(&at->at_ko, "boostrap-at");
    assert(share_import_obj(sh, (struct kobject *) at) == 0);

    cprintf("Loading %s as boostrap OS\n", embed_bins[0].name);

    if (strstr(embed_bins[0].name, ".img"))
	assert_check(load_img(sh, ps, at,
			      embed_bins[0].buf, embed_bins[0].size));
    else
	assert_check(load_elf(sh, ps, at,
			      embed_bins[0].buf, embed_bins[0].size));
}

void
user_init(void)
{
    user_bootstrap();
}
