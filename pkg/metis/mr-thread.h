#ifndef _MR_THREAD_H_
#define _MR_THREAD_H_

void mr_init_processors(uint64_t pfork_flag);
int  mthread_create(uint64_t  *thread, uint32_t cpu_id, void * (*start_routine)(void *), void *arg) ;
int  mthread_join(uint64_t id, void ** thread_return) ;
void mr_finalize_processors(void);

#endif

