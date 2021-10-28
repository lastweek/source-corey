#include <kern/lib.h>
#include <kern/share.h>
#include <kern/kobj.h>
#include <kern/lockmacro.h>
#include <kern/arch.h>
#include <inc/error.h>
#include <inc/pad.h>

LIST_HEAD(share_action_list, share_action);

static PAD_TYPE(struct share_action_list, JOS_CLINE) sh_ci_list[JOS_NCPU];
static PAD_TYPE(struct share_action_list, JOS_CLINE) sh_rm_list[JOS_NCPU];

static const kobject_scope_cb scope_cb[kobj_ntypes] = {
    [kobj_segment]	   = segment_scope_cb,
    [kobj_address_tree]	   = at_scope_cb,
    [kobj_processor]	   = processor_scope_cb,
    [kobj_share]	   = share_scope_cb,
    [kobj_device]	   = device_scope_cb,
};

static const kobject_remove_cb remove_cb[kobj_ntypes] = {
    [kobj_segment]	   = segment_remove_cb,
    [kobj_address_tree]	   = at_remove_cb,
    [kobj_processor]	   = processor_remove_cb,
    [kobj_share]	   = share_remove_cb,
    [kobj_device]	   = device_remove_cb,
};

int
share_alloc(struct Share **shp, int kobj_mask, proc_id_t pid)
{
    struct kobject *ko;
    int r = kobject_alloc(kobj_share, &ko, pid);
    if (r < 0)
        return r;

    struct Share *s = &ko->sh;
    map_init(&s->sh_kobject_map, pid);
    for (uint32_t i = 0; i < array_size(s->sh_blob); i++)
	LIST_INIT(&s->sh_blob[i].free);

    sharemu_init(&s->sh_kobject_lock);
    for (uint32_t i = 0; i < array_size(s->sh_blob); i++)
	pagetree_init(&s->sh_blob[i].pt, 0, pid);
    s->sh_mask = kobj_mask;

    *shp = s;
    return 0;
}

static int __attribute__((warn_unused_result))
share_blob_alloc(struct Share *sh, struct share_blob **sb)
{
    int r = 0;
    struct share_blob *b = LIST_FIRST(&sh->sh_blob[arch_cpu()].free);
    if (!b) {
	struct share_blob *blobs;
	r = page_alloc((void **)&blobs, sh->sh_ko.ko_pid);
	if (r < 0)
	    goto done;
	r = pagetree_put_page(&sh->sh_blob[arch_cpu()].pt, 
			      sh->sh_blob[arch_cpu()].npages, 
			      blobs);
	if (r < 0) {
	    page_free(blobs);
	    goto done;
	}
	page_zero(blobs);
	sh->sh_blob[arch_cpu()].npages++;	

	for (uint32_t i = 0; i < N_SHAREBLOB_PER_PAGE; i++)
	    LIST_INSERT_HEAD(&sh->sh_blob[arch_cpu()].free, &blobs[i], link);
	b = LIST_FIRST(&sh->sh_blob[arch_cpu()].free);
    }
    LIST_REMOVE(b, link);
    *sb = b;

 done:
    return r;
}

static void
share_blob_free(struct Share *sh, struct share_blob *sb)
{
    LIST_INSERT_HEAD(&sh->sh_blob[arch_cpu()].free, sb, link);
}

static void
share_remove_kobj(struct Share *sh, struct kobject *ko)
{
    struct kobject_hdr *kp = &ko->hdr;
    if (kp->ko_type >= array_size(scope_cb) || !scope_cb[kp->ko_type])
	panic("unknown kobject type %d", kp->ko_type);
    
    remove_cb[kp->ko_type](ko, sh->sh_ko.ko_id);
}

static void
share_action_ci(struct Share *sh, struct share_info *si)
{
    assert(jos_atomic_read(&si->si_co));
    jos_atomic_dec64(&si->si_co);
}

static void
share_action_dec(struct Share *sh, struct share_info *si)
{
    // XXX if someone still has the the object co'ed we should just
    // skip it and get it next time?
    while (jos_atomic_read(&si->si_co)) ;
    
    kobject_decref(&si->si_ko->hdr);    
    share_blob_free(sh, (struct share_blob *)si);
}

void
share_action_scan(void)
{
    struct share_action_list *list[2];
    list[0] = &sh_ci_list[arch_cpu()].val;
    list[1] = &sh_rm_list[arch_cpu()].val;;

    for (uint32_t i = 0; i < array_size(list); i++) {
	while (1) {
	    struct share_action *sa = LIST_FIRST(list[i]);
	    if (!sa)
		break;

	    struct Share *sh = sa->sa_sh;
	    struct share_info *si = sa->sa_si;
	    sa->sa_action(sh, si);

	    LIST_REMOVE(sa, sa_link);
	    share_blob_free(sh, (struct share_blob *)sa);
	}
    }
}

int
share_import_obj(struct Share *sh, struct kobject *ko)
{
    if (((1 << ko->hdr.ko_type) & sh->sh_mask) == 0)
	return -E_INVAL;
    
    struct share_blob *sb0, *sb1;
    int r = share_blob_alloc(sh, &sb0);
    if (r < 0)
	return r;
    sb0->si.si_ko = ko;
    jos_atomic_set64(&sb0->si.si_co, 0);
    jos_atomic_set(&sb0->si.si_cnt, 1);

    sharemu_write_lock(&sh->sh_kobject_lock);
    
    r = map_get(&sh->sh_kobject_map, ko->hdr.ko_id, (uintptr_t *)&sb1);    
    if (r == 0) {
	jos_atomic_inc(&sb1->si.si_cnt);
	sharemu_write_unlock(&sh->sh_kobject_lock);
	share_blob_free(sh, sb0);	
	return 0;
    }

    r = map_put(&sh->sh_kobject_map, ko->hdr.ko_id, (uintptr_t)sb0);
    sharemu_write_unlock(&sh->sh_kobject_lock);
    if (r < 0) {
	share_blob_free(sh, sb0);
	return r;
    }

    kobject_incref(&ko->hdr);
    return 0;
}

int
share_co_obj(struct Share *sh, kobject_id_t id, struct kobject **ko, 
	     uint8_t type)
{
    struct share_blob *sb0;
    sharemu_read_lock(&sh->sh_kobject_lock);
    int r = map_get(&sh->sh_kobject_map, id, (uintptr_t *)&sb0);
    if (r < 0)
	goto done;

    if (sb0->si.si_ko->hdr.ko_type != type && type != kobj_any) {
	r = -E_INVAL;
	goto done;
    }

    struct share_blob *sb1;
    r = share_blob_alloc(sh, &sb1);
    if (r < 0)
	goto done;

    jos_atomic_inc64(&sb0->si.si_co);
    *ko = sb0->si.si_ko;

    sb1->sa.sa_si = &sb0->si;
    sb1->sa.sa_sh = sh;
    sb1->sa.sa_action = &share_action_ci;
    LIST_INSERT_HEAD(&sh_ci_list[arch_cpu()].val, &sb1->sa, sa_link);

 done:
    sharemu_read_unlock(&sh->sh_kobject_lock);
    return r;
}

int 
share_remove_obj(struct Share *sh, kobject_id_t id)
{
    struct share_blob *sb0;
    sharemu_write_lock(&sh->sh_kobject_lock);
    int r = map_get(&sh->sh_kobject_map, id, (uintptr_t *)&sb0);
    if (r < 0)
	goto done;

    if (!jos_atomic_dec_and_test(&sb0->si.si_cnt))
	goto done;

    struct share_blob *sb1;
    r = share_blob_alloc(sh, &sb1);
    if (r < 0)
	goto done;

    assert(map_erase(&sh->sh_kobject_map, id) == 0);
    
    sb1->sa.sa_sh = sh;
    sb1->sa.sa_si = &sb0->si;
    sb1->sa.sa_action = &share_action_dec;
    LIST_INSERT_HEAD(&sh_rm_list[arch_cpu()].val, &sb1->sa, sa_link);

 done:
    sharemu_write_unlock(&sh->sh_kobject_lock);
    return r;
}

void
share_gc_cb(struct Share *sh)
{
    struct Map_iter iter;
    map_iter_init(&iter, &sh->sh_kobject_map);
    struct share_blob *sb;
    while (map_iter_next(&iter, 0, (uintptr_t *)&sb)) {
	assert(!jos_atomic_read(&sb->si.si_co));
	share_remove_kobj(sh, sb->si.si_ko);
        kobject_decref(&sb->si.si_ko->hdr);
    }
    map_free(&sh->sh_kobject_map);
    for (uint32_t i = 0; i < array_size(sh->sh_blob); i++)    
	pagetree_free(&sh->sh_blob[i].pt);
}

void
share_print(struct Share *sh)
{
    struct Map_iter iter;
    map_iter_init(&iter, &sh->sh_kobject_map);
    struct share_blob *sb;
    while (map_iter_next(&iter, 0, (uintptr_t *)&sb))
	cprintf(" %ld.%ld  %s\n", 
		sh->sh_ko.ko_id, sb->si.si_ko->hdr.ko_id,
		sb->si.si_ko->hdr.ko_name);
}

void
share_scope_change(struct Share *sh, struct Processor *scope_ps)
{
    sharemu_read_lock(&sh->sh_kobject_lock);
    struct Map_iter iter;
    map_iter_init(&iter, &sh->sh_kobject_map);
    struct share_blob *sb;
    while (map_iter_next(&iter, 0, (uintptr_t *)&sb)) {
	struct kobject *ko = sb->si.si_ko;
	struct kobject_hdr *kp = &ko->hdr;
	if (kp->ko_type >= array_size(scope_cb) || !scope_cb[kp->ko_type])
	    panic("unknown kobject type %d", kp->ko_type);
    
	scope_cb[kp->ko_type](ko, sh->sh_ko.ko_id, scope_ps);
    }
    sharemu_read_unlock(&sh->sh_kobject_lock);
}

void
share_remove_cb(struct Share *sh, kobject_id_t id)
{
}

void
share_scope_cb(struct Share *sh, kobject_id_t parent_sh, 
	       struct Processor *scope_ps)
{
}
