#include <inc/syscall.h>
#include <inc/device.h>
#include <machine/mmu.h>
#include <inc/lib.h>
#include <inc/thread.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <string.h>

#include "interfaces/josmpif.h"

#define JIF_BUFS 1

#define check(expr)						\
    ({								\
	int64_t __c = (expr);					\
	if (__c < 0) {						\
            cprintf("%s:%d: %s: %s\n", __FILE__, __LINE__,	\
		    #expr, e2s(__c));				\
	    return -1;						\
	}							\
	__c;							\
    })

esint8 
if_initInterface(hwInterface *jif, eint8 *fileName)
{
    int64_t r;
    memset(jif, 0, sizeof(*jif));    

    struct u_device_list udl;
    check(sys_device_list(&udl));

    uint64_t did = ~0;
    for (uint64_t i = 0; i < udl.ndev; i++) {
	if (udl.dev[i].type == device_disk) {
	    did = udl.dev[i].id;
	    break;
	}
    }

    if (did == ~0) {
	cprintf("if_initInterface: unable to find a disk\n");
	return -1;
    }

    check(r = sys_device_alloc(core_env->sh, did, core_env->pid));
    jif->disk_dev = SOBJ(core_env->sh, r);

    struct u_device_conf udc;
    udc.type = device_conf_irq;
    udc.irq.irq_pid = core_env->pid;
    udc.irq.enable = 1;
    check(sys_device_conf(jif->disk_dev, &udc));

    struct u_device_stat uds;
    check(sys_device_stat(jif->disk_dev, &uds));
    jif->sectorCount = uds.disk.bytes / 512;

    check(segment_alloc(core_env->sh, JIF_BUFS * PGSIZE, 
			&jif->buf_seg, &jif->buf_base, 0,
			"disk rw bufs", core_env->pid));

    memset(jif->buf_base, 0, PGSIZE);
    return 0;
}

esint8 
if_readBuf(hwInterface *jif, euint32 address, euint8 *buf)
{
    // Ideally efsl would provide hooks for buffer allocation
    struct diskbuf_hdr *hdr = jif->buf_base;
    hdr->address = address * 512;
    hdr->count = 512;

    check(sys_device_buf(jif->disk_dev, jif->buf_seg, 0, devio_in));
    while (!(hdr->count & DISKHDR_COUNT_DONE))
	thread_yield();

    if (hdr->count & DISKHDR_COUNT_ERR)
	check(-1);

    memcpy(buf, (void *)(hdr + 1), 512);
    return 0;
}

esint8 
if_writeBuf(hwInterface *jif, euint32 address, euint8 *buf)
{
    volatile struct diskbuf_hdr *hdr = jif->buf_base;
    hdr->address = address * 512;
    hdr->count = 512;

    memcpy((void *)(hdr + 1), buf, 512);
   
    check(sys_device_buf(jif->disk_dev, jif->buf_seg, 0, devio_out));
    while (!(hdr->count & DISKHDR_COUNT_DONE))
	thread_yield();
    
    if (hdr->count & DISKHDR_COUNT_ERR)
	check(-1);

    return 0;
}
