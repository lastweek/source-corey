
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"
#define CREATE_TBL "create table t0(a integer, b integer);"
#define CREATE_INDX_T0_A "CREATE INDEX i2a ON t0(a);"
#define CREATE_INDX_T0_B "CREATE INDEX i2b ON t0(b);"
#define CREATE_INDX_T1_A "CREATE INDEX i2a ON t1(a);"
#define CREATE_INDX_T1_B "CREATE INDEX i2b ON t1(b);"
#define CREATE_INDX_T2_A "CREATE INDEX i2a ON t2(a);"
#define CREATE_INDX_T2_B "CREATE INDEX i2b ON t2(b);"
#define CREATE_INDX_T3_A "CREATE INDEX i2a ON t3(a);"
#define CREATE_INDX_T3_B "CREATE INDEX i2b ON t3(b);"
#define NUM_SELECTS (16 * 1024)
#define MAX_AVAL (32 * 1024)
#define MAX_BVAL 100

#define NUM_DATABASES 4

const char * dbname[NUM_DATABASES] = {
	"t0.db",
	"t1.db",
	"t2.db",
	"t3.db"
};

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

// random number
static unsigned int lcg_rand32 (unsigned int seed, unsigned int *state)
{
    static unsigned int A = 1664525;
    static unsigned int B = 1013904223;
    // static uint32_t M = 2 exp 32;
	if (seed) {
		*state = seed;
	}
	*state = *state * A + B;
	return (*state) ; // % M
}

int main()
{
	sqlite3 *db[NUM_DATABASES];
	int rc;
	char * s;
	uint64_t total_cycle;

	char * insert_fmt = "INSERT into t%u VALUES(%u, %u);";
	char * select_fmt = "SELECT b from t%u where a = %u;";
	char * create_fmt = "create table t%u(a integer, b integer);";
	char query_str [256];

	unsigned int rand_state;
	unsigned int rand;

	(void)lcg_rand32(1, &rand_state);

	fprintf(stdout, "MAX_AVAL: %d\n", MAX_AVAL);

	for (uint32_t i = 0; i < NUM_DATABASES; i++) {
		rc = sqlite3_open(dbname[i], &db[i]);
		if (rc) {
			printf("Can't open db: %s\n", sqlite3_errmsg(db[i]));
			sqlite3_close(db[i]);
			panic("Panic");
		}
	}
	fprintf(stdout, "done open\n");

	for (uint32_t i = 0; i < NUM_DATABASES; i++) {
		fprintf(stdout, "Begin INSERTs into db%d\n", i);
		sqlite3_execution(db[i], BEGIN_STR);
		sprintf(query_str, create_fmt, i);
		sqlite3_execution(db[i], query_str);
		for (uint32_t j = 0; j < MAX_AVAL; j++) {
			sprintf(query_str, insert_fmt, i, j, lcg_rand32(0, &rand_state) % MAX_BVAL);
			sqlite3_execution(db[i], query_str);
		}
		sqlite3_execution(db[i], COMMIT_STR);
		fprintf(stdout, "Done INSERTs into db%d\n", i);
	}
/*
	fprintf(stdout, "Begin CREATE index into db\n");
	sqlite3_execution(db[0], CREATE_INDX_T0_A);
	sqlite3_execution(db[0], CREATE_INDX_T0_B);
	sqlite3_execution(db[1], CREATE_INDX_T1_A);
	sqlite3_execution(db[1], CREATE_INDX_T1_B);
	sqlite3_execution(db[2], CREATE_INDX_T2_A);
	sqlite3_execution(db[2], CREATE_INDX_T2_B);
	sqlite3_execution(db[3], CREATE_INDX_T3_A);
	sqlite3_execution(db[3], CREATE_INDX_T3_B);
	fprintf(stdout, "Done CREATE index into db\n");
*/
	fprintf(stdout, "Begin SELECTs on db\n");
	uint64_t cycle0 = read_tsc();	
//	sqlite3_execution(db, BEGIN_STR);
	for (uint32_t i = 0; i < NUM_SELECTS; i++) {
		uint32_t db_id = random() % NUM_DATABASES;
		sprintf(query_str, select_fmt, db_id, 
			((lcg_rand32(0, &rand_state)) % MAX_AVAL));
		sqlite3_execution(db[db_id], query_str);
	}
//	sqlite3_execution(db, COMMIT_STR);
	total_cycle = read_tsc() - cycle0;
	fprintf(stdout, "Done SELECTs on db in %llu(us)\n", total_cycle * 1000000 / cpufreq);

	fprintf(stdout, "closing db\n");
	for (uint32_t i = 0; i < NUM_DATABASES; i++) {
		sqlite3_close(db[i]);
	}
	fprintf(stdout, "db closed\n");

	return(0);
}

