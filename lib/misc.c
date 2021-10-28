#include <unistd.h>
#include <link.h>

int
dl_iterate_phdr(int (*callback) (struct dl_phdr_info *info,
                                 size_t size, void *data),
                void *data)
{
    return 0;
}
