#include <machine/memlayout.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/segment.h>
#include <inc/assert.h>
#include <inc/utrap.h>
#include <inc/utraphand.h>
#include <inc/console.h>
#include <inc/arch.h>
#include <fs/ramfs.h>
#include <fs/vfs0.h>
#include <fs/mount.h>
#include <string.h>
#include <unistd.h>

enum { bcache_disable = 1 };

core_env_t *core_env;
const char *boot_args = (char *)(uintptr_t) UBOOTARGS;

static void __attribute__((noreturn))
libmain_cb(uint64_t arg)
{
    int (*mainf) (int argc, const char **argv) = (void *)arg;
    mainf(0, 0);
    thread_halt();
    panic("libmain_cb: processor still running");
}

static void
fs_init(void)
{
    // allocate a mount table
    echeck(segment_alloc(core_env->sh, sizeof(struct fs_mount_table), 
			 &core_env->mtab, 0, 
			 0, "mtab", core_env->pid));

    struct fs_handle ram_fh;
    echeck(ramfs_init(&ram_fh));
    echeck(vfs0_init(&ram_fh, &core_env->rhand));

    if (bcache_disable)
	core_env->rhand = ram_fh;

    echeck(fs_mount(core_env->mtab, 0, "/", &core_env->rhand));
    echeck(fs_mkdir(&core_env->rhand, "x", 0));
    core_env->cwd = core_env->rhand;

    // Mounting ram_foo at /user
    // struct fs_handle ram_foo;
    // echeck(ramfs_init(&ram_foo));
    // echeck(fs_mount(default_mtab, &default_rhand, "user", &ram_foo));
}

static volatile uint64_t timer_ticks;

static void
tick_handler(struct UTrapframe *utf)
{
    timer_ticks++;
}

static uint64_t
cpufreq_estimate(void)
{
    utrap_set_handler(tick_handler);
    uint64_t s = arch_read_tsc();
    echeck(sys_processor_set_interval(processor_current(), 100));
    while (timer_ticks < 10) ;
    uint64_t e = arch_read_tsc();
    sys_processor_set_interval(processor_current(), 0);
    utrap_set_handler(0);
    return (e - s) * 10;
}


void __attribute__((noinline))
setup_env(uint64_t sh_id)
{
    static core_env_t core_env_mem;
    core_env = &core_env_mem;

    // Setup core_env
    int64_t r;
    core_env->sh = sh_id;
    echeck(r = sys_processor_current());
    core_env->psref = SOBJ(sh_id, r);
    echeck(r = sys_self_get_pid());
    core_env->pid = r;

    // Init as management code, so things like malloc can work
    as_init();
}

void
libmain(uintptr_t mainfunc)
{   
    // setup stdio
    int cons = opencons();
    if (cons != 0)
	panic("console wierdness: %d\n", cons);
    assert(1 == dup2(0, 1));
    assert(2 == dup2(0, 2));

    utrap_init();
    core_env->cpufreq = cpufreq_estimate();
    utrap_set_handler(utrap_handler);

    fs_init();
 
    int thread_r = thread_init(libmain_cb, mainfunc);
    if (thread_r < 0)
        cprintf("libmain: unable to init threads: %s\n", e2s(thread_r));

    int (*mainf) (int argc, const char **argv) = (void *) mainfunc;
    mainf(0, 0);

    processor_halt();
}
