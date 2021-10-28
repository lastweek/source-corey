extern "C" {
#include <machine/x86.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <machine/atomic.h>
}

#include <stdio.h>
#include <string.h>
#include <inc/error.hh>
#include <stdlib.h>
#include <pkg/sqlite3/sqlite3.h>

#define MAX_CORE_NUM 16

#define CHECK_ERROR(a) if (a) {\
	perror("Error at line\n\t" #a "\nSystem Msg");\
	exit(1);\
}

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"

static int create_indexes_ = 1;
static uint32_t num_databases_ = 2;
static uint32_t num_threads_ = 2;
static uint32_t num_rows_ = 8 * 1024;
static int max_c2_val_ = 128 * 1024;
static int do_global_ = 0;
static int do_poke_ = 0;
static uint32_t num_selects_ = (8 * 1024);
static uint32_t c3_str_length_ = 116;
static int shared_address_ = 0;

static const char * poke_select_fmt_ = "SELECT c2 FROM t%u WHERE c1 = %u;";
static const char * scan_select_fmt_ = "SELECT count(*) FROM t%u WHERE c2 = %u;";

uint64_t cpufreq = 1595926000ULL;

struct sstate {
	uint32_t volatile result[MAX_CORE_NUM];
	uint32_t volatile channel[2];
};

static struct sstate *shared_state;

sqlite3 *dbs_[4];
const char *dbnames_[4] = {
	"t0.db",
	"t1.db",
	"t2.db",
	"t3.db"
};
void
print_average_time(uint64_t cycle)
{
	cprintf("average time : %lu(us)\n", cycle * 1000000 / cpufreq);
}


static int callback(void *NotUsed, int argc, char **argv, char **azColName){
  return 0;
}

inline void sqlite3_execution(sqlite3 *db, const char *sql) 
{
	int rc;
	char *zErrMsg;

	//fprintf(stdout, "%s\n", sql);

	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg); 
	if( rc!=SQLITE_OK ){
		printf("ERROR: %s\n", sql);
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		panic("Panic");
	} 
	return;
}

void *select_thread (void * idp) {
	char query_str [256];
	uint32_t i, db_id, id = *(uint32_t*)idp;

	for (i = 0; i < num_selects_; i++) {
		db_id = do_global_ ? random() % num_databases_ : id;
		sprintf(query_str, do_poke_ ? poke_select_fmt_ : scan_select_fmt_, 
			db_id, random() % num_rows_);
		sqlite3_execution(dbs_[db_id], query_str);
	} 
	return NULL; 
}

void usage() {
	cprintf("usage: ./bench create-indexes num-dbs num-selects num-threads num-rows max-c2-val do-random do-poke c3-str-length shared-address\n");
}


int main(int argc, char * argv[])
{
	int rc, db_id;
	uint64_t total_cycle;
	uint64_t average_cycle;
	uint32_t i, j, k;
	uint32_t proc_id = 0;
	uint32_t is_child = 0;

	const char * insert_fmt = "INSERT INTO t%u VALUES(%u, %u, \"%s\");";
	const char * create_fmt = "CREATE TABLE t%u(c1 INTEGER, c2 INTEGER, c3 STRING);";
	const char * create_index_fmt = "CREATE INDEX i2a ON t%d(c1);";
	char query_str [256];
	char * c3_str_buf = NULL;

	cprintf("begin open\n");
	for (uint32_t i = 0; i < num_databases_; i++) {
		rc = sqlite3_open(dbnames_[i], &dbs_[i]);
		if (rc) {
			cprintf("Can't open db: %s\n", sqlite3_errmsg(dbs_[i]));
			sqlite3_close(dbs_[i]);
			panic("Panic");
		}
	}
	cprintf("done open\n");

	for (i = 0; i < num_databases_; i++) {
		cprintf("inserting keys into db%d\n", i);
		sqlite3_execution(dbs_[i], BEGIN_STR);
		sprintf(query_str, create_fmt, i);
		sqlite3_execution(dbs_[i], query_str);
		for (j = 0; j < num_rows_; j++) {
			for (k = 0; k < c3_str_length_; k ++) {
				c3_str_buf[k] = (char) (0x61 + (random() % 26));
			}
			sprintf(query_str, insert_fmt, i, j, 
				random() % max_c2_val_, c3_str_buf);
			sqlite3_execution(dbs_[i], query_str);
		}
		sqlite3_execution(dbs_[i], COMMIT_STR);
		cprintf("done inserting keys into db%d\n", i);
	}

	// create indexes on the columns
	if (create_indexes_) {
		for (uint32_t i = 0; i < num_databases_; i++) {
			cprintf("creating index on db%d\n", i);
			sprintf(query_str, create_index_fmt, i);
			sqlite3_execution(dbs_[i], query_str);
			cprintf("done creating index on db\n");
		}
	}
	if (shared_address_) {
	} else {
		int64_t r = segment_alloc(default_share, sizeof(*shared_state), 0,
				(void **) &shared_state, SEGMAP_SHARED,
				"dbtest-share-seg", default_pid);

		if (r < 0)
			panic("segment_alloc failed: %s\n", e2s(r));

		shared_state->channel[0] = 0;
		shared_state->channel[1] = 0;
		for (uint32_t i = 0; i < num_threads_; i++) {
			shared_state->result[i] = 0;
		}

		for (uint32_t i = 1; i < num_threads_; i++) {
			shared_state->channel[0] = i;
			int r = pforkv(i, 0, NULL, 0);
			if (r < 0)
				panic("unable to pfork into core %u: %s", i, e2s(r));
			if (r) {
				//parent
				while (shared_state->channel[0]) {
					nop_pause();	
				}
			} else {
				//child
				is_child = 1;
				proc_id = shared_state->channel[0];
				for (uint32_t j = 0; j < num_databases_; j++) {
					rc = sqlite3_open(dbnames_[j], &dbs_[j]);
					if (rc) {
						cprintf("Can't open db: %s\n", sqlite3_errmsg(dbs_[j]));
						sqlite3_close(dbs_[j]);
						panic("Panic");
					}
				}
				shared_state->channel[0] = 0;
				break;
			}

		}

		if (is_child) {
			while (!shared_state->channel[1]) {
				nop_pause();
			}
		} else {
			shared_state->channel[1] = 1;	
		}

		uint64_t cycle0, cycle1;
		cycle0 = read_tsc();
		for (uint32_t i = 0; i < num_selects_; i++) {
			db_id = do_global_ ? random() % num_databases_ : proc_id;
			sprintf(query_str, do_poke_ ? poke_select_fmt_ : scan_select_fmt_,
					db_id, random() % num_rows_);
			sqlite3_execution(dbs_[db_id], query_str);
		}
		cycle1 = read_tsc();
		shared_state->result[proc_id] = cycle1 - cycle0;

		if (!is_child) {
			for (uint32_t i = 1; i < num_threads_; i++)
				while (!shared_state->result[i])
					nop_pause();
			uint64_t max_cycle = 0;
			total_cycle = 0;
			for (uint32_t i = 0; i < num_threads_; i++) {
				total_cycle += shared_state->result[i];
				if (shared_state->result[i] > max_cycle)
					max_cycle = shared_state->result[i];
			}
			average_cycle = total_cycle / num_threads_;
			print_average_time(average_cycle);
			cprintf("%u max: %lu(us)\n", num_threads_, max_cycle * 1000000 / cpufreq);
		}
	}

	for (uint32_t i = 0; i < num_databases_; i++) {
		sqlite3_close(dbs_[i]);
	}

	return(0);
}


