#include <fs/datatree.h>
#include <inc/error.h>
#include <inc/assert.h>
#include <inc/arch.h>

#include <string.h>

static int
datatree_get_ent(struct datatree_entry *root, int level, uint64_t ient, 
		 struct datatree_entry **ent, 
		 data_block_alloc_fn dalloc, void *arg)
{
    if (!root->ent) {
	spin_lock(&root->lock);
	// root->ent could have been filed while waiting for the lock
	if (!root->ent) {
	    void *va;
	    va = dalloc(arg);
	    if (!va) {
		spin_unlock(&root->lock);
		return -E_NO_MEM;
	    }
	    memset(va, 0, DATA_BLK_BYTES);
	    root->ent = va;
	}
	spin_unlock(&root->lock);
    }

    if (level == 0) {
	assert(ient < DATA_ENTRIES_PER_BLOCK);
	*ent = &root->ent[ient];
	return 0;
    }

    uint64_t nbranch = DATA_ENTRIES_PER_BLOCK;
    for (int i = 1; i < level; i++)
	nbranch *= DATA_ENTRIES_PER_BLOCK;

    for (uint32_t i = 0; i < DATA_ENTRIES_PER_BLOCK; i++) {
	if (ient < nbranch)
	    return datatree_get_ent(&root->ent[i], level - 1, ient, ent, 
				    dalloc, arg);
	ient -= nbranch;
    }

    panic("overflowed, %lu leftover", ient);
}

int
datatree_put(struct datatree *dt, uint64_t ient, void *va)
{
    struct datatree_entry *ent;
    int r = datatree_get_ent(&dt->root, DATATREE_HEIGHT, ient, &ent, 
			     dt->alloc, dt->arg);
    if (r < 0)
	return r;

    uint64_t old = jos_atomic_compare_exchange64(&ent->atomic_va, 0,
						 (uint64_t)(uintptr_t)va);
    if (old != 0)
	return -E_EXISTS;
    
    return 0;
}

int
datatree_overwrite(struct datatree *dt, uint64_t ient, 
		   struct datatree_overwrite_arg *arg, void *newva)
{
    if (!arg->ent) {
	int r = datatree_get_ent(&dt->root, DATATREE_HEIGHT, ient, &arg->ent, 
				 dt->alloc, dt->arg);
	if (r < 0)
	    return r;
    }

    if (!arg->ent->va)
	return -E_INVAL;
    
    uint64_t old;
 loop:    
    old = jos_atomic_compare_exchange64(&arg->ent->atomic_va, 
					(uint64_t)(uintptr_t)arg->oldva, 
					(uint64_t)(uintptr_t)newva);
    if (old != (uint64_t)(uintptr_t)arg->oldva) {
	arg->oldva = (void *)(uintptr_t)old;
	if (arg->flags & DATATREE_OVERWRITE_FORCE) {
	    arch_pause();
	    goto loop;
	}
	return -E_AGAIN;
    }

    dt->free(dt->arg, arg->oldva);
    return 0;
}

int
datatree_get(struct datatree *dt, uint64_t ient, void **va)
{
    struct datatree_entry *ent;
    int r = datatree_get_ent(&dt->root, DATATREE_HEIGHT, ient, &ent, 
			     dt->alloc, dt->arg);
    if (r < 0)
	return r;

    if (!ent->va)
	return -E_NOT_FOUND;
    *va = ent->va;
    return 0;
}

static void
datatree_free_ents(struct datatree_entry *root, int level, 
		   data_block_free_fn dfree, void *arg)
{
    if (!root->ent)
	return;

    if (level == 0) {
	for (uint32_t i = 0; i < DATA_ENTRIES_PER_BLOCK; i++)
	    if (root->ent[i].va)
		dfree(arg, root->ent[i].va);
    } else {
	for (uint32_t i = 0; i < DATA_ENTRIES_PER_BLOCK; i++)
	    datatree_free_ents(&root->ent[i], level - 1, dfree, arg);
    }

    dfree(arg, root->ent);
}

void
datatree_free(struct datatree *dt)
{
    datatree_free_ents(&dt->root, DATATREE_HEIGHT, dt->free, dt->arg);
}

void
datatree_init(struct datatree *dt, 
	      data_block_alloc_fn dalloc, data_block_free_fn dfree, void *arg)
{
    memset(dt, 0, sizeof(*dt));
    dt->alloc = dalloc;
    dt->free = dfree;
    dt->arg = arg;
}
