
#ifndef JOS_USER
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <strings.h> // for bzero
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef LINUX_SHARED
#include <pthread.h>
#endif

#ifdef LINUX_DIFF
#include <sys/wait.h>
#endif

#ifdef JOS_USER

#include <pkg/sqlite3/sqlite3.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/kdebug.h>
#include <inc/pad.h>
#include <machine/x86.h>

#define printf(...) cprintf(__VA_ARGS__)

#define fprintf(fp, ...) do { \
	 cprintf(__VA_ARGS__); \
	} while (0);

#else

#include <sys/stat.h>
#include <assert.h>
#include <sys/shm.h>
#include <sched.h>
#include <sqlite3.h>

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

#define jrand() ((uint64_t)random())
/*
#define panic(str) do { \
		fprintf(stderr, "panic: %s", str); \
	} while (0)
*/

#define panic(str, ...) do { \
        fprintf(stderr, "panic: "); \
        printf(str, ## __VA_ARGS__); \
		assert(0); \
    } while (0)

/*static void 
panic(const char * str) {
	fprintf(stderr, "%s\n", str);
	exit(1);
}*/

#endif

# define CHECK_ERROR(a) if (a) {\
	perror("Error at line\n\t" #a "\nSystem Msg");\
	exit(1);\
}

#ifndef RD_TSC

# define timerisset(tvp)    ((tvp)->tv_sec || (tvp)->tv_usec)
# define timerclear(tvp)    ((tvp)->tv_sec = (tvp)->tv_usec = 0)
# define timercmp(a, b, CMP)                              \
  (((a)->tv_sec == (b)->tv_sec) ?                         \
   ((a)->tv_usec CMP (b)->tv_usec) :                          \
   ((a)->tv_sec CMP (b)->tv_sec))
# define timeradd(a, b, result)                           \
  do {                                        \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                 \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                  \
    if ((result)->tv_usec >= 1000000)                         \
      {                                       \
    ++(result)->tv_sec;                           \
    (result)->tv_usec -= 1000000;                         \
      }                                       \
  } while (0)
# define timersub1(a, b, result)                           \
  do {                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                 \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                  \
    if ((result)->tv_usec < 0) {                          \
      --(result)->tv_sec;                             \
      (result)->tv_usec += 1000000;                       \
    }                                         \
  } while (0)


static uint32_t 
timerbigger(struct timeval *tv0, struct timeval *tv1) {
	if ((uint64_t)tv0->tv_sec > (uint64_t)tv1->tv_sec) {
		return 1;
	} else if ((uint64_t)tv0->tv_sec < (uint64_t)tv1->tv_sec) {
		return 0;
	}
	if ((uint64_t)tv0->tv_usec > (uint64_t)tv1->tv_usec) {
		return 1;
	}
	return 0;
}

static void 
timerprint(const char * str, struct timeval * tv0) {
	printf("%s: %0.4f (seconds)\n", str,
		((double)tv0->tv_sec) + (((double)tv0->tv_usec) / 1000000.));
	return;
}

typedef struct timeval dbtimer_t;

#else
// LINUX_DIFF or LINUX_SHARED

#define timersub1(a, b, result) \
	do { \
		*(result) = *(a) - *(b); \
	} while (0)

static uint32_t
timerbigger(uint64_t * t0, uint64_t * t1) {
	if (*t0 > *t1) {
		return 1;
	} else {
		return 0;
	}
}

static void
timerprint(const char * str, uint64_t * tv0) {
	uint64_t usec = (*tv0) * 1000000 / cpufreq;

	cprintf("%s: %lu usec\n", str, usec);
	return;
}

typedef uint64_t dbtimer_t;

#endif // RD_TSC

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"

/*
enum { num_rows_ = 16 * 1024 };
enum { max_c2_val_ = 128 * 1024 };
enum { num_selects_ = 16 * 1024 };
enum { pad_length_ = 120 };
enum { num_threads_ = 15 };
enum { max_nworkers_ = 32 };
*/

enum { num_rows_ = 1024 };
enum { max_c2_val_ = 128 * 1024 };
enum { num_selects_ = 1024 };
enum { pad_length_ = 120 };
enum { num_threads_ = 4 };
enum { max_nworkers_ = 32 };

#ifdef JOS_USER

struct worker_state {
	PAD_TYPE(dbtimer_t, JOS_CLINE) start;
	PAD_TYPE(dbtimer_t, JOS_CLINE) end;
	PAD_TYPE(volatile uint32_t, JOS_CLINE) done;
};

#else

#define CLINE 64
#define PAD_TYPE(t, ln)                     \
	union __attribute__((__packed__,  __aligned__(ln))) {   \
		t val;                       \
		char __pad[ln + (sizeof(t) / ln) * ln];      \
	}

struct worker_state {
	PAD_TYPE(dbtimer_t, CLINE) start;
	PAD_TYPE(dbtimer_t, CLINE) end;
	PAD_TYPE(volatile uint32_t, CLINE) done;
};

#endif

static struct worker_state * worker_shared[max_nworkers_];

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
	return 0;
}

static inline void 
sqlite3_execution(sqlite3 *db, const char *sql) 
{
	int rc;
	char *zErrMsg;

	//fprintf(stdout, "%s\n", sql);

	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg); 
	if( rc!=SQLITE_OK ){
		printf("ERROR: %s\n", sql);
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		panic("exec not OK\n");
	} 
	return;
}

static const char * insert_fmt_ = "INSERT INTO t%u VALUES(%u, %u, \"%s\");";
static const char * create_fmt_ = 
	"CREATE TABLE t%u(c1 INTEGER, c2 INTEGER, c3 STRING);";
static const char * create_index_fmt_ = "CREATE INDEX i2a ON t%d(c1);";
static const char * scan_select_fmt_ = "SELECT count(*) FROM t%u WHERE c2 = %u;";

static void *
select_thread (void *idp) {
	uint32_t pid = *(uint32_t*)idp;
	char pad_buf[pad_length_+1];
	char query_str [256];
	char dbname[32];
	sqlite3 * db;
	uint32_t rc;
#ifndef JOS_USER
	struct stat stat_buf;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(pid, &cpuset);
	CHECK_ERROR(sched_setaffinity(0, sizeof(cpuset), &cpuset));
#endif

#ifdef JOS_USER
	jrand_init();
#endif

	// terminate the pad string
	pad_buf[pad_length_] = '\0';
	sprintf(dbname, "t%d.db", pid);

#ifndef JOS_USER
	if (!stat(dbname, &stat_buf)) {
		unlink(dbname);
	}
#endif

	// open the local database
	rc = sqlite3_open(dbname, &db);
	if (rc) {
		printf("Can't open db: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		panic("Bad open");
	}

	// INSERT phase
	printf("inserting keys into db%d\n", pid);
	sqlite3_execution(db, BEGIN_STR);
	sprintf(query_str, create_fmt_, pid);
	sqlite3_execution(db, query_str);
	for (uint32_t j = 0; j < num_rows_; j++) {
		for (uint32_t k = 0; k < pad_length_; k ++) {
			pad_buf[k] = (char) (0x61 + (jrand() % 26));
		}
		sprintf(query_str, insert_fmt_, pid, j, jrand() % max_c2_val_, pad_buf);
		sqlite3_execution(db, query_str);
	}
	sqlite3_execution(db, COMMIT_STR);
	printf("done inserting keys into db%d\n", pid);

	// create indexes on the columns
	printf("creating index on db%d\n", pid);
	sprintf(query_str, create_index_fmt_, pid);
	sqlite3_execution(db, query_str);
	printf("done creating index on db%d\n", pid);

	// SELECT phase
#ifdef RD_TSC
	worker_shared[pid]->start.val = read_tsc();
#else
	gettimeofday(&worker_shared[pid]->start.val, NULL);
#endif
	printf("begin selects on db%d\n", pid);
	for (uint32_t i = 0; i < num_selects_; i++) {
		sprintf(query_str, scan_select_fmt_, pid, jrand() % max_c2_val_);
		sqlite3_execution(db, query_str);
	}
	printf("end selects on db%d\n", pid);

#ifdef RD_TSC
	worker_shared[pid]->end.val = read_tsc();
#else
	gettimeofday(&worker_shared[pid]->end.val, NULL);
#endif

	// close the databases
	sqlite3_close(db);

	//fprintf(stderr, "setting worker_shared[%d]->done.val=1\n", pid);
	worker_shared[pid]->done.val = 1;

#ifdef LINUX_SHARED
	pthread_exit(NULL);
#else
	return (NULL);
#endif

}

int 
main(int argc, char * argv[]) {
	uint32_t i;
	int64_t r;
	dbtimer_t tv_diff;
	int pids[max_nworkers_];
	uint32_t start = 0;
	uint32_t end = 0;
#ifdef LINUX_DIFF
	key_t keys;
	int shmid;
	int waitstatus;
#elif LINUX_SHARED
	pthread_t pth_ids[max_nworkers_];
#endif

	// allocate shared regions
	for (i = 0; i < num_threads_; i ++) {
#ifdef JOS_USER
		r = segment_alloc(default_share, sizeof(struct worker_state), 0,
			(void **)&worker_shared[i], SEGMAP_SHARED,
				"select-worker-seg", default_pid);
		if (r < 0) {
			panic("segment alloc worker_shared failed: %s", e2s(r));
			bzero(worker_shared, sizeof(uint32_t) * max_nworkers_);
		}
#elif LINUX_DIFF
		keys = ftok("select-worker-seg", (char)('a' + i));
		shmid = shmget(keys, sizeof(struct worker_state), 
			IPC_CREAT | S_IRUSR | S_IWUSR);
		worker_shared[i] = shmat(shmid, 0, 0);
#else
		worker_shared[i] = 
			(struct worker_state*) malloc (sizeof (struct worker_state));
#endif
	}

	// init the shared state
	for (i = 0; i < num_threads_; i ++) {
		worker_shared[i]->done.val = 0;
		pids[i] = i;
	}

#ifdef JOS_USER
	for (i = 1; i < num_threads_; i ++) {
		r = pfork(i); 
		if (r == 0) { 
			select_thread(&pids[i]);
			return (0);
		} else if (r < 0) {
			panic("unable to pfork onto %u: %s", i, e2s(r));
		}
	}
	select_thread(&pids[0]);
#elif LINUX_DIFF
	for (i = 0; i < num_threads_; i ++) {
		r = fork();
		if (r == 0) { 
			select_thread(&pids[i]);
			return (0);
		} else if (r < 0) {
			panic("unable to fork %d\n", i);
		}
	}
#else
	for (i = 0; i < num_threads_; i ++) {
		pthread_create(&pth_ids[i], NULL, select_thread, (void*) &pids[i]);
		r = 1;
	}
#endif

	// only parent reaches below
	if (r <= 0) {
		panic("only parent should reach here.\n");
	}

#ifdef JOS_USER
	for (i = 0; i < num_threads_; i ++) {
		while (1) {
			if (worker_shared[i]->done.val) {
				break;
			} else {
				thread_yield();
			}
		}
	}
#elif LINUX_SHARED
	// pthread join
	for (i = 0; i < num_threads_; i++) {
		pthread_join(pth_ids[i], NULL);
	}
#else // LINUX_DIFF
	// join
	if (r > 0) {
		for (i = 0; i < num_threads_; i++) {
			printf("parent waiting on %dth exit\n", i);
			wait(&waitstatus);
		}
	}
#endif

	// find the earliest and latest times
	for (i = 0; i < num_threads_; i ++) {
		// pick the min 
		if (timerbigger(&worker_shared[start]->start.val, 
				&worker_shared[i]->start.val)) {
			start = i;
		}
		if (timerbigger(&worker_shared[i]->end.val, 
				&worker_shared[end]->end.val)) {
			end = i;
		}
		timersub1(&worker_shared[i]->end.val, 
			&worker_shared[i]->start.val, &tv_diff);
		timerprint("worker time: ", &tv_diff);
	}

	timersub1(&worker_shared[end]->end.val,  
		&worker_shared[start]->start.val, &tv_diff);
	timerprint("global time: ", &tv_diff);

	return(0);
}


