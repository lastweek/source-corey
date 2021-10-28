#ifndef JOS_INC_DEVICE_H
#define JOS_INC_DEVICE_H

#include <inc/proc.h>
#include <inc/types.h>
#include <inc/share.h>
#include <machine/atomic.h>

typedef enum device_type_enum {
    device_none,
    device_disk,
    device_nic,
    device_vnic,
    device_cons,
    device_sysmon,
} device_t;

struct u_device_header {
    device_t type;
    uint64_t id;
};

struct u_device_list {
    struct u_device_header dev[64];
    uint64_t ndev;
};

struct u_device_stat {
    union {
	struct {
	    uint64_t bytes;
	} disk;
	struct {
	    uint8_t hwaddr[6];
	} nic;
    };
};

typedef enum device_conf_enum {
    device_conf_irq,
    device_conf_vnic_ring,
    device_conf_sysmon,
    device_conf_sysmon_nic,
} device_conf_t;

struct u_device_conf {
    device_conf_t type;
    
    union {
	struct {
	    char enable;
	    proc_id_t irq_pid;
	} irq;
	
	struct {
	    struct sobj_ref seg;
	    uint64_t offset;
	} vnic_ring;

	struct {
	    struct sobj_ref seg;
	    uint64_t offset;
	    uint32_t num_slots;
	} sysmon;

	struct {
	    struct sobj_ref nic;
	    struct sobj_ref seg;
	} sysmon_nic;
    };
};

typedef enum {
    devio_in,
    devio_out,
    devio_reset,
} devio_type;

struct diskbuf_hdr {
#define DISKHDR_COUNT_DONE	0x800000
#define DISKHDR_COUNT_ERR	0x400000
#define DISKHDR_COUNT_MASK	0x0fffff
    volatile uint64_t address;
    volatile uint32_t count;
};

struct netbuf_hdr {
#define NETHDR_COUNT_DONE	0x8000
#define NETHDR_COUNT_RESET	0x4000
#define NETHDR_COUNT_ERR	0x2000
#define NETHDR_COUNT_MASK	0x0fff
    volatile uint16_t size;
    volatile uint16_t actual_count;
    jos_atomic_t ref;
};

struct vnic_ring {
    struct {
#define VNICRING_OFFSET_DONE	UINT64(0x1000000000000000)
#define VNICRING_OFFSET_MASK	UINT64(0x0FFFFFFFFFFFFFFF)
	uint64_t sg_id;
	uint64_t offset;
    } slot[256];
};

struct cons_entry {
#define KEY_STATUS_PRESSED	0x01
    volatile uint8_t code;
    volatile uint8_t status;
};

struct devbuf_hdr {
    union {
	struct diskbuf_hdr diskbuf_hdr;
	struct netbuf_hdr  netbuf_hdr;
    };
};

#endif
