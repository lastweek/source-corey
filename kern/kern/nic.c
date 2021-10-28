#include <machine/proc.h>
#include <kern/arch.h>
#include <kern/nic.h>
#include <kern/lib.h>
#include <kern/segment.h>
#include <inc/error.h>

static int
nic_stat(void *a, struct u_device_stat *uds)
{
    struct nic *nc = a;
    memcpy(uds->nic.hwaddr, nc->nc_hwaddr, 6);
    return 0;
}

static int
nic_conf(void *a, struct u_device_conf *udc)
{
    struct nic *nc = a;
    
    if (udc->type == device_conf_irq) {
	if (!udc->irq.enable) {
	    irq_arch_disable(nc->nc_irq_line, nc->nc_pid);
	} else {
	    if (udc->irq.irq_pid >= ncpu)
		return -E_INVAL;
	    
	    nc->nc_pid = udc->irq.irq_pid;
	    irq_arch_enable(nc->nc_irq_line, nc->nc_pid);
	}

	return 0;
    } else {
	if (nc->nc_conf)
	    return nc->nc_conf(nc->nc_arg, udc);
    }

    return -E_INVAL;
}

static int
nic_feed(void *a, struct Segment *sg, uint64_t offset, struct devbuf_hdr *db, 
	 devio_type type)
{
    struct nic *nc = a;
    struct netbuf_hdr *nb = &db->netbuf_hdr;
    
    if (type == devio_in)
	return nc->nc_add_rxbuf(nc->nc_arg, sg->sg_ko.ko_id, offset,
				&db->netbuf_hdr, nb->size);
    else if (type == devio_out)
	return nc->nc_add_txbuf(nc->nc_arg, sg->sg_ko.ko_id, offset,
				&db->netbuf_hdr, nb->size);
    else
	cprintf("nic_feed: unexpected devio_type %u\n", type);
    return -E_INVAL;
}

static void
nic_poll(void *a)
{
    struct nic *nc = a;
    nc->nc_poll(nc->nc_arg);
}

static void
nic_reset(void *a)
{
    struct nic *nc = a;
    nc->nc_reset(nc->nc_arg);
}

void
nic_register(struct nic *nic, device_t type)
{
    assert(type == device_nic || type == device_vnic);
    nic->nc_dh.dh_hdr.type = type;
    nic->nc_dh.dh_arg = nic;
    nic->nc_dh.dh_conf = nic_conf;
    nic->nc_dh.dh_stat = nic_stat;
    nic->nc_dh.dh_feed = nic_feed;
    nic->nc_dh.dh_reset = nic_reset;
    nic->nc_dh.dh_poll = nic_poll;

    device_register(&nic->nc_dh);
}
