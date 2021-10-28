#include <kern/lib.h>
#include <kern/device.h>
#include <kern/kobj.h>
#include <kern/lockmacro.h>
#include <machine/atomic.h>
#include <inc/error.h>
#include <inc/copy.h>

struct {
    struct {
	struct device_handler *dh;    
	jos_atomic_t alloced;
    } dev[64];
    uint64_t ndev;
} devices;

void
device_register(struct device_handler *dh)
{
    static uint64_t did;
    if (did >= array_size(devices.dev))
	panic("no more space in device array");

    devices.dev[did++].dh = dh;
    devices.ndev = did;
    dh->dh_hdr.id = did;
}

void
device_shutdown(void)
{
    for (uint32_t i = 0; i < array_size(devices.dev); i++)
	if (devices.dev[i].dh && devices.dev[i].dh->dh_shutdown)
	    devices.dev[i].dh->dh_shutdown(devices.dev[i].dh->dh_arg);
}

int
device_list(struct u_device_list *udl)
{
    uint64_t i = 0;
    for (; i < devices.ndev; i++) {
	if (i >= array_size(udl->dev))
	    return -E_NO_SPACE;
	udl->dev[i] = devices.dev[i].dh->dh_hdr;
    }

    udl->ndev = i;
    return 0;
}

int
device_alloc(struct Device **dv, uint64_t did, proc_id_t pid)
{
    struct kobject *ko;
    if (did > devices.ndev || did == 0)
	return -E_INVAL;

    if (jos_atomic_compare_exchange(&devices.dev[did - 1].alloced, 0, 1) != 0)
	return -E_BUSY;
    
    int r = kobject_alloc(kobj_device, &ko, pid);
    if (r < 0)
	return r;

    struct Device *dev = &ko->dv;
    dev->dv_dh = devices.dev[did - 1].dh;
    dev->dv_dh->dh_dev = dev;
    *dv = dev;
    return 0;
}

int
device_stat(struct Device *dv, struct u_device_stat *uds)
{
    struct device_handler *dh = dv->dv_dh;
    return dh->dh_stat(dh->dh_arg, uds);
}

int
device_buf(struct Device *dv, struct Segment *sg, uint64_t offset, 
	   devio_type type)
{
    void *va;
    int r = segment_get_page(sg,
			     offset / PGSIZE, 
			     &va, page_shared_cow);
    if (r < 0) 
	return r;
    if (PGSIZE - PGOFF(offset) < sizeof(struct diskbuf_hdr))
	return -E_INVAL;

    va += PGOFF(offset);
    return dv->dv_dh->dh_feed(dv->dv_dh->dh_arg, sg, offset, va, type);
}

int
device_conf(struct Device *dv, struct u_device_conf *udc)
{
    struct device_handler *dh = dv->dv_dh;
    return dh->dh_conf(dh->dh_arg, udc);
}

void
device_poll(struct Device *dv)
{
    struct device_handler *dh = dv->dv_dh;
    dh->dh_poll(dh->dh_arg);
}

void
device_gc_cb(struct Device *dv)
{
    // XXX
}

void
device_scope_cb(struct Device *dv, kobject_id_t parent_sh, 
		struct Processor *scope_ps)
{
    // XXX
}

void 
device_remove_cb(struct Device *dv, kobject_id_t id)
{
    // XXX
}
