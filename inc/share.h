#ifndef JOS_INC_SHARE_H
#define JOS_INC_SHARE_H

#include <inc/types.h>

struct sobj_ref {
    uint64_t share;
    uint64_t object;
};

#define SOBJ(share, object) \
	((struct sobj_ref) { (share), (object) } )

#endif
