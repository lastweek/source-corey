#ifndef JOS_INC_JSYSMON_H
#define JOS_INC_JSYSMON_H

#include <inc/device.h>
#include <inc/types.h>

struct jsm;

struct jsm_arg {
    uint64_t sc_num;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t a7;
};

struct jsm_slot{
    struct jsm_arg arg;
    int64_t ret;
#define SYSMON_USER_INUSE 0x10
#define SYSMON_KERN_CALL  0x20
#define SYSMON_KERN_RET	  0x40
    volatile int flag;
};

#define SM_NUM_SLOTS 51

struct jsm_shared_slots{
     struct jsm_slot sm_slots[SM_NUM_SLOTS];
};

struct jsm {
    struct sobj_ref sm_ref;
    struct jsm_shared_slots *shared_slots;
    int next_slot;
};

int  jsm_setup(struct jsm *sm, struct sobj_ref sm_ref);
int  jsm_set_nic(struct jsm *jsm, struct sobj_ref nic_ref, 
		 struct sobj_ref seg_ref);

int  jsm_next_slot(struct jsm *sm, struct jsm_slot **slot);
void jsm_free_slot(struct jsm_slot *slot);

// System calls
void jsm_call_device_buf(struct jsm_slot *slot, struct sobj_ref devref, 
			 struct sobj_ref sgref, uint64_t offset, 
			 devio_type type);
void jsm_call_processor_current(struct jsm_slot *slot);

#endif
