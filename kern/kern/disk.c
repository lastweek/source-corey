#include <machine/proc.h>
#include <kern/disk.h>
#include <kern/lib.h>
#include <kern/lockmacro.h>
#include <inc/error.h>

struct disk_list disks;

void
disk_poll(struct disk *dk)
{
    dk->dk_poll(dk);
}

int
disk_io(struct disk *dk, disk_op op, struct kiovec *iov_buf, int iov_cnt,
	uint64_t offset, disk_callback cb, void *cbarg)
{
    return dk->dk_issue(dk, op, iov_buf, iov_cnt, offset, cb, cbarg);
}

static int
disk_stat(void *a, struct u_device_stat *uds)
{
    struct disk *dk = a;
    uds->disk.bytes = dk->dk_bytes;
    return 0;
}

static int
disk_conf(void *a, struct u_device_conf *udc)
{
    struct disk *dk = a;
    if (udc->type != device_conf_irq)
	return -E_INVAL;
    
    if (!udc->irq.enable)
	return dk->dk_conf(dk, dk->dk_pid, 0);
    
    if (udc->irq.irq_pid >= ncpu)
	return -E_INVAL;

    dk->dk_pid = udc->irq.irq_pid;
    return dk->dk_conf(dk, dk->dk_pid, 1);
}

static void
disk_reset(void *a)
{
    struct disk *dk = a;

    if (dk->dk_hdr) {
	pagetree_incref_hw(dk->dk_hdr);
	dk->dk_hdr = 0;
    }

    for (uint64_t i = 0; dk->dk_iov[i].iov_base; i++) {
	pagetree_decref_hw(dk->dk_iov[i].iov_base);
	dk->dk_iov[i].iov_base = 0;
    }
}

static void
disk_io_cb(disk_io_status status, void *arg)
{
    struct disk *dk = arg;

    if (!SAFE_EQUAL(status, disk_io_success))
	dk->dk_hdr->count |= DISKHDR_COUNT_ERR;
    dk->dk_hdr->count |= DISKHDR_COUNT_DONE;
    disk_reset(dk);
}

static int
disk_feed(void *a, struct Segment *sg, uint64_t offset, struct devbuf_hdr *db, 
	  devio_type type)
{
    struct disk *dk = a;
    struct diskbuf_hdr *hdr = &db->diskbuf_hdr;

    if (hdr->address % 512)
	return -E_INVAL;

    uint32_t reqcount = hdr->count;

    if (reqcount % 512 || reqcount > DISK_REQMAX)
	return -E_INVAL;
    
    page_sharing_mode mode = type == devio_in ? 
	page_shared_cow : page_shared_cor;

    offset += sizeof(*hdr);
    uint64_t pgoff = PGOFF(offset);
    uint32_t i = 0;
    for (uint32_t count = 0; reqcount != count; ) {
	uint32_t bytes = JMIN(reqcount - count, PGSIZE - pgoff);

	if (bytes) {
	    void *va;
	    int r = segment_get_page(sg,
				     (offset + count) / PGSIZE, 
				     &va, mode);
	    if (r < 0) {
		disk_reset(dk);
		return r;
	    }
	    pagetree_incref_hw(va);
	
	    va += pgoff;
	    dk->dk_iov[i].iov_base = va;
	    dk->dk_iov[i].iov_len = bytes;
	    i++;
	}

	count += bytes;
	pgoff = 0;
    }
    dk->dk_hdr = hdr;
    pagetree_incref_hw(hdr);

    disk_op op = type == devio_in ? op_read : op_write;
    return disk_io(dk, op, dk->dk_iov, i, hdr->address, &disk_io_cb, dk);
}

void
disk_register(struct disk *dk)
{
    LIST_INSERT_HEAD(&disks, dk, dk_link);
    cprintf("disk_register: %"PRIu64" bytes, %s: %1.40s\n",
	    dk->dk_bytes, dk->dk_busloc, dk->dk_model);
    
    dk->dk_dh.dh_hdr.type = device_disk;
    dk->dk_dh.dh_arg = dk;
    dk->dk_dh.dh_conf = disk_conf;
    dk->dk_dh.dh_stat = disk_stat;
    dk->dk_dh.dh_feed = disk_feed;
    dk->dk_dh.dh_reset = disk_reset;
    
    device_register(&dk->dk_dh);
}
