
#include <stdlib.h>

#ifdef NOT_JOS
#define cprintf printf
#else
#include <inc/stdio.h>
#endif

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3.h"
#include <ctype.h>
#include <stdarg.h>

/* 
 * very simple example, it does this:
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
		case SQLITE_OK:
			cprintf("(SQLITE_OK)"); break;
		case SQLITE_BUSY:
			cprintf("(SQLITE_BUSY)"); break;
		case SQLITE_DONE:
			cprintf("(SQLITE_DONE)"); break;
		case SQLITE_ROW:
			cprintf("(SQLITE_ROW)"); break;
		case SQLITE_CANTOPEN:
			cprintf("(SQLITE_CANTOPEN)"); break;
		case SQLITE_ERROR:
			cprintf("(SQLITE_ERROR)"); break;
		case SQLITE_MISUSE:
			cprintf("(SQLITE_MISUSE)"); break;
		default:
			cprintf("(unknown value)"); 
	}
	cprintf("\n");
	return;
}

const char * sqlite_integer = "SQLITE_INTEGER";
const char * sqlite_float = "SQLITE_FLOAT";
const char * sqlite_text = "SQLITE_TEXT";
const char * sqlite_blob = "SQLITE_BLOB";
const char * sqlite_null = "SQLITE_NULL";
static const char * sqlite_type_string(int ty) {
	switch (ty) {
		case 1: return sqlite_integer;
		case 2: return sqlite_float;
		case 3: return sqlite_text;
		case 4: return sqlite_blob;
		case 5: return sqlite_null;
		default: 
			printf("error, bad type");
			return (NULL);
	}
}

#define MAX_PRINT_VAL 128
const char value_str[MAX_PRINT_VAL];
static const char * sqlite_value_string(struct sqlite3_stmt * q1, int col, int ty) {
	const char * s = value_str;
	switch (ty) {
		case 1: /* integer*/
			snprintf((char*)s, MAX_PRINT_VAL, "%d", sqlite3_column_int(q1, col));
			break;
		case 2: /* float */
			snprintf((char*)s, MAX_PRINT_VAL, "%0.4f", sqlite3_column_double(q1, col));
			break;
		case 3: /* text */
			snprintf((char*)s, MAX_PRINT_VAL, "%s", sqlite3_column_text(q1, col));
			break;
		case 4: /* blob */
			snprintf((char*)s, MAX_PRINT_VAL, "blob ..."); // XXX
			break;
		case 5: /* null */
			snprintf((char*)s, MAX_PRINT_VAL, "NULL"); 
			break;
		default:
			s = NULL;
			break;
	}
	return (s);
}

int 
main(void)
{
	sqlite3 * db;
	int ret, j, k, numcols;
	struct sqlite3_stmt *q1, *q2, *q3, *q4;
	const char * stmt, * s;

	cprintf("starting sqlite, opening test.db\n");
	cprintf("OPEN\n");
	ret = sqlite3_open("test.db", &db);
	print_error(ret);
	
	cprintf(Q1_STR); cprintf("\n");
	cprintf("PREPARE\n");
	ret = sqlite3_prepare(db, Q1_STR, 1024, &q1, &stmt); 
	print_error(ret);
	cprintf("STEP\n");
	ret = sqlite3_step(q1);
	print_error(ret);
	cprintf("FINALIZE\n");
	ret = sqlite3_finalize(q1);
	print_error(ret);
	cprintf("RESET\n");
	ret = sqlite3_reset(q1);
	print_error(ret);

	cprintf(Q2_STR); cprintf("\n");
	cprintf("PREPARE\n");
	ret = sqlite3_prepare(db, Q2_STR, 1024, &q2, &stmt); 
	print_error(ret);
	cprintf("STEP\n");
	ret = sqlite3_step(q2);
	print_error(ret);
	cprintf("FINALIZE\n");
	ret = sqlite3_finalize(q2);
	print_error(ret);
	cprintf("RESET\n");
	ret = sqlite3_reset(q2);
	print_error(ret);

	cprintf(Q3_STR); cprintf("\n");
	cprintf("PREPARE\n");
	ret = sqlite3_prepare(db, Q3_STR, 1024, &q3, &stmt); 
	print_error(ret);
	cprintf("STEP\n");
	ret = sqlite3_step(q3);
	print_error(ret);
	cprintf("FINALIZE\n");
	ret = sqlite3_finalize(q3);
	print_error(ret);
	cprintf("RESET\n");
	ret = sqlite3_reset(q3);
	print_error(ret);

	cprintf(Q4_STR);
	cprintf("\n");
	cprintf("PREPARE\n");
	ret = sqlite3_prepare(db, Q4_STR, 1024, &q4, &stmt); 
	print_error(ret);

	while ((ret = sqlite3_step(q4)) == SQLITE_ROW) {
		print_error(ret);
		numcols = sqlite3_column_count(q4);
		fprintf(stderr, "got %d columns\n", sqlite3_column_count(q4));
		for (j = 0; j < numcols; j ++) {
			k = sqlite3_column_type(q4, j);
			cprintf("data type: %s, ", (s = sqlite_type_string(k)) == NULL ? "NULL" : s);
			cprintf("value: %s\n", (s = sqlite_value_string(q4, j, k)) == NULL ? "NULL" : s);
		}
	}
	print_error(ret);

	ret = sqlite3_finalize(q4);
	ret = sqlite3_reset(q4);

	// get the values

	cprintf("sqlite: closing test.db\n");
	cprintf("CLOSE\n");
	ret = sqlite3_close(db);
	print_error(ret);
		
	cprintf("sqlite: done\n");
	return (0);
}

