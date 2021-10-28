#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tagmm.h"

//#define USE_MALLOC

enum { block_size = 1024 * 1024 };
//Same as # cores
enum { init_tblk_num = 16 };
enum { init_blk_num = 8 };

struct block {
    char *data;
};

struct tag_blocks {
    int  tag;
    struct {
        struct block *blks;
        int  alloc_len;
        int  len;
        int  rest;
    } fixed;
    struct {
        struct block *blks;
        int len;
	int alloc_len;
    } bigblks;
};

static struct tag_blocks **tblocks;
static int ntblks;
static pthread_mutex_t lock;
static __thread struct tag_blocks *local_blk = NULL;
static __thread int local_tag = -1;

void 
tmalloc_init()
{
    int i;
    pthread_mutex_init(&lock, NULL);
    ntblks = init_tblk_num;
    tblocks = malloc(sizeof(struct tag_blocks*) * ntblks);
    for (i = 0; i < ntblks; i++)
        tblocks[i] = NULL;
}

void *
tmalloc(size_t s, int tag)
{
    void *p;
#ifdef USE_MALLOC
    p = malloc(s);
    if (p) 
        return p;
#endif
    s = (s + 3) & ~3;
    int i;
    if (!local_blk || local_tag != tag) {
        local_tag = tag;
        local_blk = malloc(sizeof(struct tag_blocks));
	local_blk->tag  = tag;

	local_blk->fixed.alloc_len = init_blk_num;
	local_blk->fixed.blks =(struct block *) malloc(sizeof(struct block) * 
						       local_blk->fixed.alloc_len);
	for (i = 0; i < local_blk->fixed.alloc_len; i++)
	    local_blk->fixed.blks[i].data = (char *) malloc(block_size);
	local_blk->fixed.len = 0;
	local_blk->fixed.rest = block_size;

	local_blk->bigblks.alloc_len = init_blk_num;
	local_blk->bigblks.len = 0;
	local_blk->bigblks.blks = (struct block *) malloc(sizeof(struct block) * 
							  local_blk->bigblks.alloc_len);
	memset(local_blk->bigblks.blks, 0, sizeof(struct block) * local_blk->bigblks.alloc_len);

        pthread_mutex_lock(&lock);
        for (i = 0; i < ntblks; i++)
            if (!tblocks[i]) {
	        tblocks[i] = local_blk;
	        break;
	    }
	if (i == ntblks) {
	   printf("Currently one tag for one thread only\n");
	   exit(0);
	}
        pthread_mutex_unlock(&lock);
    }
    if (s > block_size) {
        if (local_blk->bigblks.len == local_blk->bigblks.alloc_len) {
	    local_blk->bigblks.alloc_len *= 2;
	    local_blk->bigblks.blks = (struct block *)realloc(local_blk->bigblks.blks,
	    				sizeof(struct block) * local_blk->bigblks.alloc_len);
	}
        p = (char *)malloc(s);
	local_blk->bigblks.blks[local_blk->bigblks.len++].data = p;
    }
    else {
        if (local_blk->fixed.rest < s) {
            local_blk->fixed.len ++;
	    local_blk->fixed.rest = block_size;
        }
        if (local_blk->fixed.len == local_blk->fixed.alloc_len) {
	    local_blk->fixed.alloc_len *= 2;
            local_blk->fixed.blks = (struct block *) realloc(local_blk->fixed.blks, 
	                      sizeof(struct block) * local_blk->fixed.alloc_len);
            for (i = local_blk->fixed.alloc_len / 2; i < local_blk->fixed.alloc_len; i++)
	        local_blk->fixed.blks[i].data = (char *) malloc(block_size);
        }
        p = &local_blk->fixed.blks[local_blk->fixed.len].data[block_size - local_blk->fixed.rest];
        local_blk->fixed.rest -= s;
    }
    return p;
}

int 
tfree(int tag)
{
#ifdef USE_MALLOC
    return 0;
#endif
    int i, j;
    for (i = 0; i < ntblks; i++)
        if (tblocks[i] && tblocks[i]->tag == tag) {
            for (j = 0; j < tblocks[i]->fixed.alloc_len; j++)
                free(tblocks[i]->fixed.blks[j].data);
            free(tblocks[i]->fixed.blks);
	    for (j = 0; j < tblocks[i]->bigblks.len; j++)
		free(tblocks[i]->bigblks.blks[j].data);
	    free(tblocks[i]->bigblks.blks);
	    free(tblocks[i]);
	    tblocks[i] = NULL;
	}
    return 0;
}
