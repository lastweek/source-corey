#include <machine/param.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/share.h>
#include <inc/fs.h>
#include <inc/rwlock.h>
#include <inc/hashtable.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/array.h>
#include <inc/lifo.h>
#include <inc/spinlock.h>
#include <inc/pad.h>
#include <inc/debug.h>
#include <fs/vfs0cache.h>
#include <fs/vfs0.h>
#include <fs/dev.h>
#include <string.h>

enum { debug_write = 1 };
enum { debug_demand = 1 };

struct meta_lookup_args {
    const char *fn;
    uint64_t parent_id;
    struct vfs0cache *c;
};

struct data_bufhdr {
    volatile uint64_t offset;
    volatile uint64_t object_id;
    volatile uint64_t gen;
    char __pad[40];
};

struct data_buf {
    struct data_bufhdr hdr;
    union {
	uint8_t buf[BLK_BYTES];
	LIFO_ENTRY(data_buf) data_buf_link;
    };
};

struct data_buf_col {
    LIFO_HEAD(data_buf_lifo, data_buf) data_buf_lifo;
};

struct vfs0cache
{
    struct sobj_ref seg;

    // meta_lookup maps (parent_id, file name) -> meta_entry
    // meta2_lookup maps (file_id) -> meta_entry
    struct rwlock meta_lock;
    struct hashtable meta_lookup;
    struct hashentry meta_lookup_back[NENTS];
    struct meta_entry meta_cache[NENTS];
    struct hashtable meta2_lookup;
    struct hashentry meta2_lookup_back[NENTS];

    // one data_buf collection per CPU
    // XXX should use a growable segment, refill per-core lists
    // from global growing segment.
    //#define NDATA_BUF 12288
#define NDATA_BUF 8
    struct data_buf_col data_buf_col[JOS_NCPU];
    struct data_buf data_bufs[JOS_NCPU][NDATA_BUF];
};

static void * data_block_alloc(void *arg);
static void data_block_free(void *arg, void *buf);

typedef void (*data_iter_fn)(void *db, void *buf, uint64_t cc, uint64_t off,
			     struct vfs0cache *c, struct meta_entry *me,
			     uint64_t file_off);

int
vfs0cache_init(struct vfs0cache **c, uint64_t sh)
{
    struct sobj_ref seg;
    struct vfs0cache *x = 0;
    int r = segment_alloc(sh, sizeof(*x), &seg, (void **)&x,
			  SEGMAP_SHARED, "vfs0-cache", core_env->pid);
    if (r < 0)
	return r;
    memset(x, 0, sizeof(*x));
    x->seg = seg;
    
    hash_init(&x->meta_lookup, x->meta_lookup_back, NENTS);
    hash_init(&x->meta2_lookup, x->meta2_lookup_back, NENTS);
    rw_init(&x->meta_lock);

    for (uint32_t i = 0; i < array_size(x->meta_cache); i++)
	datatree_init(&x->meta_cache[i].datatree, data_block_alloc, 
		      data_block_free, x);

    for (uint32_t i = 0; i < JOS_NCPU; i++) {
	struct data_buf_col *col = &x->data_buf_col[i];
	LIFO_INIT(&col->data_buf_lifo);
	for (uint32_t k = 0; k < array_size(x->data_bufs[i]); k++) {
	    LIFO_PUSH(&col->data_buf_lifo, 
		      &x->data_bufs[i][k], 
		      data_buf_link);
	    x->data_bufs[i][k].hdr.offset = UINT64(~0);
	}
    }

    *c = x;
    return 0;
}

uint64_t
hash_str(const char *str)
{
    uint64_t h = 0;
    for (; *str != 0; str++)
	h = (h + (*str << 4) + (*str >> 4)) * 11;
    return h;
}

uint64_t
hash_two(uint64_t a, uint64_t b)
{
    a += (b ^ GOLDEN_RATIO_PRIME) / JOS_CLINE;
    a = a ^ ((a ^ GOLDEN_RATIO_PRIME) >> HASHBITS);
    return a;
}

static int
meta_lookup_skip(uint64_t val, void *arg)
{
    struct meta_lookup_args *args = arg;

    struct meta_entry *ce = (struct meta_entry *)(uintptr_t)val;
    if (!strcmp(args->fn, ce->name) && args->parent_id == ce->parent_id)
	return 0;
    return 1;
}

static uint64_t
meta_val_fn(uint64_t probe, uint64_t val, void *arg)
{
    struct meta_lookup_args *args = arg;    
    return (uint64_t) &args->c->meta_cache[probe];
}

int
meta_cache_get(struct vfs0cache *c, uint64_t hash, uint64_t parent_id, 
	       const char *fn, struct fs_handle *o)
{
    int r;
    struct meta_entry *ce;
    struct meta_lookup_args args = { fn, parent_id, c };    
   
    rw_read_lock(&c->meta_lock);
    r = hash_get_fn(&c->meta_lookup, hash, (uint64_t *)&ce, 
		    meta_lookup_skip, &args);
    if (r == 0) {
	if (o)
	    *o = ce->h;
    }

    rw_read_unlock(&c->meta_lock);
    return r;
}

int
meta_cache_put(struct vfs0cache *c, uint64_t hash, uint64_t parent_id, 
	       const char *fn, const struct fs_handle *o)
{
    int r;
    struct meta_entry *ce;
    struct meta_lookup_args args = { fn, parent_id, c };

    rw_write_lock(&c->meta_lock);
    r = hash_get_fn(&c->meta_lookup, hash, (uint64_t *)&ce,
		    meta_lookup_skip, &args);
    if (r < 0) {
	r = hash_put_fn(&c->meta_lookup, hash, (uint64_t *) &ce,
			  meta_val_fn, meta_lookup_skip, &args);
	if (r < 0)
	    panic("meta put failed: %s", e2s(r));

	r = hash_put(&c->meta2_lookup, o->fh_vfs0.wrap.object, (uint64_t)ce);
	if (r < 0)
	    panic("meta2 put failed: %s", e2s(r));

	strcpy(ce->name, fn);
	ce->parent_id = parent_id;
	ce->h = *o;
    }

    rw_write_unlock(&c->meta_lock);
    return r;
}

int
dent_lookup(struct vfs0cache *c, uint64_t object_id, struct meta_entry **me)
{
    return hash_get(&c->meta2_lookup, object_id, (uint64_t *)me);
}

#define FS_CALL(h, fn, ...)					\
    ({								\
	struct fs_dev *__dev = fs_dev_get(h);			\
	if (__dev == 0 || !__dev->fn)				\
	    return -E_INVAL;					\
	__dev->fn(h, ##__VA_ARGS__);				\
    })

static void *
data_block_alloc(void *a)
{
    struct vfs0cache *c = a;

    struct data_buf_col *col = &c->data_buf_col[core_env->pid];
    struct data_buf *db = LIFO_POP(&col->data_buf_lifo, data_buf_link);
    if (!db) {
	cprintf("data_block_alloc: out of blocks\n");
	return 0;
    }
    db->hdr.offset = UINT64(~0);
    return db->buf;
}

static void
data_block_free(void *a, void *buf)
{
    struct vfs0cache *c = a;
    struct data_buf *db = (struct data_buf *)((struct data_bufhdr *)buf - 1);
    
    uint64_t off = (uintptr_t)db - (uintptr_t)&c->data_bufs[0];
    assert(off % sizeof(struct data_buf) == 0);
    off = off / sizeof(struct data_buf);
    uint64_t cpu_i = off / NDATA_BUF;
    assert(cpu_i < JOS_NCPU);
    
    struct data_buf_col *col = &c->data_buf_col[cpu_i];
    LIFO_PUSH(&col->data_buf_lifo, db, data_buf_link);
}

static void
data_block_commit(void *buf, uint64_t offset, uint64_t object_id)
{
    struct data_bufhdr *hdr = ((struct data_bufhdr *)buf - 1);
    hdr->object_id = object_id;
    hdr->offset = offset;
    hdr->gen++;
}

static int
data_block_verify(void *buf, uint64_t offset, uint64_t object_id, uint64_t gen)
{
    struct data_bufhdr *hdr = ((struct data_bufhdr *)buf - 1);
    return (hdr->object_id == object_id && 
	    hdr->offset == offset &&
	    hdr->gen == gen);
}

static uint64_t
data_block_gen(void *buf)
{
    struct data_bufhdr *hdr = ((struct data_bufhdr *)buf - 1);
    return hdr->gen;
}

static int
data_cache_demand(struct vfs0cache *c, struct fs_handle *h, 
		  struct meta_entry *me, uint64_t off, void **ret)
{
    assert((off % BLK_BYTES) == 0);
    int r;
    void *d;
    
 again:
    r = datatree_get(&me->datatree, off / BLK_BYTES, &d);
    if (r < 0) {
	debug_print(debug_demand, "hard refill for %lu, off %lu",
		    h->fh_vfs0.wrap.object, off);
	
	d = data_block_alloc(c);
	assert(d);
	r = FS_CALL(h, file_pread, d, BLK_BYTES, off);
	if (r < 0)
	    return r;
	else if (r == 0) {
	    debug_print(debug_demand, "hard refill return 0");
	    data_block_free(c, d);
	    return -E_NOT_FOUND;
	}

	data_block_commit(d, off, h->fh_vfs0.wrap.object);
	
	r = datatree_put(&me->datatree, off / BLK_BYTES, d);
	if (r < 0) {
	    data_block_free(c, d);
	    if (r == -E_EXISTS)
		goto again;
	    return r;
	}
    }
    
    *ret = d;
    return 0;
}

static int64_t
data_cache_iter(struct vfs0cache *c, struct fs_handle *h, 
		void *buf, uint64_t count, uint64_t off,
		data_iter_fn cb)
{
    struct meta_entry *me;
    assert(dent_lookup(c, h->fh_ref.object, &me) == 0);
    // read_lock to make sure not removed from cache
    RW_READ_LOCK(&me->lock);

    int64_t r;
    uint64_t n = count;
    uint64_t o = off;
    while (n) {
	uint64_t blk_off = o % BLK_BYTES;
	uint64_t cc = JMIN(n, BLK_BYTES - blk_off);
	void *d = 0;
	
	r = data_cache_demand(c, h, me, o - blk_off, &d);
	if (r < 0) {
	    RW_READ_UNLOCK(&me->lock);
	    return r;
	}	
	
	cb(d, buf, cc, blk_off, c, me, o - blk_off);
	n -= cc;
	o += cc;
	buf += cc;
    }
    RW_READ_UNLOCK(&me->lock);
    return count;
}

static void
read_cb(void *db, void *buf, uint64_t cc, uint64_t db_off,
	struct vfs0cache *c, struct meta_entry *me, uint64_t file_off)
{
    uint64_t gen;
 loop:
    gen = data_block_gen(db);
    memcpy(buf, db + db_off, cc);
    if (!data_block_verify(db, file_off, me->h.fh_vfs0.wrap.object, gen)) {
	assert(datatree_get(&me->datatree, file_off / BLK_BYTES, &buf) == 0);
	goto loop;
    }
}

int64_t
data_cache_read(struct vfs0cache *c, struct fs_handle *h, 
		void *buf, uint64_t count, uint64_t off)
{
    // XXX If a file is created, and hasn't been written, read 
    // will return -E_NOT_FOUND
    return data_cache_iter(c, h, buf, count, off, read_cb);
}

static void
write_cb(void *db, void *buf, uint64_t cc, uint64_t db_off,
	 struct vfs0cache *c, struct meta_entry *me, uint64_t file_off)
{
    // Allocate new block from CPU local list
    int r;
    void *new_db = data_block_alloc(c);
    if (!new_db)
	panic("All out of data blocks");

    // Copy requested write
    memcpy(new_db + db_off, buf, cc);

    struct datatree_overwrite_arg arg = { db, 0, DATATREE_OVERWRITE_FORCE };
 loop:
    // Copy leading data
    if (db_off) {
	memcpy(new_db, arg.oldva, db_off);
	arg.flags &= ~DATATREE_OVERWRITE_FORCE;
    }

    // Copy trailing data
    if ((cc + db_off) != BLK_BYTES) {
	memcpy(new_db + cc + db_off, arg.oldva, BLK_BYTES - cc - db_off);
	arg.flags &= ~DATATREE_OVERWRITE_FORCE;
    }

    data_block_commit(new_db, file_off, me->h.fh_vfs0.wrap.object);
    
    // Put into datatree, which frees arg.oldva
    r = datatree_overwrite(&me->datatree, file_off / BLK_BYTES, &arg, new_db);
    if (r == -E_AGAIN)
	goto loop;
    assert(r == 0);
}

int64_t
data_cache_write(struct vfs0cache *c, struct fs_handle *h, 
		 const void *buf, uint64_t count, uint64_t off)
{
    int r = data_cache_iter(c, h, (void *)buf, count, off, write_cb);
    if (r == -E_NOT_FOUND) {
	debug_print(debug_write, "hard refill for %lu, count %lu, off %lu",
		    h->fh_vfs0.wrap.object, count, off);
	r = FS_CALL(h, file_pwrite, buf, count, off);	
    }
    return r;
}
