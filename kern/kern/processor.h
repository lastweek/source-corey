#ifndef JOS_KERN_PROCESSOR_H
#define JOS_KERN_PROCESSOR_H

#include <machine/mmu.h>
#include <kern/kobjhdr.h>
#include <kern/share.h>
#include <kern/sharemap.h>
#include <inc/context.h>
#include <inc/queue.h>
#include <inc/share.h>

struct kobject;
struct Segment;

#define NDEVICES_PER_MONITOR 32

struct Processor {
    struct kobject_hdr ps_ko;
    
    struct Trapframe ps_tf;

    // Floating point registers
    uint8_t ps_fp_enabled : 1;
    struct Fpregs ps_fpreg;
    // Physical CPU running on
    proc_id_t ps_pid;
    // Running on ps_pid
    volatile uint8_t ps_running : 1;
    // Scheduled to halt
    uint8_t ps_halt : 1;
    // Timer interval for this Processor
    uint64_t ps_interval_hz;
    // Running as a VM
    ps_mode_t ps_mode;
    // Map of Share tables
    struct Sharemap ps_sharemap;
    // Monitor device
    struct sobj_ref ps_monitor_obj[NDEVICES_PER_MONITOR];
    // Per Processor AS state
    struct Address_tree *ps_at;
    struct sobj_ref ps_atref;

    LIST_ENTRY(Processor) ps_at_link;
};

LIST_HEAD(processor_list, Processor);

int  processor_alloc(struct Processor **ps, proc_id_t pid)
    __attribute__ ((warn_unused_result));

int  processor_vector(struct Processor *ps, const struct u_context *uc)
     __attribute__ ((warn_unused_result));

int  processor_pagefault(struct Processor *ps, void *va, uint32_t reqflags)
     __attribute__ ((warn_unused_result));

void processor_run(void) __attribute__((__noreturn__));

void processor_load(struct Processor *ps);

struct Processor *processor_sched(void) __attribute__ ((warn_unused_result));

void  processor_set_interval(struct Processor *ps, uint64_t hz);

int   processor_utrap(struct Processor *ps, uint32_t trapno, int precise, 
		      uint64_t arg) 
      __attribute__((warn_unused_result));

int  processor_get_share(struct Processor *ps, kobject_id_t id, 
			 struct Share **sh)
     __attribute__((warn_unused_result));

int processor_add_share(struct Processor *ps, struct Share *sh)
     __attribute__((warn_unused_result));

int  processor_remove_share(struct Processor *ps, kobject_id_t id)
     __attribute__((warn_unused_result));

int  processor_import_obj(struct Processor *ps, kobject_id_t sh_id, 
			  struct kobject *ko) 
     __attribute__ ((warn_unused_result));
int  processor_co_obj(struct Processor *ps, struct sobj_ref oref, 
		      struct kobject **ko, uint8_t type)
     __attribute__ ((warn_unused_result));
int  processor_remove_obj(struct Processor *ps, struct sobj_ref oref)
     __attribute__ ((warn_unused_result));
int  processor_halt(struct Processor *ps);

int  processor_monitor_add_buf(struct Processor *ps, struct Segment *sg, 
			       uint64_t offset)
     __attribute__ ((warn_unused_result));

int  processor_set_device(struct Processor *ps, uint64_t i, struct sobj_ref o)
     __attribute__ ((warn_unused_result));

void processor_enable_fp(struct Processor *ps);

void processor_scope_cb(struct Processor *ps, kobject_id_t sh, 
			struct Processor *scope_ps);
void processor_remove_cb(struct Processor *ps, kobject_id_t id);
void processor_gc_cb(struct Processor *ps);

#endif
