#ifndef JOS_USER_TEST_DEFCONFIG_H
#define JOS_USER_TEST_DEFCONFIG_H

#include <test.h>

static struct {
    char name[32];
    void (*func)(void);
} tests[] = {
    { "share", share_test },
    { "processor", processor_test },
    { "segment", segment_test },
    { "as" , as_test }, 
    { "thread", thread_test },
    { "cpp", cpp_test },
    { "console", console_test },
};

#endif
