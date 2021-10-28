#include <kern/lockmacro.h>
#include <kern/sharemap.h>
#include <kern/kobj.h>
#include <inc/error.h>

void
sharemap_init(struct Sharemap *sm, proc_id_t pid, struct Processor *owner,
	      int share_mask)
{
    sm->owner = owner;
    sm->mask = share_mask;
    map_init(&sm->map, pid);
}

int
sharemap_add(struct Sharemap *sm, struct Share *sh)
{
    if ((sh->sh_mask & sm->mask) != sh->sh_mask)
	return -E_INVAL;

    spin_lock(&sm->lock);
    int r = map_put(&sm->map, sh->sh_ko.ko_id, (uintptr_t)sh);
    spin_unlock(&sm->lock);
    if (r == 0)
	kobject_incref(&sh->sh_ko);

    return r;
}

int
sharemap_remove(struct Sharemap *sm, kobject_id_t id)
{
    struct Share *sh;
    spin_lock((struct spinlock *)&sm->lock);
    int r = map_get(&sm->map, id, (uintptr_t *)&sh);
    if (r == 0)
	assert(map_erase(&sm->map, id) == 0);
    spin_unlock((struct spinlock *)&sm->lock);

    if (r == 0) {
	share_scope_change(sh, sm->owner);
	kobject_decref(&sh->sh_ko);
    }
    return r;
}

void
sharemap_clear(struct Sharemap *sm)
{
    spin_lock(&sm->lock);    
    struct Map_iter iter;
    map_iter_init(&iter, &sm->map);
    struct kobject *ko;
    while (map_iter_next(&iter, 0, (uintptr_t *)&ko)) {
	share_scope_change(&ko->sh, sm->owner);
        kobject_decref(&ko->hdr);
    }

    proc_id_t pid = sm->map.pid;
    map_free(&sm->map);
    map_init(&sm->map, pid);

    spin_unlock(&sm->lock);    
}

int
sharemap_get(struct Sharemap *sm, kobject_id_t id, struct Share **sh)
{
    spin_lock(&sm->lock);
    int r = map_get(&sm->map, id, (uintptr_t *)sh);
    spin_unlock(&sm->lock);
    return r;
}

void
sharemap_free(struct Sharemap *sm)
{
    map_free(&sm->map);    
}

int
sharemap_co_obj(struct Sharemap *sm, struct sobj_ref oref, 
		struct kobject **ko, uint8_t type)
{
    struct Share *sh;
    int r = sharemap_get(sm, oref.share, &sh);
    if (r < 0)
	return r;

    if (oref.share == oref.object) {
	*ko = (struct kobject *)sh;
	return 0;
    }
    
    return share_co_obj(sh, oref.object, ko, type);
}
