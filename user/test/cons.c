#include <machine/mmu.h>
#include <inc/syscall.h>
#include <inc/device.h>
#include <inc/lib.h>
#include <inc/array.h>
#include <test.h>

#include <cusconfig.h>

#include <string.h>

static char buf[128];

static void
call_func(const char *str)
{

    for (uint32_t i = 0; i < array_size(custom_tests); i++) {
	if (!strcmp(custom_tests[i].name, str)) {
	    cprintf("test: %s\n", custom_tests[i].name);
	    custom_tests[i].func();
	}
    }
}

void
console_test(void)
{
    struct u_device_list udl;
    echeck(sys_device_list(&udl));
    
    uint64_t kbd_id = UINT64(~0);
    for (uint64_t i = 0; i < udl.ndev ; i++) {
	if (udl.dev[i].type == device_cons) {
	    kbd_id = udl.dev[i].id;
	    break;
	}
    }

    if (kbd_id == UINT64(~0)) {
	cprintf("console_test: no console found\n");
	return;
    }

    int64_t r;
    echeck(r = sys_device_alloc(core_env->sh, kbd_id, core_env->pid));
  
    struct sobj_ref sg;
    struct cons_entry *ce = 0;
    echeck(segment_alloc(core_env->sh, PGSIZE, &sg, (void **)&ce,
			 0, "console-seg", core_env->pid));
    memset(ce, 0, PGSIZE);
    echeck(sys_device_buf(SOBJ(core_env->sh, r), sg, 0, devio_in));

    cprintf("enter test to run:\n");
    for (uint32_t i = 0; i < array_size(custom_tests); i++)
	cprintf("%s\n", custom_tests[i].name);

    uint32_t j = 0;
    for (uint32_t i = 0; ; i = (i + 1) % (PGSIZE / sizeof(*ce))) {
	if (j == 0) {
	    cprintf(": ");	    
	    cflush();
	}

	while (!ce[i].status);

	cprintf("%c", ce[i].code);
	cflush();
	if (ce[i].code == '\r')
	    cprintf("\n");

	if (ce[i].code == '\r' || ce[i].code == '\n') {
	    call_func(buf);
	    j = 0;
	    buf[0] = 0;
	} else {
	    buf[j++] = ce[i].code;
	    buf[j] = 0;
	}
    }    
}
