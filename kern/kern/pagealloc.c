#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/pageinfo.h>
#include <kern/lock.h>
#include <inc/queue.h>
#include <inc/error.h>
#include <inc/pad.h>

enum { page_stats_enable = 0 };
enum { scrub_free_pages = 0 };
enum { page_nomem_debug = 1 };
enum { page_alloc_debug = 0 };
enum { page_alloc_any   = 1 };

uint64_t global_npages;  // Amount of physical memory (in pages)

struct Page_link {
    TAILQ_ENTRY(Page_link) pp_link;	// free list link
};

TAILQ_HEAD(Page_list, Page_link);

static PAD_TYPE(struct Page_list, JOS_CLINE) page_free_list[JOS_NNODE];
static PAD_TYPE(struct spinlock, JOS_CLINE) page_free_list_lock[JOS_NNODE];

// Global page allocation stats
struct page_stats page_stats;

void
page_free(void *v)
{
    struct page_info *pi = page_to_pageinfo(v);
    if (pi->pi_hwpage)
	return;

    int node = arch_node_by_addr(kva2pa(v));
    assert(node >= 0);

    struct Page_link *pl = (struct Page_link *) v;
    if (PGOFF(pl))
	panic("page_free: not a page-aligned pointer %p", pl);

    assert(!pi->pi_freepage);
    pi->pi_freepage = 1;
    pi->pi_clear = 0;
    if (scrub_free_pages)
	memset(v, 0xde, PGSIZE);

    spin_lock(&page_free_list_lock[node].val);    
    TAILQ_INSERT_HEAD(&page_free_list[node].val, pl, pp_link);
    spin_unlock(&page_free_list_lock[node].val);
    if (page_stats_enable) {
	jos_atomic_inc64(&page_stats.pages_avail);
	jos_atomic_dec64(&page_stats.pages_used);
    }
}

int
page_alloc(void **vp, proc_id_t pid)
{
    int node;
    if (pid == 0xFFFFFFFF)
	node = arch_node_by_cpu(arch_cpu());
    else
	node = arch_node_by_cpu(pid);
    assert(node >= 0);

    spin_lock(&page_free_list_lock[node].val);
    struct Page_link *pl = TAILQ_FIRST(&page_free_list[node].val);

    if (!pl) {
	spin_unlock(&page_free_list_lock[node].val);
	if (page_alloc_any) {
	    for (int n = (node + 1) % nmemnode; n != node; n = (n + 1) % nmemnode) {
		spin_lock(&page_free_list_lock[n].val);
		pl = TAILQ_FIRST(&page_free_list[n].val);
		if (pl) {
		    node = n;
		    break;
		}
		spin_unlock(&page_free_list_lock[n].val);
	    }
	}
    }
    
    if (!pl) {
	if (page_nomem_debug)
	    cprintf("page_alloc: out of memory: used %"PRIu64" avail %"PRIu64
		    " alloc %"PRIu64" fail %"PRIu64"\n",
		    jos_atomic_read(&page_stats.pages_used), 
		    jos_atomic_read(&page_stats.pages_avail),
		    jos_atomic_read(&page_stats.allocations),
		    jos_atomic_read(&page_stats.failures));

	jos_atomic_inc64(&page_stats.failures);
	// Ideally we should return E_RESTART and eventually try to free
	// up some memory, but util we have a system for freeing memory
	// simply return E_NO_MEM
	return -E_NO_MEM;
    }

    TAILQ_REMOVE(&page_free_list[node].val, pl, pp_link);
    spin_unlock(&page_free_list_lock[node].val);

    *vp = pl;

    if (page_stats_enable) {
	jos_atomic_dec64(&page_stats.pages_avail);
	jos_atomic_inc64(&page_stats.pages_used);
	jos_atomic_inc64(&page_stats.allocations);
    }

    if (scrub_free_pages)
	memset(pl, 0xcd, PGSIZE);

    struct page_info *pi = page_to_pageinfo(pl);
    assert(pi->pi_freepage);
    pi->pi_freepage = 0;

    debug(page_alloc_debug, "%p", pl);
    return 0;
}

int
page_get_hw(physaddr_t pa, void **vp)
{
    void *kva = pa2kva(pa);
    struct page_info *pi = page_to_pageinfo(kva);
    if (!pi->pi_hwpage)
	return -E_INVAL;

    *vp = kva;
    return 0;
}

void
page_register_hw(void *v)
{
    struct page_info *pi = page_to_pageinfo(v);
    assert(!pi->pi_freepage);
    assert(!pi->pi_hwpage);
    pi->pi_hwpage = 1;
}

void
page_alloc_init(void)
{
    for (int i = 0; i < JOS_NNODE; i++) {
	TAILQ_INIT(&page_free_list[i].val);    
	spin_init(&page_free_list_lock[i].val);
    }
}
