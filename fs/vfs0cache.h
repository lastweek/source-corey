#ifndef JOS_INC_VFS0CACHE_H
#define JOS_INC_VFS0CACHE_H

#include <inc/fs.h>
#include <inc/rwlock.h>
#include <fs/datatree.h>

#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
#define HASHBITS 7    
#define NENTS (1 << HASHBITS)
#define BLK_BYTES 4096

//#define RW_READ_LOCK(l) rw_read_lock(l)
//#define RW_READ_UNLOCK(l) rw_read_unlock(l)
//#define RW_LOCK struct rwlock

#define RW_READ_LOCK(l) srw_read_lock(l, core_env->pid)
#define RW_READ_UNLOCK(l) srw_read_unlock(l, core_env->pid)
#define RW_LOCK struct srwlock

struct vfs0cache;

struct meta_entry {
    struct fs_handle h;
    char name[FS_NAME_LEN];
    uint64_t parent_id;

    RW_LOCK lock;
    
    struct datatree datatree;
};

int vfs0cache_init(struct vfs0cache **c, uint64_t sh);

int meta_cache_get(struct vfs0cache *c, uint64_t hash, uint64_t parent_id, 
		   const char *fn, struct fs_handle *o);
int meta_cache_put(struct vfs0cache *c, uint64_t hash, uint64_t parent_id, 
		   const char *fn, const struct fs_handle *o);

int dent_lookup(struct vfs0cache *c, uint64_t object_id, 
		struct meta_entry **me);

int64_t data_cache_read(struct vfs0cache *c, struct fs_handle *h, 
			void *buf, uint64_t count, uint64_t off);

int64_t data_cache_write(struct vfs0cache *c, struct fs_handle *h, 
			 const void *buf, uint64_t count, uint64_t off);

uint64_t hash_str(const char *str);
uint64_t hash_two(uint64_t a, uint64_t b);

#endif
