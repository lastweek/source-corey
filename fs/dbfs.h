#ifndef JOS_FS_DBFS_H
#define JOS_FS_DBFS_H

struct fs_handle;

struct dbfs_handle
{
    uint64_t inode;
};

int dbfs_dump(void);
int dbfs_init(struct fs_handle *h, int reset);

#endif
