#include <inc/lib.h>
#include <inc/fs.h>
#include <inc/error.h>
#include <inc/stdio.h>
#include <fs/mount.h>
#include <fs/dev.h>

#include <string.h>
#include <stdio.h>

int
fs_mount(struct sobj_ref mount_seg, struct fs_handle *dir, 
	 const char *mnt_name, struct fs_handle *root)
{
    struct fs_mount_table *mtab = 0;
    int r = as_map(mount_seg, 0, SEGMAP_READ | SEGMAP_WRITE, (void **)&mtab, 0);
    if (r < 0)
	return r;

    for (int i = 0; i < FS_NMOUNT; i++) {
	struct fs_mtab_ent *ent = &mtab->mtab_ent[i];
	if (ent->mnt_name[0] == '\0') {
	    strncpy(&ent->mnt_name[0], mnt_name, FS_NAME_LEN - 1);
	    ent->mnt_name[FS_NAME_LEN - 1] = 0;
	    if (dir)
		ent->mnt_dir = *dir;
	    if (root)
		ent->mnt_root = *root;

	    as_unmap(mtab);
	    return 0;
	}
    }
    
    as_unmap(mtab);
    return -E_NO_SPACE;
}

void
fs_mount_print(struct sobj_ref mount_seg)
{
    struct fs_mount_table *mtab = 0;
    int r = as_map(mount_seg, 0, SEGMAP_READ | SEGMAP_WRITE, (void **)&mtab, 0);
    if (r < 0) {
	printf("fs_mount_print: as_map error: %s\n", e2s(r));
	return;
    }

    for (int i = 0; i < FS_NMOUNT; i++) {
	struct fs_mtab_ent *ent = &mtab->mtab_ent[i];
	if (ent->mnt_name[0] != '\0') {
	    printf("%s\n", ent->mnt_name);
	}
    }
    
    as_unmap(mtab);
}

int 
fs_mount_pfork(struct sobj_ref mount_seg, struct sobj_ref *sh, int size)
{
    struct fs_mount_table *mtab = 0;
    int r = as_map(mount_seg, 0, SEGMAP_READ | SEGMAP_WRITE, (void **)&mtab, 0);
    if (r < 0)
	return r;

    int count = 0;
    for (int i = 0; i < FS_NMOUNT; i++) {
	struct fs_mtab_ent *ent = &mtab->mtab_ent[i];
	if (ent->mnt_name[0] != '\0') {
	    if (count >= size) {
		as_unmap(mtab);
		return -E_NO_SPACE;
	    }	    

	    if (fs_dev_get(&ent->mnt_root)->fs_pfork) {
		r = fs_dev_get(&ent->mnt_root)->fs_pfork(&ent->mnt_root, &sh[count]);
		if (r < 0) {
		    as_unmap(mtab);		
		    return r;
		}
		count++;
	    }
	}
    }
    
    as_unmap(mtab);
    return count;
}
