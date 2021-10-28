#ifndef UCLIBC_JOS_PTHREADTYPES_H
#define UCLIBC_JOS_PTHREADTYPES_H

#include <stdint.h>
#include <inc/thread.h>

typedef thread_id_t pthread_t;
typedef struct {
    int reserved;
    int count;
    uint64_t owner;
    int kind;
    thread_mutex_t mu;
} pthread_mutex_t;

#define __LOCK_INITIALIZER { 0 }

// Types we don't care about

typedef int pthread_attr_t;
typedef int pthread_mutexattr_t;
typedef int pthread_cond_t;
typedef int pthread_condattr_t;

typedef int pthread_rwlock_t;
typedef int pthread_rwlockattr_t;
typedef int pthread_spinlock_t;
typedef int pthread_barrier_t;
typedef int pthread_barrierattr_t;

typedef int pthread_key_t;
typedef int pthread_once_t;

// Prototypes for the basic lock operations
int __pthread_mutex_lock(pthread_mutex_t *mutex) __THROW;
int __pthread_mutex_trylock(pthread_mutex_t *mutex) __THROW;
int __pthread_mutex_unlock(pthread_mutex_t *mutex) __THROW;

#endif
