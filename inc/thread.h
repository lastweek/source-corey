#ifndef JOS_INC_THREAD_H
#define JOS_INC_THREAD_H

#include <machine/atomic.h>
#include <inc/kobj.h>
#include <inc/proc.h>
#include <inc/share.h>

struct sobj_ref;

typedef	    uint64_t thread_id_t;
int	    thread_init(void (*entry)(uint64_t), uint64_t arg);
int	    thread_create(thread_id_t *tid, const char *name, 
			  void (*entry)(uint64_t), uint64_t arg);
int	    thread_pfork(const char *name, void (*entry)(uint64_t), 
			 uint64_t flag, struct sobj_ref *shares, int nr_shares, uint64_t arg, proc_id_t pid);
void	    thread_yield(void);
thread_id_t thread_id(void);
void	    thread_halt(void) __attribute__((noreturn));
int	    thread_onhalt(void (*onhalt)(void));

typedef   jos_atomic64_t thread_mutex_t;
void	  thread_mutex_init(thread_mutex_t *mu);
void	  thread_mutex_lock(thread_mutex_t *mu);
int	  thread_mutex_trylock(thread_mutex_t *mu);
void	  thread_mutex_unlock(thread_mutex_t *mu);

void	  thread_wait(volatile uint64_t *addr, uint64_t val, uint64_t nsec);
void	  thread_wakeup(volatile uint64_t *addr);

#endif
