#include <inc/syscall.h>
#include <inc/lib.h>

int
segment_alloc(uint64_t sh, uint64_t bytes, struct sobj_ref *sg, void **va_p, 
	      uint32_t flags, const char *name, proc_id_t pid)
{
    int64_t id = sys_segment_alloc(sh, bytes, name, pid);
    if (id < 0)
	return id;

    if (va_p) {
	uint64_t x = bytes;
	int r = as_map(SOBJ(sh, id), 0, SEGMAP_READ | SEGMAP_WRITE | flags, va_p, &x);
	if (r < 0) {
	    sys_share_unref(SOBJ(sh, id));
	    return r;
	}
    }

    if (sg)
	*sg = SOBJ(sh, id);
    return 0;
}
