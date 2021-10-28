#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <pthread.h>
#include <errno.h>

/* To support __UCLIBC_HAS_THREADS__.
 * Do not call any of these directly.  See inc/thread.h for
 * the real thread API.
 */

int
__pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (mutex->kind == PTHREAD_MUTEX_RECURSIVE) {
	uint64_t tid = thread_id();
	if (mutex->owner == tid) {
	    assert(mutex->count);
	    mutex->count++;
	    return 0;
	}

	if (0 == thread_mutex_trylock(&mutex->mu)) {
	    assert(mutex->count == 0);
	    assert(mutex->owner == 0);
	    mutex->count = 1;
	    mutex->owner = tid;
	    return 0;
	}
    } else {
	if (0 == thread_mutex_trylock(&mutex->mu))
	    return 0;
    }
    __set_errno(EBUSY);
    return -1;
}

int
__pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex->kind == PTHREAD_MUTEX_RECURSIVE) {
	assert(mutex->owner == thread_id());
	assert(mutex->count != 0);
	mutex->count--;

	if (mutex->count == 0) {
	    mutex->owner = 0;
	    thread_mutex_unlock(&mutex->mu);
	}

	return 0;
    } else {
	thread_mutex_unlock(&mutex->mu);
    }
    return 0;
}

int
__pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex->kind == PTHREAD_MUTEX_RECURSIVE) {
	uint64_t tid = thread_id();
	if (mutex->owner == tid) {
	    assert(mutex->count);
	    mutex->count++;
	    return 0;
	}

	thread_mutex_lock(&mutex->mu);
	assert(mutex->count == 0);
	assert(mutex->owner == 0);
	mutex->count = 1;
	mutex->owner = tid;
    } else {
	thread_mutex_lock(&mutex->mu);
    }
    return 0;
}

void
_pthread_cleanup_pop_restore(struct _pthread_cleanup_buffer *buffer,
			     int execute)
{
    if (execute)
	buffer->__routine(buffer->__arg);
}

void
_pthread_cleanup_push_defer(struct _pthread_cleanup_buffer *buffer,
			    void (*routine)(void *), void *arg)
{
    buffer->__routine = routine;
    buffer->__arg = arg;
}
