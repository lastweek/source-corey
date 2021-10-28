#include <machine/perfmon.h>
#include <machine/x86.h>
#include <kern/lib.h>
#include <inc/error.h>
#include <inc/kdebug.h>

/* AMD vendor code */
enum { 
    amd_ebx = 0x68747541, /* "htuA" */
    amd_edx = 0x69746e65, /* "itne" */
    amd_ecx = 0x444d4163  /* "DMAc" */
};

/* Intel vendor code */
enum { 
    intel_ebx = 0x756e6547, /* "uneG" */
    intel_edx = 0x49656e69,  /* "Ieni" */
    intel_ecx = 0x6c65746e  /* "letn" */
};    

union cpu_sig {
    struct intel_sig {
        uint32_t stepping : 4;
        uint32_t model : 4 ;
        uint32_t family : 4;
	uint32_t processor_type : 2;
        uint32_t pad0 : 2;
	uint32_t ext_model : 4;
        uint32_t ext_family : 8;
	uint32_t pad1 : 4;
    };
    uint32_t signature;
};

static int (*perfmon_setup_pmc)(uint64_t sel, uint64_t val);

static int
amd_set_pmc(uint64_t sel, uint64_t val)
{
    if (sel >= 4)
	return -E_INVAL;
    write_msr(MSR_PERF_SEL0 | sel, 0);
    write_msr(MSR_PERF_CNT0 | sel, 0);
    write_msr(MSR_PERF_SEL0 | sel, val);
    return 0;
}

static int
intel_set_pmc(uint64_t sel, uint64_t val)
{
    if (sel >= 2)
	return -E_INVAL;
    write_msr(MSR_INTEL_ARCH_EVNTSEL0 | sel, 0);
    write_msr(MSR_INTEL_ARCH_PERFCTR0 | sel, 0);
    write_msr(MSR_INTEL_ARCH_EVNTSEL0 | sel, val);
    return 0;
}

void
perfmon_init(void)
{
    if (!perfmon_setup_pmc) {
	uint32_t eax, ebx, ecx, edx;
	
        cpuid(0, 0, &ebx, &ecx, &edx);
        cpuid(0x1, &eax, 0, 0, 0);
        union cpu_sig cpu_sig;
	cpu_sig.signature = eax;

        if (ebx == intel_ebx && ecx == intel_ecx && edx == intel_edx){
	    int model = 0x0F & cpu_sig.model;
	    int family = 0x0F & cpu_sig.family;
	    if (family == 0x6 || family == 0xF)
		model = (cpu_sig.ext_model << 4) + model;
	    if (family == 0x0F)
		family = cpu_sig.ext_family + family;

	    cprintf("Intel family 0x%x model 0x%x\n", family, model);
	    perfmon_setup_pmc = &intel_set_pmc;
	} else if (ebx == amd_ebx && ecx == amd_ecx && edx == amd_edx){           
	    int model = 0x0F & cpu_sig.model;
	    int family = 0x0F & cpu_sig.family;
	    if (family == 0x0F) {
		model = (cpu_sig.ext_model << 4) + model;
		family = cpu_sig.ext_family + family;
	    }

	    cprintf("AMD family 0x%x model 0x%x\n", family, model);
	    perfmon_setup_pmc = &amd_set_pmc;
        } else {
            panic("unknow cpu");
        }
    }

    // enable RDPMC at CPL > 0
    uint64_t cr4 = rcr4();
    lcr4(cr4 | CR4_PCE);
}

int
perfmon_set(uint64_t sel, uint64_t val)
{
    if (val & PES_INT_EN)
        return -E_INVAL;

    return perfmon_setup_pmc(sel, val);
}
