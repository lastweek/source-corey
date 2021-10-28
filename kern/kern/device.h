#ifndef JOS_KERN_DEVICE_H
#define JOS_KERN_DEVICE_H

#include <kern/kobjhdr.h>
#include <inc/device.h>
#include <inc/share.h>
#include <inc/proc.h>

struct kobject;
struct Processor;
struct Segment;

struct device_handler {
    struct u_device_header dh_hdr;

    struct Device *dh_dev;
    void *dh_arg;
    int (*dh_conf) (void *a, struct u_device_conf *udc);
    int (*dh_stat) (void *a, struct u_device_stat *uds);
    int (*dh_feed) (void *a, struct Segment *sg, uint64_t offset, struct devbuf_hdr *db, 
		    devio_type type);
    void (*dh_reset) (void *a);
    void (*dh_poll) (void *a);
    void (*dh_shutdown) (void *a);
};

// Kernel object

struct Device {
    struct kobject_hdr dv_ko;
    struct device_handler *dv_dh;
};

int  device_list(struct u_device_list *udl);
void device_register(struct device_handler *di);
void device_shutdown(void);

int  device_alloc(struct Device **dv, uint64_t did, proc_id_t pid)
     __attribute__((warn_unused_result));

int  device_stat(struct Device *dv, struct u_device_stat *uds) 
     __attribute__((warn_unused_result));

int  device_buf(struct Device *dv, struct Segment *sg, uint64_t offset, 
		devio_type type) __attribute__((warn_unused_result));

int  device_conf(struct Device *dv, struct u_device_conf *udc)
     __attribute__((warn_unused_result));

void device_poll(struct Device *dv);

void device_scope_cb(struct Device *dv, kobject_id_t parent_sh, 
		     struct Processor *scope_ps);
void device_remove_cb(struct Device *dv, kobject_id_t id);
void device_gc_cb(struct Device *dv);

#endif
