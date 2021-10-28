#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <sys/types.h>
#include <inc/lib.h>

libc_hidden_proto(getuid)
libc_hidden_proto(geteuid)
libc_hidden_proto(getgid)
libc_hidden_proto(getegid)

uid_t
getuid(void)
{
    return 0;
}

gid_t
getgid(void)
{
    return 0;
}

uid_t
geteuid(void)
{
    return 0;
}

gid_t
getegid(void)
{
    return 0;
}


libc_hidden_def(getuid)
libc_hidden_def(geteuid)
libc_hidden_def(getgid)
libc_hidden_def(getegid)
