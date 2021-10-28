extern "C" {
#include <inc/syscall.h>
#include <inc/stdio.h>
#include <inc/lib.h>
#include <test.h>
#include <inc/jsysmon.h>
#include <machine/x86.h>
}

#include <sys/time.h>
#include <time.h>

#include <inc/error.hh>
#include <inc/jmonitor.hh>

static struct jsm jsm_inst;
static jmonitor the_monitor;

static void
sm_processor_current()
{
	uint64_t curr_proc = 0;
	int i;
	uint64_t start, end, span;
	enum { call_times = 1000 };

	start = read_tsc();
	for (i=0; i<call_times;i++)
	{
	    struct jsm_slot *slot;
	    int r = jsm_next_slot(&jsm_inst, &slot);
	    if (r < 0)
		panic("jsm_next_slot failed: %s", e2s(r));

	    slot->flag |= SYSMON_KERN_RET;
	    jsm_call_processor_current(slot);
	    while (slot->flag & SYSMON_KERN_RET)
		__asm__ volatile ("pause;");
	    curr_proc = slot->ret;
	    jsm_free_slot(slot);
	}
	end = read_tsc();
	span = end - start;
	//548430 cycles on intel 2 core machine
	cprintf("sm_processor_current: %lu cycles with cpu id %ld \n", span,curr_proc);
	
	start = read_tsc();
	for (i=0; i<call_times; i++)
		curr_proc = sys_processor_current();
	end = read_tsc();
	span = end - start;
	//1692174 cycles on intel 2 core machine
	cprintf("sys_processor_current: %lu cycles with cpu id %ld \n", span,curr_proc);
}

void
monitor_test(void)
{
    struct u_device_list udl;
    error_check(sys_device_list(&udl));	
    int64_t sm_dv = 0;
    for (uint64_t i = 0; i < udl.ndev ; i++) {
	if (udl.dev[i].type == device_sysmon) {
	    error_check(sm_dv = sys_device_alloc(core_env->sh, 
						 udl.dev[i].id, core_env->pid));
	    break;
	}
    }
    if (!sm_dv)
	panic("no SYSMON present");
    the_monitor.add_device(SOBJ(core_env->sh,sm_dv));
    the_monitor.start(1);
    jsm_setup(&jsm_inst, SOBJ(core_env->sh,sm_dv));
    sm_processor_current();
}
