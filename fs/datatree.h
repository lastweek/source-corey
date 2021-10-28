#ifndef JOS_FS_DATATREE_H
#define JOS_FS_DATATREE_H

#include <inc/spinlock.h>

/* 
 * Can hold (DATA_ENTRIES_PER_BLOCK ^ (DATATREE_HEIGHT + 1)) entries
 * or about 16777216 pages == 64 GB for the values below
 */

#define DATA_BLK_BYTES 4096
#define DATA_ENTRIES_PER_BLOCK (DATA_BLK_BYTES / sizeof(struct datatree_entry))
#define DATATREE_HEIGHT 2

struct datatree_entry {
    struct spinlock lock;
    union {
	void *va;
	jos_atomic64_t atomic_va;
	struct datatree_entry *ent;
    };
};

typedef void *(*data_block_alloc_fn)(void *arg); 
typedef void (*data_block_free_fn)(void *arg, void *va);

struct datatree {
    struct datatree_entry root;
    data_block_alloc_fn alloc;
    data_block_free_fn free;
    void *arg;
};

struct datatree_overwrite_arg {
    void *oldva;
    struct datatree_entry *ent;
#define DATATREE_OVERWRITE_FORCE 0x01
    int flags;
};

void datatree_init(struct datatree *dt, data_block_alloc_fn dalloc, 
		   data_block_free_fn dfree, void *arg);
void datatree_free(struct datatree *dt);
int  datatree_put(struct datatree *dt, uint64_t ient, void *va);
int  datatree_overwrite(struct datatree *dt, uint64_t ient, 
			struct datatree_overwrite_arg *arg, void *newva);
int  datatree_get(struct datatree *dt, uint64_t ient, void **va);

#endif
