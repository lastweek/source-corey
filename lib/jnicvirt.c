#include <machine/mmu.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/jnic.h>
#include <inc/stdio.h>
#include <inc/array.h>
#include <inc/error.h>

#include <string.h>

enum { verbose = 0 };

int
jnic_virt_segment(uint64_t sh_id, 
		  int n, void **buf_start, struct sobj_ref *buf_ref)
{
    return segment_alloc(sh_id, n * PAGES_PER_VIRT * PGSIZE, 
			 buf_ref, buf_start, SEGMAP_SHARED, 
			 "jnic virt rx/tx buffer", core_env->pid);
}

int
jnic_virt_setup(struct jnic *jn, struct sobj_ref nic_ref, 
		void *buf_start, struct sobj_ref buf_ref, int buf_i)
{
    memset(jn, 0, sizeof(*jn));

    jn->nic_ref = nic_ref;
    jn->jn_virt.buf_seg = buf_ref;
    jn->jn_virt.buf_base = buf_start;
    jn->jn_virt.my_base = buf_start + (buf_i * PAGES_PER_VIRT * PGSIZE);
    
    for (int i = 0; i < PAGES_PER_VIRT; i++) {
	void *bufaddr = jn->jn_virt.my_base + i * PGSIZE;
	struct netbuf_hdr *nb = bufaddr;
	nb->size = 2000;
	nb->actual_count = 0;
	int r = sys_device_buf(jn->nic_ref, jn->jn_virt.buf_seg,
			       (uint64_t) (bufaddr - jn->jn_virt.buf_base), 
			       devio_in);
	if (r < 0) {
	    cprintf("jnic_virt_setup: failed to add rx buf: %s\n", e2s(r));
	    return r;
	}
	
	jn->jn_virt.tx[i] = jn->jn_virt.my_base + i * PGSIZE + 2048;
	jn->jn_virt.tx[i]->actual_count = NETHDR_COUNT_DONE;
    }
    
    jn->jn_virt.tx_next = 0;
   
    // XXX these should really be an id to lookup a device
    jn->jn_dev = &jdevvirt2;
    return 0;
}

static int
jdev_virt_init(struct jnic *jn)
{
    // XXX should addref segments and nic_ref into core_env->sh
    struct sobj_ref ring_seg;
    void *va = 0;
    
    int r = segment_alloc(core_env->sh, PGSIZE, &ring_seg, &va, 
			  0, "jnic virt ring", core_env->pid);
    if (r < 0)
	return r;
    memset(va, 0, PGSIZE);
    
    struct u_device_conf udc;
    udc.type = device_conf_vnic_ring;
    udc.vnic_ring.seg = ring_seg;
    udc.vnic_ring.offset = 0;
    r = sys_device_conf(jn->nic_ref, &udc);
    if (r < 0)
	return r;

    jn->jn_virt.ring_seg = ring_seg;
    jn->jn_virt.rx_ring = va;
    jn->jn_virt.rx_ring_next = 0;

    if (jn->jsm_ref.object) {
	r = jsm_setup(&jn->jsm, jn->jsm_ref);
	if (r < 0)
	    return r;
	r = jsm_set_nic(&jn->jsm, jn->nic_ref, jn->jn_virt.buf_seg);
	if (r < 0)
	    cprintf("jdev_virt_init: unable to set jsm nic: %s\n", e2s(r));
    }
    return 0;
}

static int
jdev_virt_mac(struct jnic *jn, uint8_t *mac)
{
    struct u_device_stat uds;
    int r = sys_device_stat(jn->nic_ref, &uds);
    if (r < 0)
	return r;
    memcpy(mac, uds.nic.hwaddr, 6);
    return 0;
}

static int 
jdev_virt_txbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    int txslot = jn->jn_virt.tx_next;
    if (!(jn->jn_virt.tx[txslot]->actual_count & NETHDR_COUNT_DONE))
	return -E_NO_SPACE;
    
    *ret = jn->jn_virt.tx[txslot];
    return 0;
}

static int 
jdev_virt_txbuf_done(struct jnic *jn)
{
    void *txbase = jn->jn_virt.tx[jn->jn_virt.tx_next];
    jn->jn_virt.tx_next = (jn->jn_virt.tx_next + 1) % PAGES_PER_VIRT;

    if (jn->jsm_ref.object) {
	struct jsm_slot *slot;
	int r = jsm_next_slot(&jn->jsm, &slot);
	if (r == 0) {
	    jsm_call_device_buf(slot, jn->nic_ref, jn->jn_virt.buf_seg, 
				(uint64_t) (txbase - jn->jn_virt.buf_base), 
				devio_out);
	    return 0;
	} else {
	    if (verbose)
		cprintf("jdev_virt_txbuf_done: jsm_next_slot failed: %s\n", e2s(r));
	}
    }
    return sys_device_buf(jn->nic_ref, jn->jn_virt.buf_seg, 
			  (uint64_t) (txbase - jn->jn_virt.buf_base), 
			  devio_out);
}

static int 
jdev_virt_rxbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    struct vnic_ring *rx_ring = jn->jn_virt.rx_ring;
    int next = jn->jn_virt.rx_ring_next;
    
    while (!(rx_ring->slot[next].offset & VNICRING_OFFSET_DONE))
	thread_yield();

    assert(rx_ring->slot[next].sg_id == jn->jn_virt.buf_seg.object);

    struct netbuf_hdr *nb = 
	(jn->jn_virt.buf_base + 
	 (rx_ring->slot[next].offset & VNICRING_OFFSET_MASK));
    rx_ring->slot[next].offset = 0;
    jn->jn_virt.rx_ring_next = (next + 1) % array_size(rx_ring->slot);

    *ret = nb;
    return 0;
}

static int 
jdev_virt_rxbuf_done(struct jnic *jn, struct netbuf_hdr *nb)
{
    if (jos_atomic_dec_and_test(&nb->ref)) {
	nb->actual_count = 0;
	void *bufaddr = nb;

	int r = -1;
	if (jn->jsm_ref.object) {
	    struct jsm_slot *slot;
	    r = jsm_next_slot(&jn->jsm, &slot);
	    if (r == 0) {
		jsm_call_device_buf(slot, jn->nic_ref, 
				    jn->jn_virt.buf_seg,
				    (uint64_t) (bufaddr - jn->jn_virt.buf_base), 
				    devio_in);
	    } else {
		if (verbose)
		    cprintf("jdev_virt_rxbuf_done: jsm_next_slot failed: %s\n", 
			    e2s(r));
	    }
	}

	if (r < 0)
	    r = sys_device_buf(jn->nic_ref, jn->jn_virt.buf_seg,
			       (uint64_t) (bufaddr - jn->jn_virt.buf_base), 
			       devio_in);
	if (r < 0) {
	    cprintf("jdev_virt_rxbuf_done: cannot feed rx packet: %s\n", e2s(r));
	    return r;
	}
    }
    return 0;
}

struct jdev jdevvirt2 = {
    .jdev_init = jdev_virt_init,
    .jdev_mac = jdev_virt_mac,
    .jdev_txbuf_next = jdev_virt_txbuf_next,
    .jdev_txbuf_done = jdev_virt_txbuf_done,
    .jdev_rxbuf_next = jdev_virt_rxbuf_next,
    .jdev_rxbuf_done = jdev_virt_rxbuf_done,
};
