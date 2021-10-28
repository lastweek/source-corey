
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
}

#include <db_select.hh>
#include <inc/error.hh>

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
	return 0;
}

static const char * scan_select_fmt_ = "SELECT count(*) FROM t%u WHERE c2 = %u;";
static const char * create_fmt_ =
	"CREATE TABLE t%u(c1 INTEGER, c2 INTEGER, c3 STRING);";
static const char * create_index_fmt_ = "CREATE INDEX i%d ON t%d(c1);";
#define MK_INDEX(q, t) do { \
	sprintf(q, create_index_fmt_, t, t); \
	} while (0);
static const char * insert_fmt_ = "INSERT INTO t%u VALUES(%u, %u, \"%s\");";

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"

inline void sqlite3_execution(sqlite3 *db, const char *sql)
{
	int rc;
	char *zErrMsg;

	fprintf(stderr, "%s\n", sql);
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("ERROR: %s\n", sql);
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		panic("Panic");
	}
	return;
}

static void 
do_select(struct db_select_state * st, int wid) {
	char query_str [256];
	sprintf(query_str, scan_select_fmt_, wid, jrand() % db_max_c2_val);
	sqlite3_execution(st->db, query_str);
	return;
}

const char * dbname_ = "select.db";
static void __attribute__((noreturn))
select_worker(struct db_select_state * state, int wid) {

	// ready to process requests
    cprintf("worker %d (pid %u): ready to process requests\n", wid, core_env->pid);
    while (1) {
		// loop on all the cores running an httpd
		for (uint32_t i = 0; i < JOS_NCPU; i++) {
		    struct db_worker_state *worker = &state->workers[wid][i];
		    if (worker->state == idle)
				continue;
		    worker->state = working;
			do_select(state, wid);
		    worker->state = idle;
		}
		nop_pause();
    }
}

httpd_db_select::httpd_db_select(proc_id_t *pids, uint32_t nworkers, 
		uint32_t num_rows, uint32_t pad_length, uint32_t max_c2_val) 
    : nworkers_(nworkers) {
	int rc;
	char query_str[256];
	char pad_buf[db_pad_length+1];
	pad_buf[db_pad_length] = '\0';

	cprintf("in httpd_db_select(nworker=%d)\n", nworkers_);
    int64_t r = segment_alloc(core_env->sh, sizeof(struct db_select_state), 0,
			      (void **) &select_state_, SEGMAP_SHARED, 
			      "db-select-state-seg", core_env->pid);
    if (r < 0)
		panic("unable to allocate segment for select_state_ %s", e2s(r));

	select_state_->num_rows = num_rows;
	select_state_->pad_length = pad_length;

    for (int i = 0; i < max_nworkers; i++) {
		for (int k = 0; k < JOS_NCPU; k++) {
		    thread_mutex_init(&select_state_->command_mu[i][k]);
		}
	}

	jrand_init();

	// open the database
	rc = sqlite3_open(dbname_, &select_state_->db);
	if (rc != SQLITE_OK) {
		printf("Can't open db: %s\n", sqlite3_errmsg(select_state_->db));
		sqlite3_close(select_state_->db);
		panic("Panic");
	}

	// create the tables
	for (uint32_t wid = 0; wid < nworkers_; wid ++) {
    sqlite3_execution(select_state_->db, BEGIN_STR);
    sprintf(query_str, create_fmt_, wid);
    sqlite3_execution(select_state_->db, query_str);
	// populate the table
	for (uint32_t j = 0; j < select_state_->num_rows; j++) {
		for (uint32_t k = 0; k < db_pad_length; k ++) {
			pad_buf[k] = (char) (0x61 + (jrand() % 26));
		}
		sprintf(query_str, insert_fmt_, wid, j, jrand() % max_c2_val, pad_buf);
		sqlite3_execution(select_state_->db, query_str);
	}
    sqlite3_execution(select_state_->db, COMMIT_STR);
	MK_INDEX(query_str, wid);
	sqlite3_execution(select_state_->db, query_str);
	}

	// fork workers
	cprintf("forking select workers\n");
	for (uint32_t i = 0; i < nworkers_; i++) {
		cprintf("forking worker %d on core %d\n", i, pids[i]);
		r = pfork(pids[i]);
		if (r < 0)
			panic("unable to pfork worker to core %u: %s", pids[i], e2s(r));
		if (r == 0)
			select_worker(select_state_, i);
	}
}

httpd_db_select::~httpd_db_select(void)
{
    //free memory. However, it should never comes here...
}

uint64_t
httpd_db_select::compute(uint32_t key)
{
    if (!nworkers_) {
		do_select(select_state_, key % nworkers_);
		return 0;
    }

	//select a worker
	uint32_t wid = key % nworkers_;
	struct db_worker_state *worker = &select_state_->workers[wid][core_env->pid];
	thread_mutex_t *mu = &select_state_->command_mu[wid][core_env->pid];

	thread_mutex_lock(mu);

	//send request and wait until finished
	worker->ndb = key % nworkers_;
	worker->state = dispatched;
	while (worker->state != idle) {
		thread_yield();
	}
	thread_mutex_unlock(mu);
	return 1;
}


