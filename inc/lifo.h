#ifndef JOS_INC_LIFO_H
#define JOS_INC_LIFO_H

/*
 * This file defines a thread-safe, lock-free LIFO stack.  It relies on
 * architecture support for compare-and-swap of double-words (ie the x86
 * cmpxchg8b and cmpxchg16b).  Its implementation was inspired by "Lock-Free
 * Techniques for Concurrent Access to Shared Objects" by Dominique Fober:
 *
 * http://www.grame.fr/pub/fober-JIM2002.pdf
 */

/*
 * Architecture specific compare-and-swap functions.
 */

#include <machine/x86.h>

#ifdef JOS_ARCH_amd64

#define LIFO_CAS(mem, old, new)						\
	cmpxchg((uint64_t *)mem, (uint64_t)(old), (uint64_t)(new))

#define LIFO_CAS2(mem, old0, old1, new0, new1)				\
	cmpxchg16b((uint64_t *)(mem), (uint64_t)(old0),			\
		   (uint64_t)(old1), (uint64_t)(new0),			\
		   (uint64_t)(new1))

#define LIFO_PAUSE() nop_pause()

#else
#error unknown architecture
#endif

#define	LIFO_HEAD(name, type)						\
struct name {								\
	struct type *lfh_first;	/* first element */			\
	uint64_t lfh_count;	/* pop count */				\
} __attribute__((__aligned__(16)))

#define	LIFO_HEAD_INITIALIZER(head)					\
	{ NULL, 0 }

#define	LIFO_ENTRY(type)						\
struct {								\
	 struct type *lfe_next;	/* next element */			\
}

/*
 * LIFO functions.
 */

#define	LIFO_EMPTY(head)	((head)->lfh_first == NULL)

#define	LIFO_INIT(head) do {						\
	((head)->lfh_first) = NULL;					\
	(head)->lfh_count = 0;						\
} while (0)

#define	LIFO_PUSH(head, elm, field) do {				\
	while(1) {							\
	    (elm)->field.lfe_next = (head)->lfh_first;			\
	    if (LIFO_CAS(&(head)->lfh_first,				\
			 (elm)->field.lfe_next, elm))			\
			 break;						\
	    LIFO_PAUSE();						\
	}								\
} while (0)

#define	LIFO_POP(head, field) ({					\
	__typeof__((head)->lfh_first) __LF;				\
	while (1) {							\
	    __LF = (head)->lfh_first;					\
	    uint64_t __LC = (head)->lfh_count;				\
	    if (__LF == NULL)						\
		break;							\
	    if (LIFO_CAS2((head), __LF, __LC,				\
			  __LF->field.lfe_next, __LC + 1))		\
		break;							\
	}								\
	__LF;								\
})

#endif
