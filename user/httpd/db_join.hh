#ifndef JOS_USER_DB_JOIN_HH
#define JOS_USER_DB_JOIN_HH

#include <pkg/sqlite3/sqlite3.h>
#include <inc/types.h>

#include "app_shared.hh"

struct db_join_state {
    struct db_worker_state workers[max_nworkers][JOS_NCPU];	
    thread_mutex_t command_mu[max_nworkers][JOS_NCPU];
    struct sqlite3 ** dbs;
};

class httpd_db_join
{
public:
	httpd_db_join(proc_id_t *pids, uint32_t nworkers);
	~httpd_db_join(void);
	uint64_t compute(uint32_t key);
private:
	uint32_t nworkers_;
	uint32_t ndbs_;
	struct db_join_state *app_state_;
};

#endif

