#include <kern/atmap.h>
#include <kern/kobj.h>
#include <kern/lib.h>
#include <kern/at.h>

struct address_map_list *
aml_get_list(struct kobject *ko)
{
    if (ko->hdr.ko_type == kobj_segment)
	return &((struct Segment *)ko)->sg_map_list;
    else if (ko->hdr.ko_type == kobj_address_tree)
	return &((struct Address_tree *)ko)->at_map_list;

    panic("unknown type: %d", ko->hdr.ko_type);
}

void
aml_init(struct address_map_list *aml)
{
    LIST_INIT(&aml->list);
    spin_init(&aml->lock);
}

void 
aml_insert(struct address_map_list *aml, struct address_mapping2 *am)
{
    spin_lock(&aml->lock);
    LIST_INSERT_HEAD(&aml->list, am, am_link);
    spin_unlock(&aml->lock);
}

void 
aml_remove(struct address_map_list *aml, struct address_mapping2 *am)
{
    spin_lock(&aml->lock);
    LIST_REMOVE(am, am_link);
    spin_unlock(&aml->lock);
}

void
aml_foreach(struct address_map_list *aml, void (*cb)(struct address_mapping2 *))
{
    struct address_mapping2 *am;
    spin_lock(&aml->lock);
    LIST_FOREACH(am, &aml->list, am_link)
	cb(am);
    spin_unlock(&aml->lock);
}

void 
aml_invalidate(struct address_map_list *aml, kobject_id_t sh)
{
    spin_lock(&aml->lock);

    struct address_mapping2 *am = LIST_FIRST(&aml->list);
    struct address_mapping2 *prev = 0;
    
    while (am != 0) {
	assert(am->am_kobj);
	if (sh == 0 || sh == am->am_sh) {
	    LIST_REMOVE(am, am_link);
	    at_invalidate_addrmap(am->am_parent, am);
	    am = prev ? LIST_NEXT(prev, am_link) : LIST_FIRST(&aml->list);
	} else {
	    prev = am;
	    am = LIST_NEXT(am, am_link);
	}
    }
    spin_unlock(&aml->lock);
}

void
aml_scope(struct address_map_list *aml, kobject_id_t sh,
	  struct Processor *scope_ps)
{
    spin_lock(&aml->lock);
	
    struct address_mapping2 *am = LIST_FIRST(&aml->list);
    struct address_mapping2 *prev = 0;
	
    while (am != 0) {
	assert(am->am_kobj);
	if (sh == am->am_sh) {
	    if (scope_ps == am->am_ps) {
		LIST_REMOVE(am, am_link);
		at_invalidate_addrmap(am->am_parent, am);		
	    } else {
		// Skip it, mapped by a different ps
		prev = am;
	    }
	    
	    am = prev ? LIST_NEXT(prev, am_link)
		: LIST_FIRST(&aml->list);
	} else {
	    prev = am;
	    am = LIST_NEXT(am, am_link);
	}
    }
    spin_unlock(&aml->lock);
}
