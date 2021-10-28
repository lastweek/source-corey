#include <kern/console.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/lockmacro.h>
#include <inc/error.h>
#include <inc/pad.h>
#include <inc/spinlock.h>

static LIST_HEAD(cd_list, cons_device) cdevs;

enum { cons_buffer_out = 1 };
enum { cons_bufsize = 512 };

struct cons_buf {
    uint32_t count;
    int buf[cons_bufsize];
    cons_source src;
};

static struct cons_buf outbuf[JOS_NCPU];

static void
cons_flush_outbuf(struct cons_buf *b)
{
    struct cons_device *cd;
    LIST_FOREACH(cd, &cdevs, cd_link)
	if (cd->cd_output) {
	    spin_lock(&cd->cd_out_lock);
	    for (uint32_t i = 0; i < b->count; i++)
		cd->cd_output(cd->cd_arg, b->buf[i], b->src);
	    spin_unlock(&cd->cd_out_lock);
	}
    b->count = 0;
}

void
cons_putc(int c, cons_source src)
{
    if (cons_buffer_out) {
	uint32_t cpu = arch_cpu();
	struct cons_buf *b = &outbuf[cpu];
	if (!SAFE_EQUAL(src, outbuf[cpu].src))
	    cons_flush_outbuf(b);
	
	b->src = src;
	b->buf[b->count++] = c;
	
	if (c == '\n' || b->count == cons_bufsize)
	    cons_flush_outbuf(b);
    } else {
	struct cons_device *cd;
	LIST_FOREACH(cd, &cdevs, cd_link)
	    if (cd->cd_output)
		cd->cd_output(cd->cd_arg, c, src);
    }
}

void
cons_flush(void)
{
    if (cons_buffer_out)
	cons_flush_outbuf(&outbuf[arch_cpu()]);	
}

void
cons_intr(struct cons_device *cd)
{
    int c;

    while ((c = cd->cd_pollin(cd->cd_arg)) != -1) {
	if (c == 0)
	    continue;

	if (cd->cd_cons_sg) {
	    struct cons_entry *cd_cons;
	    int r = segment_get_page(cd->cd_cons_sg,
				     cd->cd_cons_npage, 
				     (void **)&cd_cons, page_shared_cow);
	    if (r < 0) {
		cprintf("cons_intr: couldn't get page: %s\n", e2s(r));
	    } else {
		cd_cons[cd->cd_consi].code = c;
		cd_cons[cd->cd_consi].status |= KEY_STATUS_PRESSED;
		cd->cd_consi = (cd->cd_consi + 1) % (PGSIZE / sizeof(*cd_cons));
	    }
	}
    }
}

static int
cons_feed(void *a, struct Segment *sg, uint64_t offset, struct devbuf_hdr *db, 
	 devio_type type)
{
    struct cons_device *cd = a;
    
    if (type == devio_out)
	return -E_INVAL;
    
    if (offset % PGSIZE)
	return -E_INVAL;

    cd->cd_cons_sg = sg;
    cd->cd_cons_npage = offset / PGSIZE;
    kobject_incref(&sg->sg_ko);
    cd->cd_consi = 0;
    return 0;
}

static void
cons_reset(void *a)
{
    struct cons_device *cd = a;
    if (cd->cd_cons_sg) {
	kobject_decref(&cd->cd_cons_sg->sg_ko);
	cd->cd_cons_sg = 0;
    }
    cd->cd_consi = 0;
}

void
cons_register(struct cons_device *cd)
{
    LIST_INSERT_HEAD(&cdevs, cd, cd_link);
    cd->cd_handler.dh_arg = cd;
    cd->cd_handler.dh_feed = cons_feed;
    cd->cd_handler.dh_reset = cons_reset;
    cd->cd_handler.dh_hdr.type = device_cons;
    spin_init(&cd->cd_out_lock);
    device_register(&cd->cd_handler);
}
