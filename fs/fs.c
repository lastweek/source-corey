#include <fs/dev.h>
#include <inc/fs.h>
#include <inc/array.h>
#include <inc/assert.h>

static struct fs_dev *devlist[] = {
    ['r'] = &ramfs_dev,
    ['0'] = &vfs0_dev,
    ['d'] = &dbfs_dev,
};

struct fs_dev *
fs_dev_get(struct fs_handle *dir)
{
    if ((uint8_t)dir->fh_dev_id >= array_size(devlist) || 
	!devlist[dir->fh_dev_id]) 
    {
	cprintf("fs_dev_get: bad dev_id %u\n", dir->fh_dev_id);
	return 0;
    }

    return devlist[dir->fh_dev_id];
}

int
fs_stat(struct fs_handle *h, struct fs_stat *stat)
{
    return fs_dev_get(h)->fs_stat(h, stat);    
}

int
fs_mkdir(struct fs_handle *root, const char *name, struct fs_handle *o)
{
    return fs_dev_get(root)->dir_mk(root, name, o);
}

int
fs_dlookup(struct fs_handle *d, const char *name, struct fs_handle *o)
{
    return fs_dev_get(d)->dir_lookup(d, name, o);
}

int
fs_create(struct fs_handle *dir, const char *name, struct fs_handle *f)
{
    return fs_dev_get(dir)->file_create(dir, name, f);
}

int
fs_fsync(struct fs_handle *file)
{
    return fs_dev_get(file)->fs_fsync(file);
}

int64_t
fs_ftruncate(struct fs_handle *file, uint64_t new_size)
{
    return fs_dev_get(file)->file_truncate(file, new_size);
}

int64_t
fs_pwrite(struct fs_handle *file, const void *buf, uint64_t count, uint64_t off)
{
    return fs_dev_get(file)->file_pwrite(file, buf, count, off);
}

int64_t
fs_pread(struct fs_handle *file, void *buf, uint64_t count, uint64_t off)
{
    return fs_dev_get(file)->file_pread(file, buf, count, off);
}
