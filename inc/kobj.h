#ifndef JOS_INC_KOBJ_H
#define JOS_INC_KOBJ_H

#include <inc/types.h>

#define JOS_KOBJ_NAME_LEN    32      /* including the terminating NULL */

typedef enum kobject_type_enum {
    kobj_segment,
    kobj_address_tree,
    kobj_processor,
    kobj_share,
    kobj_device,
    kobj_ntypes,
    kobj_any
} kobject_type_t;

typedef uint64_t kobject_id_t;

#endif
