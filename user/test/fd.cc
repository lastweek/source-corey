extern "C" {
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/jnic.h>
#include <test.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <lwipinc/lwipinit.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <machine/x86.h>
#include <string.h>
#include <stdio.h>
#include <inc/fd.h>
}

#include <inc/error.hh>
#include <inc/errno.hh>

struct jnic the_nic;
uint64_t lwip_ready;

enum { max_file_count = 15 };
enum { stress_file_size = 128 * 1024 };
enum { iterations = 10 };

static void
find_nic(void)
{
    struct u_device_list udl;
    echeck(sys_device_list(&udl));

    for (uint64_t i = 0; i < udl.ndev ; i++) {
        if (udl.dev[i].type == device_nic) {
            int64_t dv;
            dv = sys_device_alloc(core_env->sh, udl.dev[i].id, core_env->pid);
            if (dv < 0)
                panic("cannot allocate device: %s", e2s(dv));

            struct u_device_conf udc;
            udc.type = device_conf_irq;
            udc.irq.irq_pid = core_env->pid;
            udc.irq.enable = 1;
            int r = sys_device_conf(SOBJ(core_env->sh, dv), &udc);
            if (r < 0)
                panic("unable to configure nic: %s", e2s(r));

            r = jnic_real_setup(&the_nic, SOBJ(core_env->sh, dv));
            if (r < 0)
                panic("unable to init jnic: %s", e2s(r));
            return;
        }
    }
    panic("no nics detected");
}

static void
lwip_cb(uint32_t ip, void *x)
{
    thread_wakeup(&lwip_ready);
}

static void __attribute__((noreturn))
lwip_thread(uint64_t x)
{
    lib_lwip_init(lwip_cb, 0, &the_nic, 0, 0, 0);
}

static void
sock_fd_test(void)
{
    int r;
    find_nic();
    time_init(100);
    echeck(thread_create(0, "lwip-thread", lwip_thread, 0));
    thread_wait(&lwip_ready, 0, UINT64(~0));

    try {
        static const char *request = ""
                                     "GET / HTTP/1.0\r\nUser-Agent: "
                                     "TestClient\r\nHost: pdos.csail.mit.edu:80\r\n"
                                     "\r\n";
        static const char *host = "18.26.4.9";
        static uint16_t host_port = 80;

        int s;
        uint16_t port = htons(host_port);
        uint32_t ip = inet_addr(host);

        struct sockaddr_in sin;
        sin.sin_family = PF_INET;
        sin.sin_port = port;
        sin.sin_addr.s_addr = ip;

        errno_check(s = socket(PF_INET, SOCK_STREAM, 0));
        errno_check(connect(s, (struct sockaddr *)&sin, sizeof(sin)));

        int sz = strlen(request);
        r = write(s, (char *)request, sz);
        assert(r == sz);

        for (;;) {
            char buf[128];
            assert((r = read(s, buf, sizeof(buf) - 1)) >= 0);
            if (r == 0)
                break;
            buf[r] = 0;
            cprintf("%s", buf);
        }
        cprintf("\n\n");

        close(s);

    } catch (basic_exception &e) {
        panic("fd_test failed: %s\n", e.what());
    }
}

static void
fs_fd_test(void)
{
    int fd;
    echeck(open("/x", O_RDONLY));
    echeck(mkdir("/x/test", 0));
    for (uint64_t i = 0; i < max_file_count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "/x/test/foo.%ld", i);
        echeck(fd = creat(buf, 0));
        for (uint64_t k = 0; k < stress_file_size / PGSIZE; k++) {
            char page[PGSIZE];
            memset(page, 'A' + k, sizeof(page));
            ssize_t s;
            echeck(s = write(fd, page, sizeof(page)));
            if (s != sizeof(page))
                panic("fail to write to file %s\n", buf);
        }
        close(fd);
    }
}

static void
cons_fd_test(void)
{
}
static void
fd_prim_test(void)
{
    uint64_t start, time_alloc = 0, time_free = 0;
    struct Fd *fd[iterations];
    for (int i = 0; i < iterations; i++) {
	start = read_tsc();
        echeck(fd_alloc(&fd[i], "socket fd"));
	time_alloc += read_tsc() - start;
    }
    for (int i = 0; i < iterations; i++) {
	start = read_tsc();
        echeck(fd_free(fd[i]));
	time_free += read_tsc() - start;
    }
    cprintf("cycles per operations: fd_alloc, %lu\n", time_alloc / iterations);
    cprintf("cycles per operations: fd_free, %lu\n", time_free / iterations);
}

void
fd_test(void)
{
    fd_prim_test();
    fs_fd_test();
    cons_fd_test();
    sock_fd_test();
}

