#include <kern/map.h>
#include <kern/lib.h>
#include <kern/arch.h>
#include <inc/error.h>

enum { rehash_debug = 0 };
enum { rehash_thresh = 70 };

#define N_HASHENTRY_PER_PAGE (PGSIZE / sizeof(struct hashentry))

static struct hashentry *
get_hash_ent(void *arg, uint64_t idx)
{
    struct pagetree *pt = (struct pagetree *)arg;
    uint64_t npage = idx / N_HASHENTRY_PER_PAGE;
    uint64_t pagei = idx % N_HASHENTRY_PER_PAGE;
    
    struct hashentry *entries;
    int r = pagetree_get_page(pt, npage, (void **)&entries, page_excl);
    if (r < 0)
	panic("pagetree_get_page failed: %s", e2s(r));
    return &entries[pagei];
}

static int
rehash(struct Map *map)
{
    int r = 0;
    uint64_t npages = map->npages ? map->npages * 2 : 1;
    struct hashtable *cur = &map->map[map->cur].table;
    struct pagetree *curpt = &map->map[map->cur].back;
    struct hashtable *next = &map->map[!map->cur].table;
    struct pagetree *nextpt = &map->map[!map->cur].back;
    
    if (rehash_debug)
	cprintf("map_put: trying to rehash from %ld pages (%d/%ld entires) "
		"to %ld pages (%ld entries)\n",
		map->npages, cur->size, map->npages * N_HASHENTRY_PER_PAGE, 
		npages, npages * N_HASHENTRY_PER_PAGE);
    
    pagetree_init(nextpt, 0, map->pid);
    for (uint64_t i = 0; i < npages; i++) {
	void *va;
	r = page_alloc(&va, map->pid);
	if (r < 0) {
	    page_free(&va);
	    break;
	}
	r = pagetree_put_page(nextpt, i, va);
	if (r < 0)
	    break;
	page_zero(va);
    }
    
    if (r < 0) {
	if (rehash_debug)
	    cprintf("map_put: rehash failed: %s\n", e2s(r));
	pagetree_free(nextpt);
	return r;
    }
    
    int n = npages * N_HASHENTRY_PER_PAGE;
    hash_init3(next, &get_hash_ent, nextpt, n);
    
    // Copy current hashtable to next one
    struct hashiter iter;
    hashiter_init(cur, &iter);
    while (hashiter_next(&iter))
	assert(hash_put(next, iter.hi_key, iter.hi_val) == 0);
    pagetree_free(curpt);
    
    map->npages = npages;
    map->cur = !map->cur;
    return 0;
}

int
map_put(struct Map *map, uint64_t key, uintptr_t value)
{
    assert(key);

    uint64_t percent = 100;
    if (map->map[map->cur].table.capacity)
	percent = (100 * map->map[map->cur].table.size) /  
	    map->map[map->cur].table.capacity;

    if (percent >= rehash_thresh) 
	rehash(map);
    return hash_put(&map->map[map->cur].table, key, value);
}

int
map_get(const struct Map *map, uint64_t key, uintptr_t *value)
{
    return hash_get(&map->map[map->cur].table, key, value);
}

int
map_erase(struct Map *map, uint64_t key)
{
    return hash_del(&map->map[map->cur].table, key);
}

void
map_free(struct Map *map)
{
    pagetree_free(&map->map[map->cur].back);
}

void
map_init(struct Map *map, proc_id_t pid)
{
    memset(map, 0, sizeof(*map));
    map->cur = 0;
    map->pid = pid;
}

int
map_iter_next(struct Map_iter *iter, uint64_t *key, uintptr_t *value)
{
    if (!hashiter_next(&iter->iter))
	return 0;

    if (key)
	*key = iter->iter.hi_key;
    if (value)
	*value = iter->iter.hi_val;
    return 1;
}

void
map_iter_init(struct Map_iter *iter, struct Map *map)
{
    hashiter_init(&map->map[map->cur].table, &iter->iter);
}
