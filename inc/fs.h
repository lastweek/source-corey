#ifndef JOS_INC_FS_H
#define JOS_INC_FS_H

#include <inc/share.h>
#include <fs/ramfs.h>
#include <fs/vfs0.h>
#include <fs/dbfs.h>

#define FS_NAME_LEN	64

// XXX should have an 'id' unique across all handles

struct fs_handle {
    uint8_t fh_dev_id;

    union {
	struct sobj_ref fh_ref;
	struct ramfs_handle fh_ramfs;
	struct vfs0_handle fh_vfs0;
	struct dbfs_handle fh_dbfs;
    };
};

struct fs_stat {
    uint64_t size;
    // tells whether the file is a directory (and maybe other information
    // if neccessary)
    mode_t mode;
};

int fs_namei(const char *pn, struct fs_handle *o);
int64_t fs_pwrite(struct fs_handle *file, const void *buf, uint64_t count,
		  uint64_t off);
int64_t fs_ftruncate(struct fs_handle *file, uint64_t new_size);
int64_t fs_pread(struct fs_handle *file, void *buf, uint64_t count,
		 uint64_t off);
int fs_mkdir(struct fs_handle *root, const char *name, struct fs_handle *o);
int fs_dlookup(struct fs_handle *d, const char *name, struct fs_handle *o);
int fs_create(struct fs_handle *dir, const char *name, struct fs_handle *f);
int fs_stat(struct fs_handle *h, struct fs_stat *stat);
int fs_fsync(struct fs_handle *h);

int fs_mount(struct sobj_ref mount_seg, struct fs_handle *h,
	     const char *mnt_name, struct fs_handle *root);
int fs_mount_pfork(struct sobj_ref mount_seg, struct sobj_ref *sh, int size);
void fs_mount_print(struct sobj_ref mount_seg);
#endif
