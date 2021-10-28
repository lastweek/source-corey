#ifndef JOS_USER_TEST_TEST_H
#define JOS_USER_TEST_TEST_H

#include <inc/assert.h>

void share_test(void);
void as_test(void);
void segment_test(void);
void processor_test(void);
void interior_test(void);
void elf_test(void);

void thread_test(void);
void cpp_test(void);
void kdebug_test(void);
void fs_test(void);
void disk_test(void);
void console_test(void);
void pfork_test(void);
void string_test(void);
void fp_test(void);
void monitor_test(void);

void cache_test(void);
void lwip_sock_test(void);
void fd_test(void);
void fs_fd_perf_test(void);
void sock_fd_perf_test(void);
void datatree_test(void);
void lock_test(void);
void dbfs_test(void);
void fs_stress_test(void);

void efsl_test(void);
void pf_test(void);

void reinit_test(void);

void sysprof_test(void);

void malloc_test(void);

void realloc_test(void);

void memcpy_test(void);

// OSDI benchmarks
void memclone_test(void);
void memcloneat_test(void);
void bcache_test(void);
void net_test(void);
void share_perf_test(void);
void mempass_test(void);

#endif
