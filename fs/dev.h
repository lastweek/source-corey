#ifndef JOS_FS_DEV_H
#define JOS_FS_DEV_H

#include <inc/share.h>

struct fs_handle;
struct fs_stat;

struct fs_dev
{
    int64_t (*file_pread)(struct fs_handle *h, void *buf, uint64_t count, uint64_t off);
    int64_t (*file_pwrite)(struct fs_handle *h, const void *buf, uint64_t count, uint64_t off);
    int (*file_create)(struct fs_handle *h, const char *fn, struct fs_handle *o);
    int64_t (*file_truncate)(struct fs_handle *h, uint64_t new_size);

    int (*dir_lookup)(struct fs_handle *h, const char *fn, struct fs_handle *o);
    int (*dir_mk)(struct fs_handle *h, const char *fn, struct fs_handle *o);

    int (*fs_pfork)(struct fs_handle *root, struct sobj_ref *sh);
    int (*fs_stat)(struct fs_handle *h, struct fs_stat *stat);
    int (*fs_fsync)(struct fs_handle *h);
};

extern struct fs_dev ramfs_dev;  // 'r'
extern struct fs_dev vfs0_dev;   // '0'
extern struct fs_dev dbfs_dev;   // 'd'

struct fs_dev * fs_dev_get(struct fs_handle *dir);

#endif
