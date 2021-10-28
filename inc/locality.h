#ifndef JOS_INC_LOCALITY
#define JOS_INC_LOCALITY

#include <machine/param.h>
#include <inc/proc.h>

struct u_locality_matrix {
    uint8_t distance[JOS_NCPU][JOS_NCPU];
    uint32_t ncpu;
};

#endif
