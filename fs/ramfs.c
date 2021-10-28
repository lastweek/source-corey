#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/error.h>
#include <inc/fs.h>
#include <fs/ramfs.h>
#include <fs/dev.h>
#include <string.h>
#include <sys/stat.h>
#include <inc/stdio.h>

#define RAMFS_NDENT 64

struct ramfs_dent {
    char name[FS_NAME_LEN];
    struct sobj_ref seg;
};

struct ramfs_dir {
    char type;
    thread_mutex_t ent_mu;
    struct ramfs_dent ent[RAMFS_NDENT];
};

struct ramfs_file {
    char type;
    thread_mutex_t ram_mu;
    char ram[0];
};

enum { dir_type, file_type };
enum { debug = 1 };

typedef union ramfs_inode {
    char type;
    struct ramfs_dir dir;
    struct ramfs_file file;
    char pad[8192];
} ramfs_inode_t;

int
ramfs_init(struct fs_handle *h)
{
    int64_t sh;
    sh = sys_share_alloc(core_env->sh, 1 << kobj_segment,
			 "ramfs-share", core_env->pid);
    if (sh < 0)
	return sh;

    memset(h, 0, sizeof(*h));
    h->fh_dev_id = 'r';

    ramfs_inode_t *ino = 0;
    int r = segment_alloc(sh, sizeof(ramfs_inode_t), &h->fh_ramfs.seg,
			  (void **) &ino, 0, "ramfs-root", core_env->pid);
    if (r < 0) {
	sys_share_unref(SOBJ(core_env->sh, sh));
	sys_self_drop_share(sh);
	return r;
    }
    memset(ino, 0, sizeof(*ino));

    ino->type = dir_type;
    as_unmap(ino);
    return 0;
}

static int
ramfs_map_dir(struct sobj_ref seg, uint64_t flags, struct ramfs_dir **dir)
{
    ramfs_inode_t *ino = 0;
    int r = as_map(seg, 0, flags, (void **) &ino, 0);
    if (r < 0)
	return r;

    if (ino->type != dir_type) {
	as_unmap(ino);
	return -1;
    }

    *dir = &ino->dir;
    return 0;
}

static int
ramfs_map_file(struct sobj_ref seg, uint64_t flags, struct ramfs_file **file)
{
    ramfs_inode_t *ino = 0;
    int r = as_map(seg, 0, flags, (void **) &ino, 0);
    if (r < 0)
	return r;

    if (ino->type != file_type) {
	as_unmap(ino);
	return -1;
    }

    *file = &ino->file;
    return 0;
}

static int
ramfs_inode_alloc(struct fs_handle *h, const char *name, struct fs_handle *o,
		  char type)
{
    struct ramfs_dir *dir = 0;
    int r = ramfs_map_dir(h->fh_ramfs.seg, SEGMAP_WRITE | SEGMAP_READ, &dir);
    if (r < 0)
	return r;

    thread_mutex_lock(&dir->ent_mu);

    for (int i = 0; i < RAMFS_NDENT; i++) {
	struct ramfs_dent *ent = &dir->ent[i];
	if (ent->name[0] == '\0') {
	    uint64_t sh = h->fh_ramfs.seg.share;

	    struct sobj_ref seg;
	    ramfs_inode_t *ino = 0;
	    r = segment_alloc(sh, sizeof(ramfs_inode_t),
			      &seg, (void **) &ino, 0, name, core_env->pid);
	    if (r < 0) {
		thread_mutex_unlock(&dir->ent_mu);
		as_unmap(dir);
		return r;
	    }
	    memset(ino, 0, sizeof(*ino));
	    ino->type = type;
	    as_unmap(ino);

	    strncpy(ent->name, name, FS_NAME_LEN - 1);
	    ent->name[FS_NAME_LEN - 1] = 0;
	    ent->seg = seg;

	    if (o) {
		memset(o, 0, sizeof(*o));
		o->fh_dev_id = 'r';
		o->fh_ramfs.seg = seg;
	    }

	    thread_mutex_unlock(&dir->ent_mu);
	    as_unmap(dir);
	    return 0;
	}
    }

    thread_mutex_unlock(&dir->ent_mu);
    as_unmap(dir);
    return -E_NO_SPACE;
}

static int64_t
ramfs_file_size(struct fs_handle *file)
{
    int64_t r = sys_segment_get_nbytes(file->fh_ramfs.seg);
    if (r < 0)
	return r;
    return r - sizeof(ramfs_inode_t);
}

static int
ramfs_file_create(struct fs_handle *dir, const char *fn, struct fs_handle *o)
{
    return ramfs_inode_alloc(dir, fn, o, file_type);
}

static int64_t
ramfs_file_pread(struct fs_handle *h, void *buf, uint64_t count, uint64_t off)
{
    int64_t r = ramfs_file_size(h);

    if (r < 0)
	return r;
    if ((uint64_t) r < off)
	return 0;

    if (count > r - off)
	count = r - off;

    struct ramfs_file *file = 0;
    r = ramfs_map_file(h->fh_ramfs.seg, SEGMAP_READ | SEGMAP_WRITE, &file);
    if (r < 0)
	return r;

    memcpy(buf, &file->ram[off], count);
    as_unmap(file);
    return count;
}

static int64_t
ramfs_file_truncate(struct fs_handle *h, uint64_t new_size)
{
    struct ramfs_file *file = 0;
    int64_t r =
	ramfs_map_file(h->fh_ramfs.seg, SEGMAP_READ | SEGMAP_WRITE, &file);
    if (r < 0)
	return r;

    thread_mutex_lock(&file->ram_mu);
    int64_t old_size = ramfs_file_size(h);
    if (old_size < 0) {
	thread_mutex_unlock(&file->ram_mu);
	as_unmap(file);
	return old_size;
    }
    r = sys_segment_set_nbytes(h->fh_ramfs.seg,
			       new_size + sizeof(ramfs_inode_t));
    if ((uint64_t) old_size < new_size) {
	memset(&file->ram[old_size], 0, new_size - (uint64_t) old_size);
    }
    thread_mutex_unlock(&file->ram_mu);
    as_unmap(file);
    return r;
}

static int64_t
ramfs_file_pwrite(struct fs_handle *h, const void *buf, uint64_t count,
		  uint64_t off)
{
    struct ramfs_file *file = 0;
    int64_t r =
	ramfs_map_file(h->fh_ramfs.seg, SEGMAP_READ | SEGMAP_WRITE, &file);
    if (r < 0)
	return r;

    int64_t need = count + off;

    thread_mutex_lock(&file->ram_mu);
    r = ramfs_file_size(h);
    if (r < 0) {
	thread_mutex_unlock(&file->ram_mu);
	as_unmap(file);
	return r;
    }
    // Need to grow
    if (r < need) {
	r = sys_segment_set_nbytes(h->fh_ramfs.seg,
				   need + sizeof(ramfs_inode_t));
	thread_mutex_unlock(&file->ram_mu);
	as_unmap(file);

	if (r < 0)
	    return r;

	file = 0;
	r = ramfs_map_file(h->fh_ramfs.seg, SEGMAP_READ | SEGMAP_WRITE,
			   &file);
	if (r < 0)
	    return r;
    } else
	thread_mutex_unlock(&file->ram_mu);

    memcpy(&file->ram[off], buf, count);

    as_unmap(file);
    return count;
}

static int
ramfs_dir_lookup(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    struct ramfs_dir *dir = 0;
    int r = ramfs_map_dir(h->fh_ramfs.seg, SEGMAP_READ | SEGMAP_WRITE, &dir);
    if (r < 0) {
	if (debug)
	    cprintf("fail to map directory %s. not a directory\n", fn);
	return r;
    }
    thread_mutex_lock(&dir->ent_mu);

    for (int i = 0; i < RAMFS_NDENT; i++) {
	struct ramfs_dent *ent = &dir->ent[i];

	if (ent->name[0] != '\0' && !strcmp(ent->name, fn)) {
	    memset(o, 0, sizeof(*o));
	    o->fh_dev_id = 'r';
	    o->fh_ramfs.seg = ent->seg;
	    thread_mutex_unlock(&dir->ent_mu);
	    as_unmap(dir);
	    return 0;
	}
    }

    thread_mutex_unlock(&dir->ent_mu);
    as_unmap(dir);
    return -E_NOT_FOUND;
}

static int
ramfs_dir_mk(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    return ramfs_inode_alloc(h, fn, o, dir_type);
}

static int
ramfs_pfork(struct fs_handle *root, struct sobj_ref *sh)
{
    *sh = SOBJ(root->fh_ramfs.seg.share, root->fh_ramfs.seg.share);
    return 0;
}

static int
ramfs_stat(struct fs_handle *h, struct fs_stat *st)
{
    int64_t r = ramfs_file_size(h);
    if (r < 0)
	return r;
    st->size = r;

    ramfs_inode_t *ino = 0;
    r = as_map(h->fh_ramfs.seg, 0, SEGMAP_READ | SEGMAP_WRITE, (void **) &ino,
	       0);
    if (r < 0)
	return r;
    if (ino->type == dir_type)
	st->mode = S_IFDIR;
    else
	st->mode = 0;
    as_unmap(ino);
    return 0;
}

static int
ramfs_fsync(struct fs_handle *h)
{
    return 0;
}

struct fs_dev ramfs_dev = {
    .file_pread = ramfs_file_pread,
    .file_pwrite = ramfs_file_pwrite,
    .file_create = ramfs_file_create,
    .file_truncate = ramfs_file_truncate,
    .dir_lookup = ramfs_dir_lookup,
    .dir_mk = ramfs_dir_mk,
    .fs_pfork = ramfs_pfork,
    .fs_stat = ramfs_stat,
    .fs_fsync = ramfs_fsync,
};
