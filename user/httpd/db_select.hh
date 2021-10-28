#ifndef JOS_USER_DB_SELECT_HH
#define JOS_USER_DB_SELECT_HH

#include <pkg/sqlite3/sqlite3.h>
#include <inc/types.h>

#include <app_shared.hh>

struct db_select_state {
    struct db_worker_state workers[max_nworkers][JOS_NCPU];	
    thread_mutex_t command_mu[max_nworkers][JOS_NCPU];
    struct sqlite3 * db;
    uint32_t num_rows;
    uint32_t pad_length;
};

class httpd_db_select
{
public:
    httpd_db_select(proc_id_t * pids, uint32_t nworkers, uint32_t num_rows, 
		uint32_t pad_length, uint32_t max_c2_val);
    ~httpd_db_select(void);
    uint64_t compute(uint32_t key);
private:
    uint32_t nworkers_;
	struct db_select_state * select_state_;
};

#endif

