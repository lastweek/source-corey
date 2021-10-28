
// Corey db_join code

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

#include <inc/error.h>

#include <pkg/sqlite3/sqlite3.h>

#define DB_PAD_LENGTH 120

unsigned int using_private_heap = 1;
struct sobj_ref * ummap_shref;

enum { max_nworkers_ = 16 };
enum { nworkers_ = 2 };
static uint32_t private_rows_ = 1024;
static uint32_t global_rows_ = 1024;
static uint32_t max_c2_val_ = 4;
static uint32_t db_num_ops_ = 1000;

struct worker_sync_state {
    uint32_t s[max_nworkers_];
};

static struct worker_sync_state * sync_state;

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
	return 0;
}

#define TEMP_TABLES

#ifdef TEMP_TABLES

static const char * join_fmt_ =
    "CREATE TEMPORARY TABLE t%u_backup(x1,x2,x3);"
    "INSERT INTO t%u_backup SELECT p%u.c1,p%u.c2,p%u.c3 FROM p%u,g%u WHERE p%u.c2 = g%u.c2;"
    "DROP TABLE t%u_backup;";
#define MK_JOIN_FMT(buf, p, g) do { \
        sprintf(buf, join_fmt_, p, p, p, p, p, p, g, p, g, p); \
    } while (0);

#else

static const char * join_fmt_ = 
	"SELECT p%u.c2 FROM p%u,g%u WHERE p%u.c2 = g%u.c2;";
#define MK_JOIN_FMT(buf, p, g) do { \
		sprintf(buf, join_fmt_, p, p, g, p, g); \
	} while (0);

#endif
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

void sqlite3_execution(sqlite3 *db, const char *sql);
void sqlite3_execution(sqlite3 *db, const char *sql)
{
	int rc;
	char *zErrMsg;

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
make_private_table(int wid, sqlite3 * db1);
void
make_private_table(int wid, sqlite3 * db1) {
	char query_str [256];
	char pad_buf[DB_PAD_LENGTH+1];
	uint32_t num_private_table_per_core = 1;

	pad_buf[DB_PAD_LENGTH] = '\0';

	for (uint32_t i = 0; i < num_private_table_per_core; i++) {
	// create the per-thread private table
    sqlite3_execution(db1, BEGIN_STR);
    sprintf(query_str, create_fmt_, 'p', wid * num_private_table_per_core + i);
	cprintf("%s\n", query_str);
    sqlite3_execution(db1, query_str);
	// populate the database
	for (uint32_t j = 0; j < private_rows_; j++) {
		for (uint32_t k = 0; k < DB_PAD_LENGTH; k ++) {
			pad_buf[k] = (char) (0x61 + (jrand() % 26));
		}
		// insert_fmt is "INSERT INTO %c%u VALUES(%u, %u, \"%s\");";
		sprintf(query_str, insert_fmt_, 'p', wid * num_private_table_per_core + i, j, 
			jrand() % max_c2_val_, pad_buf);
		sqlite3_execution(db1, query_str);
	}
    sqlite3_execution(db1, COMMIT_STR);

	// create an index on the database
	MK_INDEX_FMT(query_str, 'p', wid * num_private_table_per_core + i);
	sqlite3_execution(db1, query_str);
	}
	return;
}

void join_worker(uint32_t procid);
void join_worker(uint32_t procid) {
	uint32_t rc;
	sqlite3 * db;
	uint32_t gid;
	char query_str [256];

	fprintf(stderr, "join_worker(%d)\n", procid);
	
	// create the database
	rc = sqlite3_open(dbname, &db);
	if (rc != SQLITE_OK) {
		printf("Can't open db: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		panic("Panic");
	}

	jrand_init();

	// wait until DB wid-1 is created
	if (procid > 0) {
		while (sync_state->s[procid-1] == 0) {
			thread_yield();
		}
	}

	make_private_table(procid, db);

	sync_state->s[procid] = 1;
	for (uint32_t i = 0; i < nworkers_; i ++) {
		while (sync_state->s[i] == 0) {
			thread_yield();
		}
	}

	// ready to process requests
	for (uint32_t j = 0; j < db_num_ops_; j ++) {
		gid = nworkers_ ? (jrand() % nworkers_) : 0;
		MK_JOIN_FMT(query_str, procid, gid);
		sqlite3_execution(db, query_str);
	}
    cprintf("join worker %d done", procid);
}

// one big, global database
int main() {
	proc_id_t pids[max_nworkers_];
	uint32_t rc;
	int r;
	char pad_buf[DB_PAD_LENGTH + 1];
	char query_str [256];
	sqlite3 * db;

	userfs_init();

	cprintf("httpd_db_join, nworkers = %u\n", nworkers_);

	pad_buf [DB_PAD_LENGTH] = '\0';

	for (uint32_t i = 0; i < max_nworkers_; i++) {
		pids[i] = i;
	}

    r = segment_alloc(default_share, sizeof(struct worker_sync_state), 0,
		(void**) &sync_state, SEGMAP_SHARED,
			"join-sync-seg", processor_current_procid());
	if (r < 0)
		panic("unable to allocate segment for join-sync-seg %s", e2s(r));
	for (uint32_t i = 0; i < nworkers_; i ++) {
		sync_state->s[i] = 0;
	}

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
	for (uint32_t i = 0; i < nworkers_; i ++) {
		cprintf("table g%u\n", i);
		sprintf(query_str, create_fmt_, 'g', i);
		sqlite3_execution(db, query_str);
		for (uint32_t j = 0; j < global_rows_; j++) {
			for (uint32_t k = 0; k < DB_PAD_LENGTH; k ++) {
				pad_buf[k] = (char) (0x61 + (jrand() % 26));
			}
			// INSERT INTO %c%u VALUES(%u, %u, \"%s\");
			sprintf(query_str, insert_fmt_, 'g', i, j, 
				jrand() % max_c2_val_, pad_buf);
			sqlite3_execution(db, query_str);
		}
	}
	sqlite3_execution(db, COMMIT_STR);

	// create indexes on the global tables
	for (uint32_t i = 0; i < nworkers_; i++) {
		cprintf("creating index on table %u\n", i);
		MK_INDEX_FMT(query_str, 'g', i);
		sqlite3_execution(db, query_str);
	}

	ummap_shref = ummap_get_shref();

	using_private_heap = 1;

	for (uint32_t i = 1; i < nworkers_; i++) {
		cprintf("forking on core %d\n", i);
		r = pforkv(i, PFORK_SHARE_HEAP, ummap_shref, 1);
		if (r < 0)
			panic("unable to pfork worker to core %u: %s", i, e2s(r));
		if (r == 0) {
			cprintf("child %u on core %u calling join_worker()\n", i, i);
			join_worker(i);
		}
	}

	if (r > 0 && nworkers_ > 0) {
		join_worker(0);
	}

}

