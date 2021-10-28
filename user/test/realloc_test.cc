extern "C" {
#include <machine/x86.h>
#include <machine/memlayout.h>
#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/sysprof.h>
#include <inc/compiler.h>

#include <test.h>
}
#include <inc/jmonitor.hh>
#include <inc/error.hh>
#include <stdlib.h>

enum { core_count = 2 };
enum { alloc_size = 100 * 1024 * 1024 };

typedef struct {
    void *key;
    void *val;
} item_t;

typedef struct {
    unsigned len;
    unsigned alloc_len;
    item_t *arr;
} res_arr_t;

static int JSHARED_ATTR rows = 0;
static int JSHARED_ATTR cols = 0;
static res_arr_t ** JSHARED_ATTR matrix = 0;
static volatile int JSHARED_ATTR started = 0;

static void
init_arr(int r, int c)
{
    matrix = (res_arr_t **) malloc(r * sizeof(res_arr_t *));
    for (int i = 0; i < r; i++)
        matrix[i] = (res_arr_t *) calloc(c, sizeof(res_arr_t));
    rows = r;
    cols = c;
}

static void
insert(int row, int col)
{
    res_arr_t *bucket = &matrix[row][col];
    if (!bucket->alloc_len) {
        bucket->alloc_len = 8;
        bucket->arr = (item_t *)malloc(sizeof(item_t) * bucket->alloc_len);
    }
    else if (bucket->alloc_len == bucket->len) {
        bucket->alloc_len *= 2;
        bucket->arr = (item_t *)realloc(bucket->arr, sizeof(item_t) * bucket->alloc_len);
    }
    bucket->len ++;
}

static void __attribute__((noreturn))
do_test(void)
{
    while (!started)
        nop_pause();
    for (int i = 0; i < 100000; i++) {
        int col = jrand() % cols;
        insert(core_env->pid, col);
    }
    cprintf("Finished\n");
    while (1);
}

void __attribute__((noreturn))
realloc_test(void)
{
    init_arr(core_count, 2048);
    struct sobj_ref shs[2];
    shs[0] = *ummap_get_shref();
    for (uint32_t i = 1; i < core_count; i++) {
        int r = pforkv(i, PFORK_SHARE_HEAP, shs, 1);
        if (!r)
            do_test();
    }
    started = 1;
    do_test();
    while (1);
}


