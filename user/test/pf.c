#include <test.h>

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

#define VA_BASE ULINKSTART

enum { num_cores = 16 };
enum { buf_pages = 64 * 4096 };

static struct {
    volatile char start;

    union {
	volatile uint64_t time;
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU] __attribute__((aligned(JOS_CLINE)));
} *shared;

volatile char *page_buf;

static void
do_test(int core)
{
    if (core != 0)
	while (!shared->start);
    else
	shared->start = 1;
    
    uint64_t s = read_tsc();

    int k = core * (buf_pages / num_cores);
    int count = 0;
    while (count != buf_pages) {
	page_buf[(k * PGSIZE) + core] = core;
	k = (k + 1) % buf_pages;
	count++;
    }
    shared->cpu[core].time = read_tsc() - s;
}

void
pf_test(void)
{

    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));
    
    /* allocat the ummap shared state */
    int64_t shid;
    void * va;
    echeck(shid = sys_share_alloc(core_env->sh, 1 << kobj_segment,
    				  "test-share", core_env->pid));
        
    struct sobj_ref shref = SOBJ(core_env->sh, shid);
    /*alllocate the segment */
    int64_t sgid;
    echeck(sgid = sys_segment_alloc(shid, buf_pages * PGSIZE, 
				    "test-seg", core_env->pid));
    struct sobj_ref sgref = SOBJ(shid, sgid);

    /*create the address tree for ummap */    
    int64_t atid;
    echeck(atid = sys_at_alloc(core_env->sh, 1, "at-test", core_env->pid));
    struct sobj_ref atref = SOBJ(core_env->sh, atid);
        
    va = (void *) VA_BASE;
    echeck(at_map_interior(atref, SEGMAP_READ | SEGMAP_WRITE | SEGMAP_HEAP, va ));
    cprintf("adding mapping to interior...\n");
    struct u_address_mapping mapping ;
    memset(&mapping, 0, sizeof(mapping));
    
    mapping.type = address_mapping_segment;
    mapping.object = sgref;
    mapping.flags = SEGMAP_READ | SEGMAP_WRITE ;
    mapping.kslot = 0;
    mapping.va = 0;
    mapping.start_page = 0;
    mapping.num_pages = buf_pages;
    echeck(sys_at_set_slot(atref, &mapping));    

    page_buf = va;

    for (int i = 1; i < num_cores; i++) {
	int64_t r;
	echeck(r = pforkv(i, PFORK_SHARE_HEAP, &shref, 1));
	if (r == 0){
	    do_test(i);
	    processor_halt();
	}
    }

    uint64_t s = read_tsc();
    while (read_tsc() - s < 100000000);

    cprintf("pre touching...\n");
    // Avoid TLB shootdowns
    *((char *)va) = 1;

    cprintf("starting...\n");
    do_test(0);

    uint64_t max_time = 0;
    uint64_t total_time = 0;
    for (int i = 0; i < num_cores; i++) {
	while (shared->cpu[i].time == 0);
	total_time += shared->cpu[i].time;
	if (shared->cpu[i].time > max_time)
	    max_time = shared->cpu[i].time;
    }
    
    cprintf("num cores %d\n", num_cores);
    cprintf("max cycles %ld\n", max_time);
    cprintf("ave cycles %ld\n", total_time / num_cores);
    
    as_unmap(shared);
    sys_share_unref(seg);
}
