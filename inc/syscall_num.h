#ifndef JOS_INC_SYSCALLNUM_H
#define JOS_INC_SYSCALLNUM_H

#define ALL_SYSCALLS \
    SYSCALL_ENTRY(cons_puts)			\
    SYSCALL_ENTRY(cons_flush)			\
    SYSCALL_ENTRY(locality_get)			\
    SYSCALL_ENTRY(device_list)			\
    SYSCALL_ENTRY(device_alloc)			\
    SYSCALL_ENTRY(device_stat)			\
    SYSCALL_ENTRY(device_conf)			\
    SYSCALL_ENTRY(device_buf)			\
    SYSCALL_ENTRY(debug)			\
    SYSCALL_ENTRY(obj_get_name)			\
    SYSCALL_ENTRY(share_alloc)			\
    SYSCALL_ENTRY(share_addref)			\
    SYSCALL_ENTRY(share_unref)			\
    SYSCALL_ENTRY(segment_copy)			\
    SYSCALL_ENTRY(segment_alloc)		\
    SYSCALL_ENTRY(segment_get_nbytes)		\
    SYSCALL_ENTRY(segment_set_nbytes)		\
    SYSCALL_ENTRY(processor_alloc)		\
    SYSCALL_ENTRY(processor_current)		\
    SYSCALL_ENTRY(processor_vector)		\
    SYSCALL_ENTRY(processor_addref)		\
    SYSCALL_ENTRY(processor_unref)		\
    SYSCALL_ENTRY(processor_set_interval)	\
    SYSCALL_ENTRY(processor_halt)		\
    SYSCALL_ENTRY(processor_set_device)		\
    SYSCALL_ENTRY(self_fp_enable)		\
    SYSCALL_ENTRY(self_drop_share)		\
    SYSCALL_ENTRY(self_get_as)			\
    SYSCALL_ENTRY(self_get_pid)			\
    SYSCALL_ENTRY(at_alloc)			\
    SYSCALL_ENTRY(at_get)			\
    SYSCALL_ENTRY(at_set)			\
    SYSCALL_ENTRY(at_set_slot)			\
    SYSCALL_ENTRY(machine_reinit)

#ifndef __ASSEMBLER__
#define SYSCALL_ENTRY(name)	SYS_##name,

enum {
    ALL_SYSCALLS
    NSYSCALLS
};

#undef SYSCALL_ENTRY
#endif

#endif
