#ifndef JOS_USER_FILESUM_HH
#define JOS_USER_FILESUM_HH

#include <inc/types.h>
#include <inc/pad.h>

//enum { max_workers = 16 };
//enum { idle = 0, dispatched, working };
#include <app_shared.hh>

union worker_state{
    struct {
	volatile uint64_t total_time;
	volatile uint64_t time_per_req;
	volatile uint64_t sum;
	volatile uint32_t request_processed;
	volatile uint16_t nfile; //file to access
	volatile uint16_t state; //idle means finished
    };
    uint8_t __pad[JOS_CLINE];
};

struct filesum_state {
    union worker_state workers[max_nworkers][JOS_NCPU];	
    PAD_TYPE(thread_mutex_t, JOS_CLINE) command_mu[max_nworkers][JOS_NCPU];
    union {
	struct fs_handle *fh;
	void **buf;
    };
    uint32_t fsize;
    uint32_t maxsize;
};

class httpd_filesum
{
public:
    httpd_filesum(proc_id_t *worker_coreid, uint32_t nworker_core, 
		  uint32_t fsize, uint32_t nfiles);
    ~httpd_filesum(void);
    uint64_t compute(uint32_t key, uint64_t fsize);
private:
    uint32_t nworker_;
    uint32_t nfiles_;
    struct filesum_state *app_state_;
};

#endif
