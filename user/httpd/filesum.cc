extern "C" {
#include <machine/x86.h>
#include <stdio.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/kdebug.h>
#include <string.h>
#include <stdlib.h>
}

enum { multisum_hack = 1 };
enum { sumbuf_hack = 1 };
enum { nosum_hack = 0 };
//enum { constantwork_hack = 65536 };
enum { constantwork_hack = 0 };

enum { dumbmode_hack = 0 };

#include <filesum.hh>
#include <inc/error.hh>

static uint64_t
sumfile(struct fs_handle *fh, uint32_t fsize, uint32_t step)
{
    uint64_t sum = 0;
    uint8_t buf[4 * PGSIZE];

    uint64_t inc = JMIN(fsize, sizeof(buf));

    for (uint32_t i = 0; i < fsize; i += inc) {
        int64_t r = fs_pread(fh, buf, inc, i);
        assert(r > 0);
        for (uint8_t *ptr = buf; ptr < buf + r; ptr += step)
            sum += *ptr;
    }
    return sum;
}

static uint64_t
sumbuf(void *buf, uint32_t n, uint32_t step)
{
    uint64_t sum = 0;
    uint64_t *p = (uint64_t *)buf;
    uint64_t *q = p + (n / 8);
    
    for (; p < q; p += 4) {

	sum += p[0];
	sum += p[1];
	sum += p[2];
	sum += p[3];
	
    }
    
    return sum;
}

static uint64_t
workbuf(void *buf, uint32_t n, uint32_t step)
{
    uint64_t sum = 0;
    uint8_t *p = (uint8_t *)buf;
    uint8_t *q = p + n;
    
    for (; p < q; p += 128) {
	sum += p[0];
	sum += p[64];
    }
    
    return sum;
}

static uint64_t
do_sum(struct filesum_state * state, uint64_t i)
{
    uint64_t sum = 0;
    if (!constantwork_hack) {
	uint64_t todo = state->fsize;
	uint64_t fsize = JMIN(state->fsize, state->maxsize);
	
	while (todo) {
	    if (!sumbuf_hack)
		sum += sumfile(&state->fh[i], fsize, 1);
	    else
		sum += sumbuf(state->buf[i], fsize, 1);
	    todo -= fsize;
	}
    } else {
	assert(sumbuf_hack);
	
	uint64_t todo = constantwork_hack / multisum_hack;
	uint64_t fsize = state->fsize;
	
	while (todo) {
	    sum += workbuf(state->buf[i], fsize, 1);
	    todo -= fsize / 64;
	}
    }

    return sum;
}

/*
  * wid - the worker id [0 to nworker_core-1]
  * coreid - the core that the worker thread is working on[0 to nprocs-1]
  */
static void __attribute__((noreturn))
filesum_worker(struct filesum_state * state, int wid, int coreid)
{
    while (1) {
        for (int i = 0; i < JOS_NCPU; i++) {
            union worker_state *worker = &state->workers[wid][i];
            if (worker->state == idle)
                continue;

            worker->state = working;
            //dispatched. do compute
	    worker->sum = do_sum(state, worker->nfile);
	    worker->state = idle;
        }
        nop_pause();
    }
}

httpd_filesum::httpd_filesum(proc_id_t *pids, uint32_t nworker,
                             uint32_t fsize, uint32_t nfiles)
        : nworker_(nworker), nfiles_(nfiles)
{
    assert(max_nworkers >= nworker);

    int64_t r = segment_alloc(core_env->sh, sizeof(*app_state_), 0,
                              (void **) &app_state_, SEGMAP_SHARED,
                              "filesum-state-seg", core_env->pid);
    if (r < 0)
        panic("unable to allocate segment for app_state %s", e2s(r));


    for (int i = 0; i < max_nworkers; i++)
        for (int k = 0; k < JOS_NCPU; k++) {
            thread_mutex_init(&app_state_->command_mu[i][k].val);
        }

    if (!sumbuf_hack) {
	//initialize data(malloc or open file)
	app_state_->fh =
	    (struct fs_handle *)malloc(nfiles * sizeof(struct fs_handle));
	struct fs_handle dir, file;
	error_check(fs_namei("/x", &dir));
	char *buf = (char *)malloc(fsize);
	for (uint32_t i = 0; i < nfiles; i++) {
	    char name[16];
	    memset(buf, i + 1, fsize);
	    
	    snprintf(name, sizeof(name), "%u", i);
	    echeck(fs_create(&dir, name, &file));
	    assert(fs_pwrite(&file, buf, fsize, 0) == fsize);
	    app_state_->fh[i] = file;
	    // read to make sure data will be held in vfs0cache
	    assert(fs_pread(&file, buf, fsize, 0) == fsize);
	}
    } else {
	app_state_->buf = (void **) malloc(nfiles * sizeof(void *));
	for (uint32_t i = 0; i < nfiles; i++) {
	    void *buf = 0;
	    assert(segment_alloc(core_env->sh, fsize, 0, &buf, 
				 SEGMAP_SHARED, "foo", core_env->pid) == 0);
	    app_state_->buf[i] = buf;
	    memset(app_state_->buf[i], i, fsize);
	}
    }
    
    app_state_->fsize = fsize;
    app_state_->maxsize = fsize;

    //fork worker cores
    for (uint32_t i = 0; i < nworker; i++) {
        r = pfork(pids[i]);
        if (r < 0)
            panic("unable to pfork worker to core %u: %s", pids[i], e2s(r));
        if (r == 0)
            filesum_worker(app_state_, i, pids[i]);
    }
}

httpd_filesum::~httpd_filesum(void)
{
    //free memory. However, it should never comes here...
}

uint64_t
httpd_filesum::compute(uint32_t key, uint64_t fsize)
{
    if (nosum_hack)
	return 1;

    if (app_state_->fsize != fsize / multisum_hack) {
	//cprintf("filesum: fsize %u -> %ld\n", app_state_->fsize, fsize);
	app_state_->fsize = fsize / multisum_hack;
    }

    if (!nworker_) {
        uint32_t nfile = key % nfiles_;
	return do_sum(app_state_, nfile);
    }

    uint64_t sum = 0;
    uint32_t nfile;
    uint32_t wid;

    for (int i = 0; i < multisum_hack; i++, key++) {

        if (nfiles_ >= nworker_) {
            nfile = key % nfiles_;
            wid = nfile % nworker_;
        } else {
            wid = key % nworker_;
            nfile = wid % nfiles_;
        }

	if (dumbmode_hack)
	    wid = jrand() % nworker_;

        union worker_state *worker = &app_state_->workers[wid][core_env->pid];
        thread_mutex_t *mu = &app_state_->command_mu[wid][core_env->pid].val;

        thread_mutex_lock(mu);

        //send request and wait until finished
        worker->nfile = nfile;
        worker->state = dispatched;
        while (worker->state != idle)
            thread_yield();
        sum += worker->sum;
        thread_mutex_unlock(mu);
    }
    return sum;
}
