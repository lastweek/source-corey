#ifndef HASHTABLE_H_
#define HASHTABLE_H_

#include <inc/types.h>

// simple hashtable, uses open addressing with linear probing

struct hashentry {
    uint64_t key;
    uint64_t val;
};

typedef struct hashentry *(*hash_get_ent_t)(void *arg, uint64_t idx);
typedef int (*hash_skip_fn)(uint64_t val, void *arg);
typedef uint64_t (*hash_val_fn)(uint64_t probe, uint64_t val, void *arg);

struct hashtable {
    union {
	struct hashentry *entry;
	struct hashentry **entry2;
	void *arg;
    };

    hash_get_ent_t hash_ent;
    int capacity;
    int size;
    int pgents;
};

struct hashiter {
    struct hashtable *hi_table;
    int hi_index;
    uint64_t hi_key;
    uint64_t hi_val;
};

void hash_init(struct hashtable *table, struct hashentry *back, int n);
void hash_init2(struct hashtable *table, struct hashentry **back, int n, int pgsize);
void hash_init3(struct hashtable *table, hash_get_ent_t get, void *arg, int n);
int hash_put(struct hashtable *table, uint64_t key, uint64_t val);
int hash_put_fn(struct hashtable *table, uint64_t key, uint64_t *val,
		hash_val_fn val_fn, hash_skip_fn skip, void *arg);
int hash_get(const struct hashtable *table, uint64_t key, uint64_t *val);
int hash_get_fn(const struct hashtable *table, uint64_t key, uint64_t *val,
		hash_skip_fn skip, void *arg);
int hash_del(struct hashtable *table, uint64_t key);
void hash_print(struct hashtable *table);

void hashiter_init(struct hashtable *table, struct hashiter *iter);
int hashiter_next(struct hashiter *iter);

#endif /*HASHTABLE_H_ */
