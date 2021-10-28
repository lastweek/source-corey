extern "C" {
#include <inc/lib.h>
#include <inc/atomic.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/cpuman.h>
#include <inc/arch.h>
}

#include <jhttpd.hh>

//enum { num_apps = 8 };
//enum { num_stacks = 8 };

enum { num_apps = 0 };
enum { num_stacks = 16 };

// filesum app knobs
enum { filesum_files = 16 };
enum { filesum_maxsize = 4 * 1024 * 1024 };
enum { filesum_test = 1 };

static httpd_filesum *the_summer;
static httpd_db_select *the_selector;
static httpd_db_join *the_joiner;

static struct cpu_state cs;

static struct {
    volatile int ready;
    uint64_t fsize;
    union {
	volatile uint64_t count;
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU];
}  *filesum_state;

static void
filesum_tester_thread(uint64_t x)
{
    for (int i = 0; i < filesum_files; i++)
	the_summer->compute(i, filesum_state->fsize);

    while (!filesum_state->ready)
	arch_pause();

    while (filesum_state->ready) {
	uint64_t k = jrand();
	
	the_summer->compute(k, filesum_state->fsize);
	filesum_state->cpu[core_env->pid].count++;
	if (core_env->pid == 0 && filesum_state->cpu[core_env->pid].count >= 10000)
	    break;
    }
}

static void
filesum_tester(void)
{
    int64_t r = segment_alloc(core_env->sh, sizeof(*filesum_state), 0,
			      (void **) &filesum_state, SEGMAP_SHARED,
			      "shared-seg", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));

    the_summer = new httpd_filesum(0, 0, filesum_maxsize, filesum_files);

    for (uint64_t k = 16 * 1024; k <= filesum_maxsize; k = k * 2) {
	memset(filesum_state, 0, sizeof(*filesum_state));
	filesum_state->fsize = k;

	for (int i = 1; i < num_stacks; i++) {
	    error_check(r = pfork(i));
	    if (r == 0) {
		for (int j = 0; j < 0; j++)
		    thread_create(0, "foo", filesum_tester_thread, 0);
		filesum_tester_thread(0);
		processor_halt();
	    }
	}

	for (int j = 0; j < 0; j++)
	    thread_create(0, "foo", filesum_tester_thread, 0);

	//time_delay_cycles(100000000);
		
	uint64_t s = arch_read_tsc();
	filesum_state->ready = 1;
	filesum_tester_thread(0);
	filesum_state->ready = 0;
	uint64_t e = arch_read_tsc();
	
	uint64_t count = 0;
	for (int i = 0; i < JOS_NCPU; i++)
	    count += filesum_state->cpu[i].count;
	uint64_t usec = (e - s) * 1000000 / core_env->cpufreq;
	uint64_t per_sec = (count * 1000000) / usec;
	cprintf(" %ld KB, count %ld, usec %ld, per/sec %ld\n", 
		k / 1024, count, usec, per_sec);
	time_delay_cycles(100000000);
    }
}

static struct {
    volatile int ready;
    union {
	jos_atomic64_t count;
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU];
}  *db_test_state;

static void
db_tester_thread(uint64_t x, app_type_t app)
{
    while (db_test_state->ready) {
	uint64_t k = jrand();
	//cprintf("calling the_selector->compute(%lu)\n", k);
	if (app == db_select_app) {
		the_selector->compute(k);
	} else {
		the_joiner->compute(k);
	}
	jos_atomic_inc64(&db_test_state->cpu[core_env->pid].count);
	if (jos_atomic_read(&db_test_state->cpu[core_env->pid].count) >= 10000)
	    break;
    }
}

static void
db_tester(app_type_t app)
{
    int64_t r = segment_alloc(core_env->sh, sizeof(*db_test_state), 0,
			      (void **) &db_test_state, SEGMAP_SHARED,
			      "shared-seg", core_env->pid);
    if (r < 0)
	panic("segment alloc failed: %s", e2s(r));

    proc_id_t pids[num_apps];
    for (int i = 0; i < num_apps; i++) {
		pids[i] = cpu_man_any(&cs);
	}
	if (app == db_select_app) {
	    the_selector = new httpd_db_select(pids, num_apps, dbsel_num_rows,
				db_pad_length, db_max_c2_val);
	} else {
		the_joiner = new httpd_db_join(pids, num_apps);
	}

    for (int i = 1; i < num_stacks; i++) {
	error_check(r = pforkv(cpu_man_any(&cs), PFORK_SHARE_HEAP, ummap_shref, 1));
	if (r == 0) {
	    while (!db_test_state->ready)
		nop_pause();
	    db_tester_thread(0, app);
	    processor_halt();
	}
    }

    db_test_state->ready = 1;
    uint64_t s = arch_read_tsc();
    db_tester_thread(0, app);
    db_test_state->ready = 0;
    uint64_t e = arch_read_tsc();

    uint64_t count = 0;
    for (int i = 0; i < JOS_NCPU; i++) {
		count += jos_atomic_read(&db_test_state->cpu[i].count);
	}
    uint64_t usec = (e - s) * 1000000 / core_env->cpufreq;
    uint64_t per_sec = (count * 1000000) / usec;
    cprintf("db_select_tester done: per_sec %ld\n", per_sec);
}

void __attribute__((noreturn))
app_tester(void)
{   
    cpu_man_init(&cs);
    if (filesum_test) {
	filesum_tester();
    }
    
    if (db_select_test) {
	db_tester(db_select_app);
    }
    
    if (db_join_test) {
	db_tester(db_join_app);
    }

    for (;;) {}
}
