extern "C" {
#include <inc/stdio.h>
#include <inc/assert.h>
}
#include <pkg/sqlite3/sqlite3.h>

//#define SIMPLE 1

const char *dbname = "test.db";

#ifdef SIMPLE
const char *sql_query[] = {   "create table tbl1(one varchar(10), two smallint);",
                        "insert into tbl1 values('hello!',10);",
                        "insert into tbl1 values('goodbye', 20);",
                        "select * from tbl1;"
                     };
#else

const char *sql_query[] = {
#include "sqlitec_query.1001_inserts"
#include "sqlitec_query.1003_inserts_in_one_trans"
#include "sqlitec_query.1_join_wo_index"
#include "sqlitec_query.100_select_wo_index"
#include "sqlitec_query.100_select_on_str_comp"
#include "sqlitec_query.2_create_index"
#include "sqlitec_query.1000_select_wt_index"
#include "sqlitec_query.1002_update_wo_index"
#include "sqlitec_query.1002_update_wt_index"
#include "sqlitec_query.1002_txt_update_wt_index"
#include "sqlitec_query.4_insert_from_select"
#include "sqlitec_query.1_delete_wo_index"
#include "sqlitec_query.1_delete_wt_index"
#include "sqlitec_query.1_big_insert_after_big_delete"
#include "sqlitec_query.1003_big_delete_followed_by_small_insert"
#include "sqlitec_query.2_drop_table"
};
#endif

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
  int i;
  for(i=0; i<argc; i++){
    cprintf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  }
  cprintf("\n");
  return 0;
}

int main(int argc, char **argv){
	sqlite3 *db;
	char *zErrMsg = 0;
	int rc;
	unsigned int query_num = sizeof(sql_query)/sizeof(char *);

	rc = sqlite3_open(dbname, &db);
	if( rc ){
		cprintf("Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		panic("Panic");
	}

	for (unsigned int i = 0; i < query_num; i++) {
		//	cprintf("CLIENT: query[%d]: %s\n", i, sql_query[i]);
		rc = sqlite3_exec(db, sql_query[i], callback, 0, &zErrMsg);
		if( rc!=SQLITE_OK ){
			cprintf("ERROR[%d]: %s\n", i, sql_query[i]);
			cprintf("SQL error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
			panic("Panic");
		} else {
			cprintf("OK[%d]: %s\n", i, sql_query[i]);
		}
	}

	sqlite3_close(db);
	return 0;
}


