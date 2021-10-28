
#include "query.hh"

// an example use of the dynamic query interface

#define NUM_OPS 1000

int main(void) {

	char * s;

	insert_sequence * s2 = new insert_sequence(2, 1000);
	fprintf(stderr, "%s\n", BEGIN_STR);
	while (s = s2->query_string()) {
		fprintf(stderr, "%s\n", s);
	}
	fprintf(stderr, "%s\n", COMMIT_STR);


	selectsum_sequence * s3 = new selectsum_sequence(3, 1000);
	fprintf(stderr, "%s\n", BEGIN_STR);
	while (s = s3->query_string()) {
		fprintf(stderr, "%s\n", s);
	}
	fprintf(stderr, "%s\n", COMMIT_STR);

	delete s2;
	delete s3;

	return(0);
}


