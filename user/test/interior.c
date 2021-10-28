#include <machine/x86.h>
#include <inc/stdio.h>
#include <stdlib.h>
#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/error.h>

#include <inc/syscall.h>
#include <inc/lib.h>
#include <test.h>
#include <string.h>

enum { mem_bytes = 4 * 4096 };
enum { seg_size = 4 * mem_bytes };
//enum { seg_size = 0x80000000 };
#define VA_BASE  ULINKSTART

void
interior_test(void)
{
    /* allocat the ummap shared state */
    int64_t shid;
    void * va;
    echeck(shid = sys_share_alloc(core_env->sh, 1 << kobj_segment,
    				  "test-share", core_env->pid));
        
    struct sobj_ref shref = SOBJ(core_env->sh, shid);
    /*alllocate the segment */
    int64_t sgid;
    echeck(sgid = sys_segment_alloc(shid, seg_size, "test-seg", core_env->pid));
    struct sobj_ref sgref = SOBJ(shid, sgid);

    uint64_t map_size = seg_size / 2;
    
    /*create the address tree for ummap */    
    for (uint64_t i = 0;  i <  2 ; i ++) {        
        int64_t atid;
        echeck(atid = sys_at_alloc(core_env->sh, 1, "at-test", core_env->pid));
        struct sobj_ref atref = SOBJ(core_env->sh, atid);
        
        va = (void *) (VA_BASE + i * PISIZE);
        echeck(at_map_interior(atref, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP, va ));  
        cprintf("adding mapping to interior... %p\n", va);
        struct u_address_mapping mapping ;
        memset(&mapping, 0, sizeof(mapping));

        mapping.type = address_mapping_segment;
        mapping.object = sgref;
        mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
        mapping.kslot = i;
        mapping.va = 0;
        mapping.start_page = i * (map_size / PGSIZE);
        mapping.num_pages = map_size/PGSIZE;
        echeck(sys_at_set_slot(atref, &mapping));    
    }

    int64_t r;
    cprintf("pforking...\n");
    echeck(r = pforkv(1, PFORK_SHARE_HEAP, &shref, 1));
    if (r == 0) {
        cprintf("pforked...\n");
        va = (void *)VA_BASE;
        memset(va, 0, mem_bytes);
        va = (void *)(VA_BASE + PISIZE);
        memset(va, 0, mem_bytes);
        processor_halt();
    } else {
        cprintf("parent...\n");
        uint64_t x = read_tsc();
        while (read_tsc() - x < 10000000000);
        va = (void *)(VA_BASE + mem_bytes);
        memset(va, 0, mem_bytes);
        va = (void *)(VA_BASE + PISIZE +  mem_bytes);
        memset(va, 0, mem_bytes);
        cprintf("parent done!\n");
    }
}


