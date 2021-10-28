#include <test.h>
#include <machine/x86.h>

#include <string.h>

#if 0
static void
memcpy_test(void)
{
    enum { len = 4096 };
    enum { copy_times = 1000 };

    uint64_t start, end, span;
    static char g_dst[len];
    static char g_src[len];
    int i;

    // pretouch
    memcpy(g_dst, g_src, len);
    start = read_tsc();
    for (i = 0; i < copy_times; i++)
	memcpy(g_dst, g_src, len);
    end = read_tsc();
    span = end - start;
    cprintf("memcpy_test: %lu cycles\n", span);
}
#endif

static void
strlen_test(void)
{
    enum { len = 4096 };
    enum { test_times = 1000 };
    static char str[len];
    uint64_t start, end, span;
    int size_sum = 0;
    int i;
    for (i = 0; i < len; i++)
	str[i] = 1;
    str[len - 1] = 0;
    start = read_tsc();
    for (i = 0; i < test_times; i++) {
	size_sum += strlen(str);
    }
    end = read_tsc();
    span = end - start;
    cprintf("strlen_test: %lu cycles\n", span);
    return;
}

static void
memset_test()
{
    enum { len = 4096 };
    enum { test_times = 1000 };
    static char str[len];
    uint64_t start, end, span;
    void *ret;
    int i;
    //pretouch
    for (i = 0; i < len; i++)
	str[i] = 0;
    start = read_tsc();
    for (i = 0; i < test_times; i++) {
	ret = memset(str, 0, len);
    }
    end = read_tsc();
    span = end - start;
    cprintf("memset_test: %lu cycles\n", span);
    return;
}

static void
strcpy_test()
{
    enum { len = 4096 };
    enum { copy_times = 1000 };

    uint64_t start, end, span;
    static char g_dst[len];
    static char g_src[len];
    int i;

    //pretouch
    for (i = 0; i < len; i++)
	g_src[i] = 1;
    g_src[len - 1] = 0;
    start = read_tsc();
    for (i = 0; i < copy_times; i++)
	strcpy(g_dst, g_src);
    end = read_tsc();
    span = end - start;
    cprintf("strcpy_test: %lu cycles\n", span);
    return;
}

static void
strcmp_test()
{
    enum { len = 4096 };
    enum { test_times = 1000 };

    uint64_t start, end, span;
    static char g_dst[len];
    static char g_src[len];
    int i;
    int sum = 0;

    //pretouch
    for (i = 0; i < len; i++)
	g_src[i] = 1;
    g_src[len - 1] = 0;

    for (i = 0; i < len; i++)
	g_dst[i] = 1;
    g_dst[len - 1] = 0;
    start = read_tsc();
    for (i = 0; i < test_times; i++)
	sum += strcmp(g_dst, g_src);
    end = read_tsc();
    span = end - start;
    cprintf("strcmp_test: %lu cycles\n", span);
    return;
}



void
string_test(void)
{

    strlen_test();
    memset_test();
    strcpy_test();
    //memcpy_test();
    strcmp_test();
}
