#ifndef JOS_KERN_SHARE_H
#define JOS_KERN_SHARE_H

#include <kern/kobjhdr.h>
#include <kern/map.h>
#include <inc/queue.h>
#include <inc/pad.h>
#include <inc/rwlock.h>
#include <inc/mcs.h>

struct kobject;
struct Processor;

#if 0
typedef struct rwlock sharemu_t;
#define sharemu_init rw_init
#define sharemu_read_lock rw_read_lock
#define sharemu_read_unlock rw_read_unlock
#define sharemu_write_lock rw_write_lock
#define sharemu_write_unlock rw_write_unlock
#elif 1
typedef struct mcsrwlock sharemu_t;
#define sharemu_init mcsrw_init
#define sharemu_read_lock mcsrw_read_lock
#define sharemu_read_unlock mcsrw_read_unlock
#define sharemu_write_lock mcsrw_write_lock
#define sharemu_write_unlock mcsrw_write_unlock
#endif

struct share_info {
    struct kobject *si_ko;
    jos_atomic64_t si_co;
    jos_atomic_t si_cnt;
};

struct share_action {
    struct Share *sa_sh;
    struct share_info *sa_si;
    void (*sa_action)(struct Share *, struct share_info *);
    LIST_ENTRY(share_action) sa_link;
};

struct share_blob {
    union {
	struct share_info si;
	struct share_action sa;
	LIST_ENTRY(share_blob) link;
    };
};

#define N_SHAREBLOB_PER_PAGE	(PGSIZE / sizeof(struct share_blob))
LIST_HEAD(share_blob_list, share_blob);

struct Share {
    struct kobject_hdr sh_ko;
    struct Map sh_kobject_map;

    union {
	struct {
	    struct share_blob_list free;
	    uint64_t npages;
	    struct pagetree pt;
	};
	uint8_t pad[JOS_CLINE];
    } sh_blob [JOS_NCPU];

    sharemu_t sh_kobject_lock;

    // Restrict kobject types we can hold
    int sh_mask;
};

int  share_alloc(struct Share **sgp, int kobj_mask, proc_id_t pid)
    __attribute__ ((warn_unused_result));

int  share_import_obj(struct Share *sh, struct kobject *ko)
     __attribute__ ((warn_unused_result));

int  share_co_obj(struct Share *sh, kobject_id_t id, struct kobject **ko,
		  uint8_t type) __attribute__ ((warn_unused_result));

int  share_remove_obj(struct Share *sh, kobject_id_t id)
     __attribute__ ((warn_unused_result));

void share_scope_cb(struct Share *sh, kobject_id_t id, 
		    struct Processor *scope_ps);
void share_remove_cb(struct Share *sh, kobject_id_t id);
void share_gc_cb(struct Share *sh);

void share_scope_change(struct Share *sh, struct Processor *scope_ps);

void share_action_scan(void);

void share_print(struct Share *sh);

#endif
