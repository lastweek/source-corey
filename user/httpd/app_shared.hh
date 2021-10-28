#ifndef JOS_USER_APP_SHARED_HH
#define JOS_USER_APP_SHARED_HH

void app_tester(void);

typedef enum {
    filesum_app,
    db_select_app,
    db_join_app,
} app_type_t;

enum { max_nworkers = 16 };
enum { idle = 0, dispatched, working };

// knobs for database apps
enum { db_select_test = 0 };
enum { db_join_test = 0 };
enum { db_num_dbs = 1 }; // number of DBs
enum { db_num_ops = 1 }; // number of ops (selects or joins) per HTTP request
enum { db_pad_length = 120 }; // size (B) of the pad (third column)
enum { db_max_c2_val = 16 }; // max value in second column
enum { use_streamflow = 1 };

// db_select-specific app knobs
enum { dbsel_num_rows = 128 }; // number of rows in the DB, for select
// db_join-specific app knobs
enum { dbjoin_global_num_rows = 128 };
enum { dbjoin_private_num_rows = 128 };

struct db_worker_state {
	volatile uint64_t total_time;
	volatile uint64_t time_per_req;
	volatile uint16_t ndb; // db to access
	volatile uint16_t state; // idle means finished
};


extern 	struct sobj_ref * ummap_shref; 
#endif

