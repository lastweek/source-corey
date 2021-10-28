/* Copyright (c) 2007, Stanford University
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of Stanford University nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#ifndef __WIN__
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sched.h>
#define TCHAR char
#else
#include "mr-common.h"
#endif
#include "mr-config.h"
#include "mr-sched.h"
#include "bench.h"

#define DEFAULT_NDISP 10

#define RAND_INPUT

#ifdef JOS_USER
#ifndef RAND_INPUT
#include "wc-datafile.h"
#endif
#include <inc/sysprof.h>
#include <inc/lib.h>
#include <inc/syscall.h>
enum { josmp_min_core = 16 };
enum { josmp_max_core = 16 };
enum { josmp_runs = 1 };
#endif

typedef struct {
    uint64_t fpos;
    uint64_t flen;
    char *fdata;
    int unit_size;
} wc_data_t;

enum {
    IN_WORD,
    NOT_IN_WORD
};

static int
mystrcmp(const void *s1, const void *s2)
{
    enterapp();
    int res = strcmp((const char *) s1, (const char *) s2);
    leaveapp();
    return res;
}

static int
wordcount_splitter(void *data_in, unsigned req_units, map_arg_t * out)
{
    wc_data_t *data = (wc_data_t *) data_in;
    assert(data_in);
    assert(out);
    assert(req_units);
    assert(data->fdata);
    /* EOF, return FALSE for no more data */
    if (data->fpos >= data->flen)
        return 0;
    enterapp();
    out->data = (void *) &data->fdata[data->fpos];
    out->length = req_units * data->unit_size;
    if ((unsigned long)(data->fpos + out->length) > data->flen)
        out->length = data->flen - data->fpos;

    /* set the length to end at a space */
    for (data->fpos += (long) out->length;
         data->fpos < data->flen &&
             data->fdata[data->fpos] != ' ' && data->fdata[data->fpos] != '\t' &&
             data->fdata[data->fpos] != '\r' && data->fdata[data->fpos] != '\n' &&
             data->fdata[data->fpos] != 0 ;
         data->fpos++, out->length++) ;
    leaveapp();
    return 1;
}

static void
wordcount_map(map_arg_t * args)
{
    char *curr_start, curr_ltr;
    int state = NOT_IN_WORD;
    uint32_t i;
    enterapp();
    assert(args);
    char *data = (char *) args->data;
    assert(data);
    curr_start = data;
    for (i = 0; i < args->length; i++) {
        curr_ltr = toupper(data[i]);
        switch (state) {
	    case IN_WORD:
            data[i] = curr_ltr;
            if ((curr_ltr < 'A' || curr_ltr > 'Z') && curr_ltr != '\'') {
                data[i] = 0;
                leaveapp();
                emit_intermediate(curr_start, (void *) curr_start,
                                  &data[i] - curr_start + 1);
                enterapp();
                state = NOT_IN_WORD;
            }
            break;
	    default:
            if (curr_ltr >= 'A' && curr_ltr <= 'Z') {
                curr_start = &data[i];
                data[i] = curr_ltr;
                state = IN_WORD;
            }
            break;
	    }
	}

    /* add the last word */
    if (state == IN_WORD) {
        data[args->length] = 0;
        leaveapp();
        emit_intermediate(curr_start, (void *) curr_start, &data[i] - curr_start + 1);
        enterapp();
    }
    leaveapp();
}

static void
do_mapreduce(int nprocs, int map_tasks, int reduce_tasks,
             void *fdata, size_t len, final_data_t *wc_vals)
{
    mr_param_t mr_param;
    wc_data_t wc_data;
    uint64_t starttime, durtime;

    wc_data.unit_size = 5;	/* approx 5 bytes per word */
    wc_data.fpos = 0;
    wc_data.flen = len;
    wc_data.fdata = fdata;

    memset(&mr_param, 0, sizeof(mr_param_t));
    memset(wc_vals, 0, sizeof(*wc_vals));
    mr_param.nr_cpus = nprocs;
    mr_param.task_data = &wc_data;
    mr_param.final_results = wc_vals;
    mr_param.map_func = wordcount_map;
    mr_param.reduce_func = NULL;
    mr_param.keep_reduce_array = 1;
    mr_param.local_reduce_func = NULL;
    mr_param.split_func = wordcount_splitter;
    mr_param.part_func = NULL;
    mr_param.key_cmp = mystrcmp;
    mr_param.out_cmp = NULL;

    mr_param.data_size = len;
    mr_param.unit_size = wc_data.unit_size;
    mr_param.split_size = mr_l2_cache_size / mr_param.unit_size;
    mr_param.presplit_map_tasks = map_tasks;
    mr_param.reduce_tasks = reduce_tasks;

    starttime = read_tsc();
    echeck(mr_run_scheduler(&mr_param));
    durtime = read_tsc() - starttime;

    print_phase_time();
#ifdef JOS_USER
    josmp_results.times[nprocs - 1].tot_over += durtime * 1000000 / get_cpu_freq();
    josmp_results.times[nprocs - 1].cmd_lat += core_cmd_lat;
#endif
    printf("%ld\n", durtime * 1000 / get_cpu_freq());
}

static void
print_top(final_data_t *wc_vals, int ndisp)
{
    uint64_t occurs = 0;
    for (uint32_t i = 0; i < wc_vals->length; i++) {
        keyval_t *curr = &((keyval_t *) wc_vals->data)[i];
        occurs += (uint64_t)curr->val;
    }
    printf("\nwordcount: results (TOP %d from %ld keys, %ld words):\n", ndisp,
           wc_vals->length, occurs);
    for (uint32_t i = 0; i < (uint32_t)ndisp && i < wc_vals->length; i++) {
        keyval_t *curr = &((keyval_t *) wc_vals->data)[i];
        printf("%15s - %d\n", (char *) curr->key,
               (unsigned) (size_t) curr->val);
    }
}

#ifdef RAND_INPUT
static uint32_t
frnd(uint32_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return *seed & 0x7fffffff;
}
#endif

int
main(int argc, TCHAR * argv[])
{
    final_data_t wc_val;
#ifdef JOS_USER
    sysprof_init();
    sysprof_prog_cmdcnt(0);
    sysprof_prog_latcnt(1);
#else
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);
    echeck(sched_setaffinity(0, sizeof(cpu_set), &cpu_set));
#endif

#ifdef RAND_INPUT
    enum { maxlen = 3 };
    uint32_t seed = 0;
    size_t len = 0x40000000;
    char *fdata = (char *)malloc(len + 1);
    size_t gened = 0;
    size_t nwords = 0;
    while (1) {
        uint32_t wlen = frnd(&seed) % (maxlen - 1) + 1;
        wlen = 3;
        if (gened + wlen > len) {
            break;
        }
        nwords ++;
        for (uint32_t i = 0; i < wlen; i ++)
            fdata[gened++] = frnd(&seed) % 26 + 'A';
        fdata[gened++] = ' ';
    }
    printf("generated 0x%lx bytes, %ld words\n", gened + 1, nwords);
    while (gened < len)
        fdata[gened++] = 0;
#else
    size_t orglen;
    char *forg;
#ifdef JOS_USER
    orglen = sizeof(wc_data_file) ;
    forg = (char *)wc_data_file;
#else
    int fd;
    struct stat finfo;
    echeck((fd = open("wc_trim_50KB.txt", O_RDONLY)));
    echeck(fstat(fd, &finfo) < 0);
    echeck((forg = mmap(0, finfo.st_size + 1,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
                        0)) == NULL);
    orglen = finfo.st_size;
#endif
    enum { iter = 20000 };
    char *fdata = (char *)malloc((orglen + 1) * iter);
    printf("wr1:file_length is %ld original data %p copied %p\n", orglen, forg, fdata);
    for (uint32_t i = 0; i < iter ; i ++)
        memcpy(fdata + i * orglen, forg, orglen);
    size_t len = orglen * iter;
#endif

    unsigned int ncore = 1;
    unsigned int run = 0;

#ifdef JOS_USER
    memcpy(&josmp_results, boot_args, sizeof(josmp_results));
    if (josmp_results.core < josmp_min_core) {
        josmp_results.core = josmp_min_core;
        josmp_results.run = 0;
    } else {
        josmp_results.run++;
        if (josmp_results.run >= josmp_runs) {
            josmp_results.run = 0;
            josmp_results.core++;
        }
    }
    ncore = josmp_results.core;
    run = josmp_results.run;
#endif

    printf("Using %u cores (run %u)\n", ncore, run);

    do_mapreduce(ncore, 2048, 2048, fdata, len, &wc_val);
    print_top(&wc_val, 5);

#ifdef JOS_USER
    if (josmp_results.core >= josmp_max_core &&
        josmp_results.run == josmp_runs - 1)
    {
        printf("%3s %9s %9s %9s %9s %9s %9s\n",
               "", "map", "reduce", "merge(us)", "tot",  "tot+over", " cmdcyc");

        for (int i = 0; i < JOS_NCPU; i++) {
            unsigned int map =
                josmp_results.times[i].map / josmp_runs;
            unsigned int reduce =
                josmp_results.times[i].reduce / josmp_runs;
            unsigned int merge =
                josmp_results.times[i].merge / josmp_runs;
            unsigned int tot =
                josmp_results.times[i].tot / josmp_runs;
            unsigned int tot_over =
                josmp_results.times[i].tot_over / josmp_runs;
            unsigned int cmd_lat =
                josmp_results.times[i].cmd_lat / josmp_runs;
            printf("%3u %9u %9u %9u %9u %9u %9u\n",
                   i + 1,
                   map/1000,
                   reduce/1000,
                   merge,
                   tot/1000,
                   tot_over/1000,
                   cmd_lat);
        }
        for (;;);
    }

    assert(sys_machine_reinit((char *)&josmp_results, sizeof(josmp_results)) == 0);
    printf("sys_machine_reinit failed...\n");
    assert(0);
#endif

    while (1);
    finit_mr();
    return 0;
}
