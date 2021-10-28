extern "C" {
#include <machine/mmu.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/error.h>
#include <inc/pad.h>
#include <test.h>
#include <malloc.h>
}

#include <inc/error.hh>
#include <inc/scopeguard.hh>

class user_disk {
    static const uint64_t nbufs_ = 16;

public:
    user_disk(uint64_t disk_num, uint64_t sh, proc_id_t disk_pid) {
	struct u_device_list udl;
	error_check(sys_device_list(&udl));
	
	uint64_t did = UINT64(~0);
	uint64_t disk_count = 0;
	for (uint64_t i = 0; i < udl.ndev; i++) {
	    if (udl.dev[i].type == device_disk) {
		if (disk_num == disk_count) {
		    did = udl.dev[i].id;
		    break;
		}
		disk_count++;
	    }
	}
	
	if (did == UINT64(~0))
	    throw error(-E_NOT_FOUND, "unable to find disk %lu", disk_num);

	int64_t dv;
	error_check(dv = sys_device_alloc(sh, did, disk_pid));
	disk_dev_ = SOBJ(core_env->sh, dv);

	struct u_device_conf udc;
	udc.type = device_conf_irq;
	udc.irq.irq_pid = disk_pid;
	udc.irq.enable = 1;
	error_check(sys_device_conf(disk_dev_, &udc));
	
	struct u_device_stat uds;
	error_check(sys_device_stat(disk_dev_, &uds));
	bytes_ = uds.disk.bytes;

	error_check(segment_alloc(sh, nbufs_ * PGSIZE, 
				  &buf_seg_, (void **)&buf_base_, 0,
				  "disk rw buf", disk_pid));
	memset(buf_base_, 0, nbufs_ * PGSIZE);
    };

    uint64_t write(uint64_t offset, uint64_t nbytes, void *buf) {
	if (offset % 512)
	    throw error(-E_INVAL, "offset not 512-byte aligned");
	if (nbytes % 512)
	    throw error(-E_INVAL, "nbytes not 512-byte aligned");

	uint64_t max = ROUNDDOWN((nbufs_ * PGSIZE) - sizeof(diskbuf_hdr), 512);

	uint64_t cc = 0;
	while (nbytes) {
	    uint64_t bytes = JMIN(max, nbytes);
	    void *va = (buf_base_ + 1);
	    memcpy(va, &((char *)buf)[cc], bytes);

	    buf_base_->address = offset + cc;
	    buf_base_->count = bytes;

	    nbytes -= bytes;
	    cc += bytes;

	    error_check(sys_device_buf(disk_dev_, buf_seg_, 0, devio_out));
	    while (!(buf_base_->count & DISKHDR_COUNT_DONE));
	    if (buf_base_->count & DISKHDR_COUNT_ERR)
		throw error(-E_IO, "disk error");
	}
	return cc;
    }

    uint64_t read(uint64_t offset, uint64_t nbytes, void *buf) {
	if (offset % 512)
	    throw error(-E_INVAL, "offset not 512-byte aligned");
	if (nbytes % 512)
	    throw error(-E_INVAL, "nbytes not 512-byte aligned");

	uint64_t max = ROUNDDOWN((nbufs_ * PGSIZE) - sizeof(diskbuf_hdr), 512);

	uint64_t cc = 0;
	while (nbytes) {
	    uint64_t bytes = JMIN(max, nbytes);
	    buf_base_->address = offset + cc;
	    buf_base_->count = bytes;

	    error_check(sys_device_buf(disk_dev_, buf_seg_, 0, devio_in));
	    while (!(buf_base_->count & DISKHDR_COUNT_DONE));
	    if (buf_base_->count & DISKHDR_COUNT_ERR)
		throw error(-E_IO, "disk error");

	    void *va = (buf_base_ + 1);
	    memcpy(&((char *)buf)[cc], va, bytes);

	    nbytes -= bytes;
	    cc += bytes;
	}

	return cc;
    }

    ~user_disk(void) {
	as_unmap(buf_base_);
	sys_share_unref(disk_dev_);
	sys_share_unref(buf_seg_);
    }

private:
    struct sobj_ref disk_dev_;
    struct sobj_ref ring_seg_;

    struct sobj_ref buf_seg_;
    struct diskbuf_hdr *buf_base_;

    uint64_t bytes_;
};

void
disk_write_read(user_disk *disk, char c, uint64_t offset, uint64_t nbytes)
{
    enum { buf_size = 32 * PGSIZE };

    static void *buf, *buf2;
    if (!buf) {
	buf = malloc(buf_size);
	buf2 = malloc(buf_size);
    }
    assert(buf && buf2 && nbytes <= buf_size);

    memset(buf, c, nbytes);
    memset(buf2, c, nbytes);
    disk->write(offset, nbytes, buf);
    disk->read(offset, nbytes, buf2);
    assert(!memcmp(buf, buf2, 512));    
}

void
disk_test(void)
{
    user_disk disk(0, core_env->sh, core_env->pid);

    /*
    char hello[512] = "hello";
    if (false) {
      disk.write(0, sizeof hello, hello);
      cprintf("written!\n");
    } else {
      char output[sizeof hello];
      disk.read(0, sizeof hello, output);
      cprintf("%s\n", output);
    }
    return;
    */

    cprintf("Test 512-byte writes to sector 0\n");
    for (int i = 0; i < 500; i++) {
	if (!(i % 100))
	    cprintf(" %d\n", i);
	disk_write_read(&disk, jrand(), 0, 512);
    }

    cprintf("Clearing first PGSIZE-bytes of disk\n");
    disk_write_read(&disk, 0, 0, PGSIZE);

    cprintf("Test PGSIZE-byte writes to offset 0\n");
    for (int i = 0; i < 500; i++) {
	if (!(i % 100))
	    cprintf(" %d\n", i);
	disk_write_read(&disk, jrand(), 0, PGSIZE);
    }

    cprintf("Test PGSIZE-byte write to offset 1313\n");
    try {
	disk_write_read(&disk, jrand(), 1313, PGSIZE);
	panic("disk_write_read should have failed");
    } catch (error &e) {
	if (e.err() != -E_INVAL)
	    cprintf(" wierd error: %s\n", e.what());
    }

    cprintf("Test 8PGSIZE-byte write to offset PGSIZE\n");
    for (int i = 0; i < 500; i++) {
	if (!(i % 100))
	    cprintf(" %d\n", i);
	disk_write_read(&disk, jrand(), 4096, 8*PGSIZE);
    }

    cprintf("Test 16PGSIZE-byte write to offset 8*PGSIZE\n");
    for (int i = 0; i < 500; i++) {
	if (!(i % 100))
	    cprintf(" %d\n", i);
	disk_write_read(&disk, jrand(), 16*PGSIZE, 16*PGSIZE);
    }
}
