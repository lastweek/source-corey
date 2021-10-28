
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

#ifdef NOT_JOS
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"
#define MAX_AVAL (32 * 1024)
#define MAX_BVAL 100

static int create_indexes_ = 0;
static int num_databases_ = 4;
static int num_threads_ = 1;
static int do_random_ = 0;
static int num_selects_ = (16 * 1024);
static sqlite3 **dbs_;
static char ** dbnames_ = NULL;

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

uint64_t cpufreq = 2391337000ULL;

uint64_t
read_tsc(void)
{
    uint32_t a, d;
    __asm __volatile("rdtsc" : "=a" (a), "=d" (d));
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}


static int callback(void *NotUsed, int argc, char **argv, char **azColName){
  return 0;
}

void panic(char * str) {
	fprintf(stderr, "%s\n", str);
	exit(1);
}

inline void sqlite3_execution(sqlite3 *db, const char *sql) 
{
	int rc;
	char *zErrMsg;

//	fprintf(stdout, "%s\n", sql);
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg); 
	if( rc!=SQLITE_OK ){
		printf("ERROR: %s\n", sql);
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		panic("Panic");
	} 
	return;
}

void select_thread (void * idp) {
	char query_str [256];
	char * select_fmt = "SELECT b from t%u WHERE a = %u;";
	uint32_t i, db_id, id = *(int*) idp;

	for (i = 0; i < num_selects_; i++) {
		fprintf(stdout, ".");
		db_id = do_random_ ? random() % num_databases_ : id;
		sprintf(query_str, select_fmt, db_id, random() % MAX_AVAL);
		sqlite3_execution(dbs_[db_id], query_str);
	} 
	return; 
}

int main(int argc, char * argv[])
{
	int rc;
	char * s;
	uint64_t total_cycle;
	uint32_t i, j;
	pthread_t * pids;
	uint32_t * ids;
#ifdef NOT_JOS
	struct stat statbuf;
#endif

	char * insert_fmt = "INSERT into t%u VALUES(%u, %u);";
	char * select_fmt = "SELECT b from t%u WHERE a = %u;";
	char * create_fmt = "CREATE table t%u(a INTEGER, b INTEGER);";
	char * create_index_acol_fmt = "CREATE INDEX i2a ON t%d(a);";
	char * create_index_bcol_fmt = "CREATE INDEX i2b ON t%d(b);";
	char query_str [256];

	unsigned int rand_state;
	unsigned int rand;

	if (argc > 1) {
		num_databases_ = atoi(argv[1]);
		num_selects_ = atoi(argv[2]);
	}

	// allocate the dynamic arrays
	dbnames_ = (char**) malloc (num_databases_ * sizeof (char*));
	for (i = 0; i < num_databases_; i++) {
		dbnames_[i] = (char*) malloc (16);
		sprintf(dbnames_[i], "t%d.db", i);
	}
#ifdef NOT_JOS
	for (i = 0; i < num_databases_; i++) {
		if (stat(dbnames_[i], &statbuf) == 0) {
			unlink(dbnames_[i]);	
		}
	}
#endif
	dbs_ = (sqlite3 **) malloc (num_databases_ * sizeof (sqlite3*));
	pids = (pthread_t *) malloc (num_threads_ * sizeof (pthread_t));
	ids = (uint32_t *) malloc (num_threads_ * sizeof (uint32_t));
	for (i = 0; i < num_threads_; i ++) {
		ids[i] = i;
	}

	fprintf(stdout, 
	"create_indexes_=%u,num_databases_=%u,num_threads_=%u,do_random_=%u\n", 
		create_indexes_, num_databases_, num_threads_, do_random_);

	fprintf(stdout, "MAX_AVAL: %d\n", MAX_AVAL);

	fprintf(stderr, "begin open\n");
	for (i = 0; i < num_databases_; i++) {
		rc = sqlite3_open(dbnames_[i], &dbs_[i]);
		if (rc) {
			printf("Can't open db: %s\n", sqlite3_errmsg(dbs_[i]));
			sqlite3_close(dbs_[i]);
			panic("Panic");
		}
	}
	fprintf(stdout, "done open\n");

	for (i = 0; i < num_databases_; i++) {
		fprintf(stdout, "Begin INSERTs into db%d\n", i);
		sqlite3_execution(dbs_[i], BEGIN_STR);
		sprintf(query_str, create_fmt, i);
		sqlite3_execution(dbs_[i], query_str);
		for (j = 0; j < MAX_AVAL; j++) {
			sprintf(query_str, insert_fmt, i, j, random() % MAX_BVAL);
			sqlite3_execution(dbs_[i], query_str);
		}
		sqlite3_execution(dbs_[i], COMMIT_STR);
		fprintf(stdout, "Done INSERTs into db%d\n", i);
	}

	// create indexes on the columns
	if (create_indexes_) {
		fprintf(stdout, "Begin CREATE index on dbs\n");
		for (i = 0; i < num_databases_; i++) {
			sprintf(query_str, create_index_acol_fmt, i);
			sqlite3_execution(dbs_[i], query_str);
			sprintf(query_str, create_index_bcol_fmt, i);
			sqlite3_execution(dbs_[i], query_str);
		}
		fprintf(stdout, "Done CREATE index on db\n");
	}

	fprintf(stdout, "Begin SELECTs on db\n");
	uint64_t cycle0 = read_tsc();

	for (i = 0; i < num_threads_; i ++) {
		pthread_create(&pids[i], NULL, &select_thread, (void *) &ids[i]);
	}

	// join all the threads
	for (i = 0; i < num_threads_; i ++) {
		pthread_join(&pids[i]);
	}

	total_cycle = read_tsc() - cycle0;
	fprintf(stdout, "Done SELECTs on db in %llu(us)\n", 
		total_cycle * 1000000 / cpufreq);

	fprintf(stdout, "closing dbs\n");
	for (i = 0; i < num_databases_; i++) {
		sqlite3_close(dbs_[i]);
	}
	fprintf(stdout, "dbs closed\n");

	return(0);
}

