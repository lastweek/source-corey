#include <machine/mmu.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/assert.h>

#include <sys/mman.h>
#include <bits/unimpl.h>

libc_hidden_proto(mmap)
libc_hidden_proto(munmap)
libc_hidden_proto(mremap)

void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    if (!(flags & MAP_ANONYMOUS)) {
        __set_errno(ENOSYS);
        cprintf("mmap: only MAP_ANONYMOUS supported!\n");
        return MAP_FAILED;
    } else if (flags & MAP_FIXED) {
	__set_errno(ENOSYS);
        cprintf("mmap: MAP_FIXED is not supported!\n");
        return MAP_FAILED;
    }

    uint32_t seg_flags;
    seg_flags = SEGMAP_READ;
    if ((prot & PROT_EXEC))
	seg_flags |= SEGMAP_EXEC;
    if ((prot & PROT_WRITE))
	seg_flags |= SEGMAP_WRITE;

    int64_t sg = sys_segment_alloc(core_env->sh, len, "anon mmap", 
				   core_env->pid);
    if (sg < 0) {
        __set_errno(ENOMEM);
        return MAP_FAILED;
    }
    
    void *va = 0;
    int r = as_map(SOBJ(core_env->sh, sg), 0, seg_flags, &va, 0);
    if (r < 0) {
	sys_share_unref(SOBJ(core_env->sh, sg));
	__set_errno(ENOMEM);
	return MAP_FAILED;
    }

    return va;
}

int 
munmap(void *addr, size_t len)
{
    assert(!PGOFF(addr));
    len = ROUNDUP(len, PGSIZE);
    
    struct u_address_mapping omap;
    int r = as_lookup(addr, &omap);
    if (r < 0) {
        __set_errno(EINVAL);
	return -1;
    }
    
    if (len != omap.num_pages * PGSIZE) {
        cprintf("munmap: partial unmap not supported!");
        __set_errno(ENOSYS);
        return -1;
    }
    
    r = as_unmap(addr);
    if (r < 0) {
        cprintf("munmap: as_unmap failed: %s\n", e2s(r));
        __set_errno(EINVAL);
        return -1;
    }

    // If use (un)mmap interface, assume segment should be dropped
    r = sys_share_unref(omap.object);
    if (r < 0)
        cprintf("munmap: sys_processor_unref failed: %s\n", e2s(r));
    return 0;
}

void *
mremap (void *__addr, size_t __old_len, size_t __new_len,
	int __flags, ...)
{
    set_enosys();
    return MAP_FAILED;
}

libc_hidden_def(mmap)
libc_hidden_def(munmap)
libc_hidden_def(mremap)
