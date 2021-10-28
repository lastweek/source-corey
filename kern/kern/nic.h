#ifndef JOS_KERN_NIC_H
#define JOS_KERN_NIC_H

#include <kern/device.h>

struct nic 
{
    struct device_handler nc_dh;    

    void *nc_arg;
    int (*nc_add_rxbuf)(void *a, uint64_t sg_id, uint64_t offset,
			struct netbuf_hdr *nb, uint16_t size);
    int (*nc_add_txbuf)(void *a, uint64_t sg_id, uint64_t offset,
			struct netbuf_hdr *nb, uint16_t size);
    int (*nc_conf)(void *a, struct u_device_conf *udc);
    void (*nc_reset)(void *a);
    void (*nc_poll)(void *a);

    // callbacks optionally invoked by NIC drivers
    void *nc_cb_arg;
    void (*nc_intr_rx_cb)(void *a, uint64_t sg_id, uint64_t offset,
			  struct netbuf_hdr *nb, uint16_t size);
    void (*nc_intr_tx_cb)(void *a, uint64_t sg_id, uint64_t offset,
			  struct netbuf_hdr *nb, uint16_t size);
    
    proc_id_t nc_pid;
    uint8_t nc_hwaddr[6];
    uint8_t nc_irq_line;
};

void nic_register(struct nic *nic, device_t type);

#endif
