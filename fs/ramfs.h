#ifndef JOS_FS_RAMFS_H
#define JOS_FS_RAMFS_H

struct fs_handle;

struct ramfs_handle
{
    struct sobj_ref seg;
};

int ramfs_init(struct fs_handle *h);

#endif
