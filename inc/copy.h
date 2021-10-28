#ifndef JOS_INC_COPY_H
#define JOS_INC_COPY_H

#include <inc/safetype.h>

/*
 * When an object is copied:
 *   cow - both src and dst are COWed
 *   cor - src is COWed dst COWed and CORed
 *   excl - src contents are copied to dst
 */
typedef SAFE_TYPE(int) page_sharing_mode;
#define page_shared_cow SAFE_WRAP(page_sharing_mode, 1)
#define page_shared_cor SAFE_WRAP(page_sharing_mode, 2)
#define page_excl	SAFE_WRAP(page_sharing_mode, 3)

#endif
