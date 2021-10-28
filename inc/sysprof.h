#ifndef JOS_INC_SYSPROF_H
#define JOS_INC_SYSPROF_H

typedef enum sysprof_type_enum {
    sysprof_type_cache,
    sysprof_type_refill,
    sysprof_type_bus,
} sysprof_t;

struct sysprof_arg
{
    sysprof_t type;
    union {
	/*
	 * Cache misses
	 */
	struct {
	    uint64_t l1_miss_cnt;
	    uint64_t l2_miss_cnt;
	    uint64_t l3_miss_cnt;
	} gen_cache;
	struct {
	    uint64_t l1_miss_cnt;
	    uint64_t l2_miss_cnt;
	    uint64_t l3_miss_cnt;
	    uint64_t inst_ret;
	} amd_cache;
	struct {
	    uint64_t miss_cnt;
	} intel_cache;

	/*
	 * Cache refills
	 */
	struct {
	    uint64_t cnt;
	} gen_refill;
    };
};

void sysprof_init(void);

uint64_t sysprof_rdpmc(uint32_t n);
void	 sysprof_prog_l3miss(uint32_t n);
void	 sysprof_prog_cmdcnt(uint32_t n);
void	 sysprof_prog_latcnt(uint32_t n);

#endif
