#include <machine/x86.h>
#include <os.hh>

#include <pthread.h>
#include <sys/time.h>

int 
mthread_join(mthread_t id, void ** thread_return)
{
    return pthread_join(id, thread_return);
}

int 
mthread_create(mthread_t  *  thread,  const mthread_attr_t * attr, 
	       void * (*start_routine)(void *), void *arg)
{
    return pthread_create(thread, attr, start_routine, arg);
}

uint64_t
mnsec(void)
{
    return read_tsc();
}
