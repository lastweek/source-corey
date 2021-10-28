#ifndef JOS_INC_LIB_H
#define JOS_INC_LIB_H

#include <inc/segment.h>
#include <inc/proc.h>
#include <inc/intmacro.h>
#include <inc/fs.h>
#include <machine/atomic.h>

// rand.c
uint64_t  jrand(void);
void	  jrand_init(void);
void	  jrand_seed(uint64_t *seed, int n);

// pfork.c
#define PFORK_SHARE_HEAP 0x0001
#define PFORK_COR	 0x0002
int64_t   pfork(proc_id_t pid);
int64_t	  pforkv(proc_id_t pid, uint64_t flags, 
		 struct sobj_ref *shares, uint32_t n);

// processor.c
struct sobj_ref  processor_current(void);
proc_id_t	 processor_current_procid(void);
struct sobj_ref	 processor_current_as(void);
int		 processor_get_as(struct sobj_ref psref, struct sobj_ref *asref);
void		 processor_halt(void) __attribute__((noreturn));
uint64_t	 processor_ncpu(void);

// as.c
void	  as_init(void);
int       as_map(struct sobj_ref sgref, uint64_t start_byteoff, uint64_t flags, 
		 void **va_p, uint64_t *bytes_store);
int	  as_unmap(void *va);
int	  as_lookup(void *va, struct u_address_mapping *uam);
int	  as_set_utrap(void *entry, void *stack_base, void *stack_top);
void	  as_print_uas(struct u_address_tree *at);
void	  as_print_current_uas(void);
int	  at_map_interior(struct sobj_ref intref, uint64_t flags, void *va);

// segment.c
int	  segment_alloc(uint64_t sh, uint64_t bytes, 
			struct sobj_ref *sg, void **va_p, 
			uint32_t flags, const char *name, proc_id_t pid);
// libmain.c
typedef struct {
    // default share
    uint64_t sh;
    // pcore ref
    struct sobj_ref psref;
    // physical core id
    proc_id_t pid;
    
    // fs data
    struct sobj_ref mtab;
    struct fs_handle rhand;
    struct fs_handle cwd;
    
    // estimated cpu frequency
    uint64_t cpufreq;
} core_env_t;

extern core_env_t *core_env;
extern const char *boot_args;

void	  libmain(uintptr_t mainfunc) __attribute__((noreturn));
void	  setup_env(uint64_t sh_id);

// debug.cc
void	  print_backtrace(void);

// time.c
#define NSEC_PER_SECOND           UINT64(1000000000)
uint64_t  time_nsec(void);
void	  time_init(uint64_t hz);
uint64_t  time_cycles_to_nsec(uint64_t cycles);
void	  time_delay_cycles(uint64_t c);

// ummap.c
void  ummap_alloc_init(void);
int   ummap_finit(void);
void *u_mmap(void *addr, size_t len,...);
int   u_munmap(void  *addr, size_t len,...);
void *u_mremap (void *__addr, size_t __old_len, size_t __new_len, int __flags,...);
void ummap_get_shref(struct sobj_ref **sh, uint64_t *n);

typedef struct {
    uint64_t last;
} memusage_t;

void ummap_init_usage(memusage_t *usage);
void ummap_print_usage(memusage_t *usage);
uint64_t ummap_prefault(uint64_t segsize);

// string.c
int strtou64(const char *begin, char **endptr, int base, uint64_t *return_value);

#endif
