#include <kern/lib.h>
#include <kern/arch.h>
#include <kern/lockmacro.h>
#include <machine/atomic.h>
#include <dev/vnic.h>
#include <inc/error.h>

enum { vnic_print_mac = 1 };
enum { vnic_print_noring = 0 };
enum { vnic_print_overrun = 0 };

struct vnic_master;

struct vnic_card {
    struct nic nic;
    struct vnic_master *master;
    struct vnic_ring *rx_ring;
    int rx_ring_next;
};

struct vnic_master {
    struct interrupt_handler ih;
    struct interrupt_handler *real_ih;

    // Important stuff about the real NIC
    struct nic *real_nic;
    uint32_t irq;

    int (*mac_index)(void *, uint8_t *);
    struct vnic_card card[VNIC_MAX];
    uint32_t ncard;
};

static void
vnic_buffer_reset(void *a)
{
    struct nic *nic = ((struct vnic_card *)a)->master->real_nic;
    nic->nc_reset(nic->nc_arg);
}

static void
vnic_intr(struct Processor *ps, void *arg)
{
    struct interrupt_handler *ih = ((struct vnic_master *)arg)->real_ih;
    ih->ih_func(ps, ih->ih_arg);
}

static void
vnic_poll(void *a)
{
    struct nic *nic = ((struct vnic_card *)a)->master->real_nic;
    nic->nc_poll(nic->nc_arg);
}

static int 
vnic_add_txbuf(void *a, uint64_t sg_id, uint64_t offset,
	       struct netbuf_hdr *nb, uint16_t size)
{
    struct nic *nic = ((struct vnic_card *)a)->master->real_nic;
    return nic->nc_add_txbuf(nic->nc_arg, sg_id, offset, nb, size);
}

static int 
vnic_add_rxbuf(void *a, uint64_t sg_id, uint64_t offset,
	       struct netbuf_hdr *nb, uint16_t size)
{
    struct nic *nic = ((struct vnic_card *)a)->master->real_nic;
    return nic->nc_add_rxbuf(nic->nc_arg, sg_id, offset, nb, size);
}

static void
vnic_intr_rx_cb(void *a, uint64_t sg_id, uint64_t offset, 
		struct netbuf_hdr *nb, uint16_t size)
{
    struct vnic_master *m = a;

    if (size < 6) {
	cprintf("vnic_intr_rx_cb: funny packet size: %u\n", size);
	return;
    }

    if (jos_atomic_read(&nb->ref))
	cprintf("vnic_intr_rx_cb: nb is referenced, offset: %lx\n", offset);

    uint8_t *buf = (uint8_t *)(nb + 1);

    // Check if a broadcast packet
    if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF &&
	buf[3] == 0xFF && buf[4] == 0xFF && buf[5] == 0xFF)
    {
	jos_atomic_set(&nb->ref, m->ncard);

	for (uint32_t k = 0; k < m->ncard; k++) {
	    struct vnic_card *c = &m->card[k];
	    if (!c->rx_ring) {
		if (vnic_print_noring)
		    cprintf("vnic_intr_rx_cb: no rx_ring on %u\n", k);
		jos_atomic_dec(&nb->ref);
		continue;
	    }
	    
	    int next = c->rx_ring_next;
	    if (c->rx_ring->slot[next].offset & VNICRING_OFFSET_DONE) {
		if (vnic_print_overrun)
		    cprintf("vnic_intr_rx_cb: rx_ring overrun on %u\n", k);
		jos_atomic_dec(&nb->ref);
		continue;
	    }

	    c->rx_ring->slot[next].sg_id = sg_id;
	    c->rx_ring->slot[next].offset = offset;
	    c->rx_ring->slot[next].offset |= VNICRING_OFFSET_DONE;
	    c->rx_ring_next = (next + 1) % array_size(c->rx_ring->slot);
	}
	return;
    }

    // Check if for one of the vnics
    int i = m->mac_index(m->real_nic, buf);
    if (i >= 0) {
	struct vnic_card *c = &m->card[i];
	if (!c->rx_ring) {
	    if (vnic_print_noring)
		cprintf("vnic_intr_rx_cb: no rx_ring on %u\n", i);
	    return;
	}

	int next = c->rx_ring_next;
	if (c->rx_ring->slot[next].offset & VNICRING_OFFSET_DONE) {
	    if (vnic_print_overrun)
		cprintf("vnic_intr_rx_cb: rx_ring overrun on %u\n", i);
	    return;
	}

	jos_atomic_set(&nb->ref, 1);
	c->rx_ring->slot[next].sg_id = sg_id;
	c->rx_ring->slot[next].offset = offset;
	c->rx_ring->slot[next].offset |= VNICRING_OFFSET_DONE;
	c->rx_ring_next = (next + 1) % array_size(c->rx_ring->slot);
	return;
    }

    cprintf("vnic_intr_rx_cb: funny MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
}

static int
vnic_conf(void *a, struct u_device_conf *udc)
{
    int r;
    struct vnic_card *c = a;
    static_assert(PGSIZE >= sizeof(struct vnic_ring));
    
    if (udc->type == device_conf_vnic_ring) {
	struct Processor *ps = processor_sched();
	struct Segment *sg;
	r = processor_co_obj(ps, udc->vnic_ring.seg, 
			     (struct kobject **)&sg, kobj_segment);
	if (r < 0)
	    return r;

	struct vnic_ring *ring;
	r = segment_get_page(sg,
			     udc->vnic_ring.offset / PGSIZE, 
			     (void **) &ring, page_shared_cow);
	if (r < 0) 
	    return r;
	if (PGSIZE - PGOFF(udc->vnic_ring.offset) < sizeof(*ring))
	    return -E_INVAL;

	pagetree_incref_hw(ring);
	c->rx_ring = ring;
	return 0;
    }
    return -E_INVAL;
}

int
vnic_attach(struct nic *nic, 
	    uint32_t irq, struct interrupt_handler *ih,
	    uint32_t num, 
	    int (*mac_index)(void *arg, uint8_t *mac_addr),
	    void (*index_mac)(void *arg, int index, uint8_t *mac_addr))
{
    struct vnic_master *m;
    int r = page_alloc((void **) &m, 0);
    if (r < 0)
	return r;
    
    memset(m, 0, sizeof(*m));
    static_assert(PGSIZE >= sizeof(*m));

    m->ih.ih_func = &vnic_intr;
    m->ih.ih_arg = m;
    m->real_ih = ih;
    m->real_nic = nic;
    m->irq = irq;
    m->mac_index = mac_index;
    m->ncard = num;
    
    irq_register(irq, &m->ih);

    nic->nc_cb_arg = m;
    nic->nc_intr_rx_cb = &vnic_intr_rx_cb;
    
    for (uint32_t i = 0; i < num; i++) {
	struct vnic_card *c = &m->card[i];

	c->master = m;

	c->nic.nc_arg = c;
	c->nic.nc_add_txbuf = &vnic_add_txbuf;
	c->nic.nc_add_rxbuf = &vnic_add_rxbuf;
	c->nic.nc_reset = &vnic_buffer_reset;
	c->nic.nc_poll = &vnic_poll;
	c->nic.nc_conf = &vnic_conf;
	c->nic.nc_irq_line = irq;
	
	index_mac(nic->nc_arg, i, c->nic.nc_hwaddr);
	
	nic_register(&c->nic, device_vnic);
	
	if (vnic_print_mac)
	    cprintf("vnic: %02x:%02x:%02x:%02x:%02x:%02x\n",
		c->nic.nc_hwaddr[0], c->nic.nc_hwaddr[1],
		c->nic.nc_hwaddr[2], c->nic.nc_hwaddr[3],
		c->nic.nc_hwaddr[4], c->nic.nc_hwaddr[5]);
    }

    nic->nc_reset(nic->nc_arg);
    return 1;
}
