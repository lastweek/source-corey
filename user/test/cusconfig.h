#ifndef JOS_USER_TEST_CUSCONFIG_H
#define JOS_USER_TEST_CUSCONFIG_H

#include <test.h>

enum { run_default_tests = 1 };
enum { run_custom_tests = 0 };

static struct {
    char name[32];
    void (*func)(void);
} custom_tests[] = {
    { "fd_test", fd_test },
    { "elf_test", elf_test },
    { "memcloneat_test", memcloneat_test },
    { "console", console_test },
    { "pfork", pfork_test },
    { "pf", pf_test },
    { "sock", lwip_sock_test },
    { "net", net_test },
    { "bcache", bcache_test },
    { "pfork", pfork_test },
    { "kdebug", kdebug_test },
    { "stress_test", fs_stress_test },
    { "interior", interior_test},
    { "memclone", memclone_test },
    { "lock", lock_test },
    { "monitor", monitor_test },
#if 1
    { "thread", thread_test },
    { "console", console_test },
    { "cache", cache_test },
    { "share", share_test },
    { "processor", processor_test },
    { "segment", segment_test },
    { "as" , as_test }, 
    { "pfork", pfork_test},
    { "cpp", cpp_test },
    { "disk", disk_test },
#endif
    { "interior", interior_test},
};

#endif
