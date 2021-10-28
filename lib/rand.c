#include <inc/lib.h>
#include <inc/arc4.h>
#include <inc/syscall.h>
#include <inc/array.h>

#include <machine/x86.h>

static struct arc4 a4;
static int a4_inited;

void
jrand_init(void)
{
    uint64_t keybuf[4];
    keybuf[0] = processor_current().object;
    keybuf[1] = (uint64_t)&keybuf[0];
    keybuf[2] = read_tsc();
    keybuf[3] = core_env->pid;

    jrand_seed(keybuf, array_size(keybuf));
}

void
jrand_seed(uint64_t *seed, int n)
{
    arc4_reset(&a4);
    arc4_setkey(&a4, seed, n * sizeof(*seed));    
    a4_inited = 1;
}

uint64_t
jrand(void)
{
    if (!a4_inited)
	jrand_init();
    
    uint64_t ret;
    for (uint32_t i = 0; i < sizeof(ret); i++)
        ((char *)&ret)[i] = arc4_getbyte(&a4);
    return ret;
}
