#ifndef JOS_KERN_MAP_H
#define JOS_KERN_MAP_H

#include <machine/types.h>
#include <kern/pagetree.h>
#include <inc/hashtable.h>

struct Map 
{
    struct {
	struct pagetree back;
	struct hashtable table;
    } map[2];

    uint64_t npages;
    uint8_t  cur;
    proc_id_t pid;
};

struct Map_iter 
{
    struct hashiter iter;
};

int  map_put(struct Map *map, uint64_t key, uintptr_t value)
     __attribute__ ((warn_unused_result));
int  map_get(const struct Map *map, uint64_t key, uintptr_t *value) 
     __attribute__ ((warn_unused_result));
int  map_erase(struct Map *map, uint64_t key)
     __attribute__ ((warn_unused_result));
void map_init(struct Map *map, proc_id_t pid);
void map_free(struct Map *map);
void map_iter_init(struct Map_iter *iter, struct Map *map);
// Returns 1 if key and value hold new contents, 0 otherwise
int  map_iter_next(struct Map_iter *iter, uint64_t *key, uintptr_t *value);

#endif
