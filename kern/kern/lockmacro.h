#ifndef JOS_KERN_LOCKMACRO_H
#define JOS_KERN_LOCKMACRO_H

#include <kern/kobj.h>

#define lock_kobj(ko)                                          \
    spin_lock(&((struct kobject *)(ko))->hdr.ko_lock)

#define unlock_kobj(ko)                                        \
    spin_unlock(&((struct kobject *)(ko))->hdr.ko_lock)

#define locked_void_call(fn, ko, __args...)                    \
    ({                                                         \
	__typeof__(ko) __ko = (ko);			       \
        lock_kobj(__ko);                                       \
        fn (__ko, ##__args);                                   \
        unlock_kobj(__ko);                                     \
    })

#define locked_call(fn, ko, __args...)                         \
    ({                                                         \
	__typeof__(ko) __ko = (ko);			       \
 	__typeof__(fn (__ko, ##__args)) __lcr;                 \
        lock_kobj(__ko);                                       \
        __lcr = fn (__ko, ##__args);                           \
        unlock_kobj(__ko);                                     \
        __lcr;                                                 \
    })

#define assert_locked(kp)                                      \
    assert(spin_locked(&(kp)->ko_lock))

#endif
