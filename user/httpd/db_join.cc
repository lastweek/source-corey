
extern "C" {
#include <machine/x86.h>
#include <stdio.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/kdebug.h>
#include <string.h>
#include <stdlib.h>
#include <pkg/sqlite3/josmp/userfs.h>
}

#include <db_join.hh>
#include <inc/error.hh>

#include "app_shared.hh"

#ifdef PRIVATE_HEAP
extern unsigned int using_private_heap;
#endif

struct worker_sync_state {
	uint32_t s[max_nworkers];
};

static struct worker_sync_state * sync_state;

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
	return 0;
}

static const char * join_fmt_ = 
	"SELECT p%u.c2 FROM p%u,g%u WHERE p%u.c2 = g%u.c2;";
#define MK_JOIN_FMT(buf, p, g) do { \
		sprintf(buf, join_fmt_, p, p, g, p, g); \
	} while (0);
static const char * create_fmt_ =
	"CREATE TABLE %c%u(c1 INTEGER, c2 INTEGER, c3 STRING);";
static const char * create_index_fmt_ = "CREATE INDEX i%c%dc1 ON %c%d(c1);";
#define MK_INDEX_FMT(buf, c, t) do { \
		sprintf(buf, create_index_fmt_, c, t, c, t); \
	} while (0);
static const char * insert_fmt_ = "INSERT INTO %c%u VALUES(%u, %u, \"%s\");";

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"

static const char * dbname = "join.db";

inline void sqlite3_execution(sqlite3 *db, const char *sql)
{
	int rc;
	char *zErrMsg;

	//cprintf("(%d)%s\n", processor_current_procid(), sql);
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("ERROR: %s\n", sql);
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		panic("Panic");
	}
	return;
}

void
do_join(sqlite3 * db1, int wid, int nworkers) {
	char query_str [256];
	uint32_t gid;

	gid = nworkers ? (jrand() % nworkers) : 0;
	MK_JOIN_FMT(query_str, wid, gid);
//	cprintf("%s\n", query_str);
	sqlite3_execution(db1, query_str);
	return;
}

void
make_private_table(int wid, sqlite3 * db1) {
	char query_str [256];
	char pad_buf[db_pad_length+1];
	uint32_t num_private_table_per_core = 1;

	pad_buf[db_pad_length] = '\0';

	for (uint32_t i = 0; i < num_private_table_per_core; i++) {
	// create the per-thread private table
    sqlite3_execution(db1, BEGIN_STR);
    sprintf(query_str, create_fmt_, 'p', wid * num_private_table_per_core + i);
	cprintf("%s\n", query_str);
    sqlite3_execution(db1, query_str);
	// populate the database
	for (uint32_t j = 0; j < dbjoin_private_num_rows; j++) {
		for (uint32_t k = 0; k < db_pad_length; k ++) {
			pad_buf[k] = (char) (0x61 + (jrand() % 26));
		}
		// insert_fmt is "INSERT INTO %c%u VALUES(%u, %u, \"%s\");";
		sprintf(query_str, insert_fmt_, 'p', wid * num_private_table_per_core + i, j, 
			jrand() % db_max_c2_val, pad_buf);
		sqlite3_execution(db1, query_str);
	}
    sqlite3_execution(db1, COMMIT_STR);

	// create an index on the database
	MK_INDEX_FMT(query_str, 'p', wid * num_private_table_per_core + i);
	sqlite3_execution(db1, query_str);
	}
	return;
}

static void __attribute__((noreturn))
join_worker(struct db_join_state * state, uint32_t wid, 
		uint32_t coreid, uint32_t nworkers, sqlite3 * db)
{
	uint32_t rc;
	sqlite3 * db1;
	
	// create the database
	rc = sqlite3_open(dbname, &db1);
	if (rc != SQLITE_OK) {
		printf("Can't open db: %s\n", sqlite3_errmsg(db1));
		sqlite3_close(db1);
		panic("Panic");
	}

	jrand_init();

	// wait until DB wid-1 is created
	if (wid > 0) {
		while (sync_state->s[wid-1] == 0) {
			thread_yield();
		}
	}

	make_private_table(wid, db1);

	sync_state->s[wid] = 1;
	// now wait for all 
	for (uint32_t i = 0; i < nworkers; i ++) {
		while (sync_state->s[i] == 0) {
			thread_yield();
		}
	}

	// ready to process requests
    cprintf("join worker %d (core %d): ready to process requests\n", wid, coreid);
    while (1) {
		// loop on all the cores running an httpd
		for (uint32_t i = 0; i < JOS_NCPU; i++) {
		    struct db_worker_state *worker = &state->workers[wid][i];
		    if (worker->state == idle)
				continue;
		    worker->state = working;
			for (uint32_t j = 0; j < db_num_ops; j ++) {
				do_join(db1, wid, nworkers);
			}
		    worker->state = idle;
		}
		nop_pause();
    }
}

// one big, global database
httpd_db_join::httpd_db_join(proc_id_t *pids, uint32_t nworkers)
	: nworkers_(nworkers) {

	uint32_t rc;
	char pad_buf[db_pad_length + 1];
	char query_str [256];
	sqlite3 * db;

	userfs_init();

	cprintf("httpd_db_join, nworkers = %u\n", nworkers_);

	pad_buf [db_pad_length] = '\0';
    int64_t r = segment_alloc(core_env->sh, sizeof(*app_state_), 0,
		      (void **) &app_state_, SEGMAP_SHARED, 
			      "sqlite-state-seg", processor_current_procid());
    if (r < 0)
		panic("unable to allocate segment for sqlite-state-seg %s", e2s(r));
    for (uint32_t i = 0; i < nworkers; i++) {
		for (uint32_t k = 0; k < JOS_NCPU; k++) {
		    thread_mutex_init(&app_state_->command_mu[i][k]);
		}
	}

	r = segment_alloc(core_env->sh, sizeof(struct worker_sync_state), 0,
				(void**) &sync_state, SEGMAP_SHARED,
					"join-sync-seg", processor_current_procid());
    if (r < 0)
		panic("unable to allocate segment for join-sync-seg %s", e2s(r));
	for (uint32_t i = 0; i < nworkers; i ++) {
		sync_state->s[i] = 0;
	}

	cprintf("create the database\n");
	// create the database
	rc = sqlite3_open(dbname, &db);
	if (rc != SQLITE_OK) {
		printf("Can't open db: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		panic("Panic");
	}

	cprintf("create the global tables\n");
	// create the global tables
	sqlite3_execution(db, BEGIN_STR);
	for (uint32_t i = 0; i < nworkers; i ++) {
		cprintf("table g%u\n", i);
		sprintf(query_str, create_fmt_, 'g', i);
		sqlite3_execution(db, query_str);
		for (uint32_t j = 0; j < dbjoin_global_num_rows; j++) {
			for (uint32_t k = 0; k < db_pad_length; k ++) {
				pad_buf[k] = (char) (0x61 + (jrand() % 26));
			}
			// INSERT INTO %c%u VALUES(%u, %u, \"%s\");
			sprintf(query_str, insert_fmt_, 'g', i, j, 
				jrand() % db_max_c2_val, pad_buf);
			sqlite3_execution(db, query_str);
		}
	}
	sqlite3_execution(db, COMMIT_STR);

	// create indexes on the global tables
	for (uint32_t i = 0; i < nworkers; i++) {
		cprintf("creating index on table %u\n", i);
		MK_INDEX_FMT(query_str, 'g', i);
		sqlite3_execution(db, query_str);
	}

	uint64_t ummap_shref_n;
	ummap_get_shref(&ummap_shref, &ummap_shref_n);

	// fork worker cores
	cprintf("forking join workers\n");
#ifdef PRIVATE_HEAP	
	using_private_heap = 1;
#endif
	for (uint32_t i = 0; i < nworkers; i++) {
		cprintf("parent forking worker %u on core %u\n", i, pids[i]);
		r = pforkv(pids[i], PFORK_SHARE_HEAP, ummap_shref, ummap_shref_n);
		if (r < 0)
			panic("unable to pfork worker to core %u: %s", pids[i], e2s(r));
		if (r == 0) {
			cprintf("child %u on core %u calling join_worker()\n", i, pids[i]);
			join_worker(app_state_, i, pids[i], nworkers, db);
		}
	}
}

httpd_db_join::~httpd_db_join(void)
{
    //free memory. However, it should never comes here...
}

uint64_t
httpd_db_join::compute(uint32_t key)
{
	sqlite3 * db1;
	static int made_priv = 0;

    if (!nworkers_) {
		uint32_t rc = sqlite3_open(dbname, &db1);
		if (rc != SQLITE_OK) {
			printf("Can't open db: %s\n", sqlite3_errmsg(db1));
			sqlite3_close(db1);
			panic("Panic");
		}
		if (!made_priv) {
		jrand_init();
		make_private_table(0, db1);
		made_priv = 1;
		}
		for (uint32_t j = 0; j < db_num_ops; j ++) {
			do_join(db1, 0, 0);
		}
		return 1;
    }

	//join a worker
	uint32_t wid = key % nworkers_;
	struct db_worker_state *worker = &app_state_->workers[wid][core_env->pid];
	thread_mutex_t *mu = &app_state_->command_mu[wid][core_env->pid];

	thread_mutex_lock(mu);

	//send request and wait until finished
	// worker->ndb = key % ndbs_;
	worker->state = dispatched;
	while (worker->state != idle) {
		thread_yield();
	}
	thread_mutex_unlock(mu);
	return 1;
}

