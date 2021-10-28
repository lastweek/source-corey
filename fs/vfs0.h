#ifndef JOS_FS_VFS0_H
#define JOS_FS_VFS0_H

#include <fs/ramfs.h>

struct fs_handle;
struct vfs0_state;

struct vfs0_handle
{
    struct sobj_ref wrap; // wrap segment based fs devs
    struct vfs0_state *s; // global state
};

int vfs0_init(struct fs_handle *wrap, struct fs_handle *o);

#endif
