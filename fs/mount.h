#ifndef JOS_FS_MOUNT_H
#define JOS_FS_MOUNT_H

#include <inc/fs.h>

#define FS_NMOUNT 8

struct fs_mtab_ent {
    struct fs_handle mnt_dir;
    char mnt_name[FS_NAME_LEN];
    struct fs_handle mnt_root;
};

struct fs_mount_table {
    struct fs_mtab_ent mtab_ent[FS_NMOUNT];
};

#endif
