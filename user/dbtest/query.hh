
#ifndef USER_DBTEST_QUERY_HH
#define USER_DBTEST_QUERY_HH

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define COMMIT_STR "COMMIT;"
#define BEGIN_STR "BEGIN;"

class query_sequence
{
public:
	query_sequence(void) {}
	virtual char * query_string() = 0;
	~query_sequence(void) {}
};

// subclass query_sequence
class insert_sequence : query_sequence
{
public:
	insert_sequence(int ncore, int n);
	char * query_string() ;
	~insert_sequence();
private:
	int index;
	int num_ops;
	const char * fmt_str;
	char * query_str;
	int core_num;
	unsigned int rand_state;
};

class selectsum_sequence : query_sequence 
{
public:
	selectsum_sequence(int ncore, int n);
	char * query_string();
	char * query_string(int);
	~selectsum_sequence();
private:
	int index;
	int num_ops;
	const char * fmt_str;
	char * query_str;
	int core_num;
	unsigned int rand_state;
};

#endif

