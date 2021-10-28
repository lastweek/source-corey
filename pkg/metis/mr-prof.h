#ifndef PROFILING_H
#define PROFILING_H

//#define PROF_ENABLED
enum { profile_kern = 0 };

#ifndef PROF_ENABLED

#define enterapp()
#define leaveapp()

#else

#include "bench.h"

#ifdef JOS_USER
#include <inc/compiler.h>
#endif
extern JTLS uint64_t app_tsc_start;
extern JTLS uint64_t app_tsc_tot;

#define enterapp() app_tsc_start = read_tsc();

#define leaveapp() app_tsc_tot += read_tsc() - app_tsc_start;

#endif

#endif
