
#ifndef JOS_INC_USERFS_H
#define JOS_INC_USERFS_H

#include <inc/queue.h>
#include <inc/lib.h>
#include <machine/atomic.h>
#include <inc/rwlock.h>
#include <stdint.h>
#define UF_SETLK 0
#define UF_GETLK 1

typedef uint32_t userfs_dev_t;
typedef uint32_t userfs_ino_t;

enum userfs_flock_t { UF_RDLCK_, UF_WRLCK_, UF_UNLCK_ };

enum userfs_whence_t { U_SEEK_SET };

#define MAX_CONCURRENT_READER_REQUEST 1000000
typedef struct read_write_lock {
	jos_atomic_t reader_count;
	jos_atomic_t writer_id;
} userfs_rwlock;

struct userfs_flock {
	enum userfs_flock_t l_type;    /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
	enum userfs_whence_t l_whence;  /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
	off_t l_start;   /* Starting offset for lock */
	off_t l_len;     /* Number of bytes to lock */
	proc_id_t l_pid;     /* PID of process blocking our lock (F_GETLK only) */
};

struct internal_userfs_flock {
	enum userfs_flock_t l_type;    /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
	enum userfs_whence_t l_whence;  /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
	off_t l_start;   /* Starting offset for lock */
	off_t l_len;     /* Number of bytes to lock */
	proc_id_t l_pid;     /* PID of process blocking our lock (F_GETLK only) */
	TAILQ_ENTRY(internal_userfs_flock) lock_list;
//	userfs_rwlock frw_lock;
	struct rwlock frw_lock;
};

struct userfs_stat {
	userfs_dev_t st_dev;
	userfs_ino_t st_ino;
	uint64_t st_size;
};

struct userfs_file {
	uint64_t size;
	uint64_t used;
	userfs_dev_t dev;
	userfs_ino_t ino;
	thread_mutex_t flocklist_mutex;
	TAILQ_HEAD(lock_head, internal_userfs_flock) lock_head;
	char * buf;
	char * name;
};

int userfs_init(void);
int64_t userfs_pwrite(struct userfs_file *file, const void *buf, 
	uint64_t count, uint64_t off);
int64_t userfs_ftruncate(struct userfs_file *file, uint64_t new_size);
int64_t userfs_pread(struct userfs_file *file, void *buf, 
	uint64_t count, uint64_t off);
int	userfs_create(const char *name, struct userfs_file **fpp);
int userfs_lookup(const char *name, struct userfs_file **fpp);
int	userfs_fstat(struct userfs_file *fp, struct userfs_stat *pstat);
void userfs_print_file_info(struct userfs_file *fp);
int userfs_fcntl(struct userfs_file * fp, int cmd, struct userfs_flock * lp);
/*
inline uint32_t userfs_atomic_inc(jos_atomic_t *v) {
repeat:
	uint32_t t = (uint32_t)v->counter;	
	if (jos_atomic_compare_exchange(v, t, t+1) != t) goto repeat;
	return t;
}

inline void userfs_rw_init(userfs_rwlock *lock) {
	lock->reader_count = 0;
	lock->writer_id = 0;
}

inline void userfs_rw_read_lock(userfs_rwlock *lock) {
	if (userfs_atomic_inc(&lock->reader_count) < MAX_CONCURRENT_READER_REQUEST) {
		return;
	}
	int i = 0;
	do {
		jos_atomic_dec(&lock->reader_count);
	} while (userfs_atomic_inc(&lock->reader_count) >= MAX_CONCURRENT_READER_REQUEST);	
}

inline void userfs_rw_read_unlock(userfs_rwlock *lock) {
	jos_atomic_dec(&lock->reader_count);
}
*/

#endif

