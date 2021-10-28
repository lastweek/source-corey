#include <machine/mmu.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/jnic.h>
#include <inc/device.h>
#include <inc/error.h>
#include <inc/assert.h>
#include <inc/array.h>
#include <inc/jsysmon.h>

#include <string.h>

enum { debug_sysmon = 0 };

int
jnic_real_setup(struct jnic *jn, struct sobj_ref nic_ref)
{
    memset(jn, 0, sizeof(*jn));
    jn->jn_dev = &jdevreal;
    jn->nic_ref = nic_ref;
    return 0;
}

static int
jdev_real_init(struct jnic *jn)
{
    // Allocate and setup rx/tx buffers
    int r = segment_alloc(core_env->sh, 
			  JNIC_REAL_BUFS * PGSIZE, 
			  &jn->jn_real.buf_seg, 
			  &jn->jn_real.buf_base, 
			  0, 
			  "lwip rx/tx buffer", 
			  core_env->pid);
    if (r < 0)
	return r;

    for (int i = 0; i < JNIC_REAL_BUFS; i++) {
	jn->jn_real.rx[i] = jn->jn_real.buf_base + i * PGSIZE;
	jn->jn_real.rx[i]->size = 2000;
	jn->jn_real.rx[i]->actual_count = -1;
	
	jn->jn_real.tx[i] = jn->jn_real.buf_base + i * PGSIZE + 2048;
	jn->jn_real.tx[i]->actual_count = NETHDR_COUNT_DONE;
    }

    jn->jn_real.tx_next = 0;
    jn->jn_real.rx_head = -1;
    jn->jn_real.rx_tail = -1;

    if (jn->jsm_ref.object) {
	r = jsm_setup(&jn->jsm, jn->jsm_ref);
	if (r < 0)
	    return r;
	r = jsm_set_nic(&jn->jsm, jn->nic_ref, jn->jn_real.buf_seg);
	if (r < 0)
	    cprintf("jdev_real_init: unable to set jsm nic: %s\n", e2s(r));
    }
    return 0;
}

static int
jdev_real_mac(struct jnic *jn, uint8_t *mac)
{
    struct u_device_stat uds;
    int r = sys_device_stat(jn->nic_ref, &uds);
    if (r < 0)
	return r;
    memcpy(mac, uds.nic.hwaddr, 6);
    return 0;
}

static int
jdev_real_txbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    int txslot = jn->jn_real.tx_next;
    if (!(jn->jn_real.tx[txslot]->actual_count & NETHDR_COUNT_DONE))
	return -E_NO_SPACE;

    *ret = jn->jn_real.tx[txslot];
    return 0;
}

static int
jdev_real_txbuf_done(struct jnic *jn)
{
    void *txbase = jn->jn_real.tx[jn->jn_real.tx_next];
    jn->jn_real.tx_next = (jn->jn_real.tx_next + 1) % JNIC_REAL_BUFS;
    
    if (jn->jsm_ref.object) {
	struct jsm_slot *slot;
	int r = jsm_next_slot(&jn->jsm, &slot);
	if (r == 0) {
	    jsm_call_device_buf(slot, jn->nic_ref, jn->jn_real.buf_seg, 
				(uint64_t) (txbase - jn->jn_real.buf_base), 
				devio_out);
	    return 0;
	} else {
	    if (debug_sysmon)
		cprintf("jdev_real_txbuf_done: jsm_next_slot failed: %s\n", 
			e2s(r));
	}
    }
    
    return sys_device_buf(jn->nic_ref, jn->jn_real.buf_seg, 
			  (uint64_t) (txbase - jn->jn_real.buf_base), 
			  devio_out);
}

static int
jdev_real_feed(struct jnic *jn)
{
    int ss = (jn->jn_real.rx_tail >= 0 ? (jn->jn_real.rx_tail + 1) % JNIC_REAL_BUFS : 0);
    for (int i = ss; i != jn->jn_real.rx_head; i = (i + 1) % JNIC_REAL_BUFS) {
	void *bufaddr = jn->jn_real.rx[i];
	jn->jn_real.rx[i]->actual_count = 0;

	int r = -1;
	if (jn->jsm_ref.object) {
	    struct jsm_slot *slot;
	    r = jsm_next_slot(&jn->jsm, &slot);
	    if (r == 0) {
		jsm_call_device_buf(slot, jn->nic_ref, 
				    jn->jn_real.buf_seg,
				    (uint64_t) (bufaddr - jn->jn_real.buf_base), 
				    devio_in);
	    } else {
		if (debug_sysmon)
		    cprintf("jdev_real_feed: jsm_next_slot failed: %s\n", 
			    e2s(r));
	    }
	}

	if (r < 0)
	    r = sys_device_buf(jn->nic_ref, jn->jn_real.buf_seg,
			       (uint64_t) (bufaddr - jn->jn_real.buf_base), 
			       devio_in);
	if (r < 0) {
	    cprintf("jdev_real_feed: cannot feed rx packet: %s\n", e2s(r));
	    return r;
	}

	jn->jn_real.rx_tail = i;
	if (jn->jn_real.rx_head == -1)
	    jn->jn_real.rx_head = i;
    }

    return 0;
}

static int
jdev_real_rxbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    while (jn->jn_real.rx_head < 0 || 
	   !(jn->jn_real.rx[jn->jn_real.rx_head]->actual_count & 
	     NETHDR_COUNT_DONE)) 
    {
	jdev_real_feed(jn);

	if (jn->jn_real.rx_head < 0)
	    continue;

	while (!(jn->jn_real.rx[jn->jn_real.rx_head]->actual_count & 
		 NETHDR_COUNT_DONE))
	    thread_yield();

	if (jn->jn_real.rx[jn->jn_real.rx_head]->actual_count & 
	    NETHDR_COUNT_RESET) 
	{
	    // All buffers have been cleared
	    for (int i = 0; i < JNIC_REAL_BUFS; i++)
		jn->jn_real.tx[i]->actual_count = NETHDR_COUNT_DONE;
	    jn->jn_real.rx_head = -1;
	    jn->jn_real.rx_tail = -1;
	}
    }
    
    *ret = jn->jn_real.rx[jn->jn_real.rx_head];
    return 0;
}

static int
jdev_real_rxbuf_done(struct jnic *jn, struct netbuf_hdr *nb)
{
    if (jn->jn_real.rx_head == jn->jn_real.rx_tail)
	jn->jn_real.rx_head = -1;
    else
	jn->jn_real.rx_head = (jn->jn_real.rx_head + 1) % JNIC_REAL_BUFS;
    return jdev_real_feed(jn);
}

struct jdev jdevreal = {
    .jdev_init = jdev_real_init,
    .jdev_mac = jdev_real_mac,
    .jdev_txbuf_next = jdev_real_txbuf_next,
    .jdev_txbuf_done = jdev_real_txbuf_done,
    .jdev_rxbuf_next = jdev_real_rxbuf_next,
    .jdev_rxbuf_done = jdev_real_rxbuf_done,
};
