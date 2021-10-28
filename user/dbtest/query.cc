
#include "query.hh"

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

insert_sequence::insert_sequence(int ncore, int n) {
	fmt_str = "INSERT into t%d VALUES(%d, %u, 'qsIHSINqsIHSINqsIHSINqsIHSINqsIHSINqsIHSINqsIHSINqsIHSINqsIHSIN');";
	core_num = ncore;
	num_ops = n;
	index = 0;
	query_str = (char*)malloc(strlen(fmt_str) + (3 * 8) + 1);
	(void)lcg_rand32(ncore, &rand_state);
}

char * 
insert_sequence::query_string(void) {
	if (index == num_ops) {
		index = 0;
		return NULL;
	}
	unsigned int rand = lcg_rand32(0, &rand_state);
	sprintf(query_str, fmt_str, core_num, index, rand); 
	index ++;
	return (query_str);
}

insert_sequence::~insert_sequence(void) {
	if (query_str) {
		free(query_str);
	}
}

selectsum_sequence::selectsum_sequence(int ncore, int n) {
	fmt_str = "SELECT count(*), avg(b) from t%d where t%d.b >= %u and t%d.b <= %u";
	core_num = ncore;
	num_ops = n;
	index = 0;
	query_str = (char*)malloc(strlen(fmt_str) + (5 * 8) + 1);
	(void)lcg_rand32(ncore, &rand_state);
}

char *
selectsum_sequence::query_string(void) {
	unsigned int max, min, k1, k2;
	if (index == num_ops) {
		index = 0;
		return NULL;
	}
 	k1 = lcg_rand32(0, &rand_state);
	k2 = lcg_rand32(0, &rand_state);
	if (k1 > k2) {
		max = k1; min = k2;
	} else {
		max = k2; min = k1;
	}
	sprintf(query_str, fmt_str, core_num, core_num, min, core_num, max);
	index ++;
	return (query_str);
}

char *
selectsum_sequence::query_string(int ncore) {
	unsigned int max, min, k1, k2;
	if (index == num_ops) {
		index = 0;
		return NULL;
	}
 	k1 = lcg_rand32(0, &rand_state);
	k2 = lcg_rand32(0, &rand_state);
	if (k1 > k2) {
		max = k1; min = k2;
	} else {
		max = k2; min = k1;
	}
	sprintf(query_str, fmt_str, ncore, ncore, min, ncore, max);
	index ++;
	return (query_str);
}

selectsum_sequence::~selectsum_sequence() {
	if (query_str) {
		free(query_str);
	}
}


