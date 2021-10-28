#ifndef JOS_INC_JNIC_H
#define JOS_INC_JNIC_H

#include <inc/device.h>
#include <inc/jsysmon.h>

#define JNIC_REAL_BUFS	128
// One page fits one rx and one tx buffer
#define PAGES_PER_VIRT 64

struct jnic;

struct jdev {
    // inits the jnic for local use
    int (*jdev_init)(struct jnic *jn);

    // returns the mac address
    int (*jdev_mac)(struct jnic *jn, uint8_t *mac);

    // get a pointer to the next txbuf
    int (*jdev_txbuf_next)(struct jnic *jn, struct netbuf_hdr **ret);
    // give the next txbuf to the kernel
    int (*jdev_txbuf_done)(struct jnic *jn);
    
    // get a pointer to the next rxbuf, blocking
    int (*jdev_rxbuf_next)(struct jnic *jn, struct netbuf_hdr **ret);
    // give the next rxbuf to the kernel
    int (*jdev_rxbuf_done)(struct jnic *jn, struct netbuf_hdr *nb);
};

struct jnic {
    struct jdev *jn_dev;

    struct sobj_ref nic_ref;

    // optional monitor to use for feeding buffers
    struct sobj_ref jsm_ref;
    struct jsm jsm;
    
    union {
	struct {
	    int rx_head;	// kernel will place next packet here
	    int rx_tail;	// last buffer we gave to the kernel
	    int tx_next;	// next slot to use for TX
	    
	    // rx/tx buffers
	    struct sobj_ref buf_seg;
	    struct netbuf_hdr *rx[JNIC_REAL_BUFS];
	    struct netbuf_hdr *tx[JNIC_REAL_BUFS];
	    void *buf_base;
	} jn_real;

	struct {
	    // rx/tx buffers
	    struct sobj_ref buf_seg;
	    void *buf_base;
	    void *my_base;

	    // special vnic rx notification ring
	    struct sobj_ref ring_seg;
	    struct vnic_ring *rx_ring;
	    int rx_ring_next;
	    
	    // tx buffers
	    struct netbuf_hdr *tx[PAGES_PER_VIRT];
	    int tx_next;	// next slot to use for TX
	} jn_virt;
    };
};

extern struct jdev jdevreal;
extern struct jdev jdevvirt2;

int jnic_real_setup(struct jnic *jn, struct sobj_ref nic_ref);

int jnic_init(struct jnic *jn);
int jnic_mac(struct jnic *jn, uint8_t *mac);
int jnic_txbuf_next(struct jnic *jn, struct netbuf_hdr **ret);
int jnic_txbuf_done(struct jnic *jn);
int jnic_rxbuf_next(struct jnic *jn, struct netbuf_hdr **ret);
int jnic_rxbuf_done(struct jnic *jn, struct netbuf_hdr *nb);

int64_t jnic_alloc_all(struct jnic *jnic, int n, struct sobj_ref *nic_share);

int jnic_virt_segment(uint64_t sh_id, 
		      int n, void **buf_start, struct sobj_ref *buf_ref);
int jnic_virt_setup(struct jnic *jn, struct sobj_ref nic_ref, 
		    void *buf_start, struct sobj_ref buf_ref, int buf_i);

#endif
