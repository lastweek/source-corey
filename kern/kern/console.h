#ifndef JOS_KERN_CONSOLE_H
#define JOS_KERN_CONSOLE_H

#include <inc/queue.h>
#include <inc/safetype.h>
#include <inc/device.h>
#include <inc/spinlock.h>
#include <kern/device.h>

typedef SAFE_TYPE(int) cons_source;
#define cons_source_user	SAFE_WRAP(cons_source, 1)
#define cons_source_kernel	SAFE_WRAP(cons_source, 2)

struct cons_device {
    struct device_handler cd_handler;
    struct Segment *cd_cons_sg;
    uint64_t cd_cons_npage;
    uint32_t cd_consi;

    struct spinlock cd_out_lock;

    void *cd_arg;
    int  (*cd_pollin) (void *);
    void (*cd_output) (void *, int, cons_source);
    LIST_ENTRY(cons_device) cd_link;
};

void cons_putc(int c, cons_source src);
void cons_flush(void);
int  cons_getc(void);
int  cons_probe(void);
void cons_intr(struct cons_device *cd);
void cons_register(struct cons_device *cd);

#endif
