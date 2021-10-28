#ifndef JOS_KERN_DARRAY_H
#define JOS_KERN_DARRAY_H

#include <kern/pagetree.h>
#include <inc/rwlock.h>
#include <inc/copy.h>

struct darray
{
    struct pagetree  da_pt;
    struct rwlock    da_ptl;
    
    proc_id_t	     da_pid;
    
    uint64_t	     da_pp;
    uint64_t	     da_esz;
    uint64_t	     da_nent;

    char	     da_demand;
};

void	 darray_init(struct darray *a, uint64_t ent_size, char on_demand, proc_id_t pid);
void	 darray_free(struct darray *a);
int	 darray_get(struct darray *a, uint64_t i, void **p, page_sharing_mode mode);
int	 darray_set_nent(struct darray *a, uint64_t n, char clear);
int	 darray_grow_nent(struct darray *a, uint64_t n);
uint64_t darray_get_nent(const struct darray *a);
int	 darray_copy(struct darray *src, struct darray *dst, page_sharing_mode mode);

#endif
