
#include <stdlib.h>

#include <inc/stdio.h>

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3.h"
#include <ctype.h>
#include <stdarg.h>

/* 
 * very simple example:
 * sqlite> create table tbl1(one varchar(10), two smallint);
 * sqlite> insert into tbl1 values('hello!',10);
 * sqlite> insert into tbl1 values('goodbye', 20);
 * sqlite> select * from tbl1;
 */

#define Q1_STR "create table tbl1(one varchar(10), two smallint);"
#define Q2_STR "insert into tbl1 values('hello!',10);"
#define Q3_STR "insert into tbl1 values('goodbye', 20);"
#define Q4_STR "select * from tbl1;"

static void print_error(int code) {
	fprintf(stderr, "error = %d ", code);
	switch (code) {
		case SQLITE_BUSY:
			cprintf("(SQLITE_BUSY)"); break;
		case SQLITE_DONE:
			cprintf("(SQLITE_DONE)"); break;
		case SQLITE_ROW:
			cprintf("(SQLITE_ROW)"); break;
		case SQLITE_ERROR:
			cprintf("(SQLITE_ERROR)"); break;
		case SQLITE_MISUSE:
			cprintf("(SQLITE_MISUSE)"); break;
		default:
			fprintf(stderr, "(unknown value)"); 
	}
	fprintf(stderr, "\n");
	return;
}

int 
main(void)
{
	sqlite3 * db;
	int ret, err;
	struct sqlite3_stmt *q1, *q2, *q3, *q4;
	const char * stmt;

	//fprintf(stderr, "hello from SQLite\n");
	cprintf("[sqlOS]: start\n");
	cprintf("[sqlOS]: opening foo.db\n");
	if ((err = sqlite3_open("foo.db", &db)) != SQLITE_OK) {
		cprintf("splite3_open error\n");
		goto done;
	}
	
	cprintf(Q1_STR);
	cprintf("\n");
	ret = sqlite3_prepare(db, Q1_STR, 1024, &q1, &stmt); 
	ret = sqlite3_step(q1);
	print_error(ret);
	ret = sqlite3_finalize(q1);
	ret = sqlite3_reset(q1);

	cprintf(Q2_STR);
	cprintf("\n");
	ret = sqlite3_prepare(db, Q2_STR, 1024, &q2, &stmt); 
	ret = sqlite3_step(q2);
	print_error(ret);
	ret = sqlite3_finalize(q2);
	ret = sqlite3_reset(q2);

	cprintf(Q3_STR);
	cprintf("\n");
	ret = sqlite3_prepare(db, Q3_STR, 1024, &q3, &stmt); 
	ret = sqlite3_step(q3);
	print_error(ret);
	ret = sqlite3_finalize(q3);
	ret = sqlite3_reset(q3);

	cprintf(Q4_STR);
	cprintf("\n");
	ret = sqlite3_prepare(db, Q4_STR, 1024, &q4, &stmt); 

	// SQLITE_BUSY, SQLITE_DONE, SQLITE_ROW, SQLITE_ERROR
	while ((ret = sqlite3_step(q4)) == SQLITE_ROW) {
		fprintf(stderr, "got %d columns\n", sqlite3_column_count(q4));
	}
	print_error(ret);

	ret = sqlite3_finalize(q4);
	ret = sqlite3_reset(q4);

	// get the values

	cprintf("[sqlOS]: closing foo.db\n");
	if ((err = sqlite3_close(db)) != SQLITE_OK) {
		cprintf("splite3_close error\n");
		goto done;
	}

done:
	cprintf("[sqlOS]: done\n");
	return (0);
}

