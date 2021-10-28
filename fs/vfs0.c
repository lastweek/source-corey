#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/error.h>
#include <inc/stdio.h>
#include <inc/hashtable.h>
#include <inc/rwlock.h>
#include <inc/assert.h>
#include <fs/vfs0.h>
#include <fs/vfs0cache.h>
#include <fs/dev.h>

#include <string.h>

#define FS_CALL(h, fn, ...)					\
    ({								\
	struct fs_handle __w;					\
	__w.fh_dev_id = h->fh_vfs0.s->wrap_id;			\
	__w.fh_ref = h->fh_vfs0.wrap;				\
	struct fs_dev *__dev = fs_dev_get(&__w);		\
	if (__dev == 0 || !__dev->fn)				\
	    return -E_INVAL;					\
	__dev->fn(&__w, ##__VA_ARGS__);				\
    })

struct vfs0_state 
{
    struct sobj_ref seg;
    char wrap_id;
    struct vfs0cache *c;
};

int
vfs0_init(struct fs_handle *wrap, struct fs_handle *o)
{
    struct vfs0_state *s = 0;    
    struct sobj_ref seg;
    int r = segment_alloc(core_env->sh, sizeof(*s), &seg, (void **)&s, 
			  SEGMAP_SHARED, "vfs0-root", core_env->pid);
    if (r < 0)
	return r;

    memset(s, 0, sizeof(*s));
    s->seg = seg;
    s->wrap_id = wrap->fh_dev_id;

    r = vfs0cache_init(&s->c, core_env->sh);
    if (r < 0) {
	as_unmap(s);
	return r;
    }
	
    memset(o, 0, sizeof(*o));
    o->fh_dev_id = '0';
    o->fh_vfs0.wrap = wrap->fh_ref;
    o->fh_vfs0.s = s;
    return 0;
}

static int64_t
vfs0_file_pread(struct fs_handle *h, void *buf, uint64_t count, uint64_t off)
{
    struct fs_handle w;
    w.fh_dev_id = h->fh_vfs0.s->wrap_id;
    w.fh_ref = h->fh_vfs0.wrap;
    return data_cache_read(h->fh_vfs0.s->c, &w, buf, count, off);
}

static int64_t 
vfs0_file_pwrite(struct fs_handle *h, const void *buf, uint64_t count, uint64_t off)
{
    struct fs_handle w;
    w.fh_dev_id = h->fh_vfs0.s->wrap_id;
    w.fh_ref = h->fh_vfs0.wrap;
    return data_cache_write(h->fh_vfs0.s->c, &w, buf, count, off);
}

static int 
vfs0_file_create(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    int r = FS_CALL(h, file_create, fn, o);
    if (r < 0)
	return r;

    if (o) {
	o->fh_dev_id = '0';
	o->fh_vfs0.wrap = o->fh_ref;
	o->fh_vfs0.s = h->fh_vfs0.s;

	uint64_t parent_id = h->fh_vfs0.wrap.object;
	uint64_t fn_hash = hash_str(fn);
	uint64_t hash = hash_two(fn_hash, parent_id);
	meta_cache_put(h->fh_vfs0.s->c, hash, parent_id, fn, o);
    }
    return r;
}

static int 
vfs0_dir_lookup(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    int r;
    uint64_t parent_id = h->fh_vfs0.wrap.object;
    uint64_t fn_hash = hash_str(fn);
    uint64_t hash = hash_two(fn_hash, parent_id);
    r = meta_cache_get(h->fh_vfs0.s->c, hash, parent_id, fn, o);
    if (r == 0)
	return r;
    
    r = FS_CALL(h, dir_lookup, fn, o);
    if (r < 0)
	return r;

    if (o) {
	o->fh_dev_id = '0';
	o->fh_vfs0.wrap = o->fh_ref;
	o->fh_vfs0.s = h->fh_vfs0.s;
	meta_cache_put(h->fh_vfs0.s->c, hash, parent_id, fn, o);
    }
    return r;
}

static int 
vfs0_dir_mk(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    int r = FS_CALL(h, dir_mk, fn, o);
    if (r < 0)
	return r;

    if (o) {
	o->fh_dev_id = '0';
	o->fh_vfs0.wrap = o->fh_ref;
	o->fh_vfs0.s = h->fh_vfs0.s;
    }
    return r;
}

static int 
vfs0_pfork(struct fs_handle *root, struct sobj_ref *sh)
{
    return FS_CALL(root, fs_pfork, sh);
}

static int 
vfs0_stat(struct fs_handle *h, struct fs_stat *stat)
{
    return FS_CALL(h, fs_stat, stat);
}

struct fs_dev vfs0_dev = 
{
    .file_pread = vfs0_file_pread,
    .file_pwrite = vfs0_file_pwrite,
    .file_truncate = NULL,
    .file_create = vfs0_file_create,
    .dir_lookup = vfs0_dir_lookup,
    .dir_mk = vfs0_dir_mk,
    .fs_pfork = vfs0_pfork,
    .fs_stat = vfs0_stat,
    .fs_fsync = NULL,
};
