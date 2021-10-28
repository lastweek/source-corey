#include <inc/lib.h>
#include <inc/fs.h>
#include <inc/error.h>
#include <inc/assert.h>
#include <inc/array.h>
#include <inc/stdio.h>
#include <fs/dev.h>
#include <fs/mount.h>

#include <string.h>

static int
fs_lookup_path(struct fs_handle *start_dir, const char *pn, struct fs_handle *h,
	       struct fs_mount_table *mtab)
{
    int r;

    if (pn[0] == '/') {
	start_dir = &core_env->rhand;
	pn++;
    }    
    
    struct fs_handle q = *start_dir;
    
    while (pn[0] != '\0') {
	const char *name_end = strchr(pn, '/');
	const char *next_pn = name_end ? name_end + 1 : "";
	size_t namelen = name_end ? (size_t) (name_end - pn) : strlen(pn);

	char fn[FS_NAME_LEN];
	if (namelen >= sizeof(fn))
	    return -E_NO_SPACE;

	strncpy(&fn[0], pn, namelen + 1);
	fn[namelen] = '\0';

	if (fn[0] != '\0') {
	    // check the mount table first
	    if (mtab) {
		for (int i = 0; i < FS_NMOUNT; i++) {
		    struct fs_mtab_ent *mnt = &mtab->mtab_ent[i];		    
		    if (mnt->mnt_dir.fh_ref.object == q.fh_ref.object &&
			!strcmp(&mnt->mnt_name[0], fn))
		    {
			q = mnt->mnt_root;
			goto resolved;
		    }
		}
	    }
	    
	    struct fs_handle q2;
	    r = fs_dev_get(&q)->dir_lookup(&q, fn, &q2);
	    if (r < 0)
		return r;
	    q = q2;
	}
	
    resolved:
	pn = next_pn;
    }

    *h = q;
    return 0;
}

int
fs_namei(const char *pn, struct fs_handle *o)
{
    static struct fs_mount_table *mtab;
    static struct sobj_ref mtab_ref;
    if (mtab_ref.share != core_env->mtab.share || 
	mtab_ref.object != core_env->mtab.object)
    {
	if (mtab)
	    as_unmap(mtab);
	mtab = 0;
    }

    if (!mtab) {
	int r = as_map(core_env->mtab, 0, SEGMAP_READ, (void **)&mtab, 0);
	if (r < 0)
	    return r;
	mtab_ref = core_env->mtab;
    }

    return fs_lookup_path(&core_env->cwd, pn, o, mtab);
}
