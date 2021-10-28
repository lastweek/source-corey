#ifndef JOS_INC_TYPES_H
#define JOS_INC_TYPES_H

#if defined(JOS_KERNEL)
#include <machine/types.h>

#elif defined(JOS_USER)
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>

#elif !defined(JOS_GUEST) 
#include <machine/types.h>
#endif

#endif
