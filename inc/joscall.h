#include <inc/types.h>
#include <inc/syscall_num.h>
#include <inc/segment.h>
#include <inc/context.h>
#include <inc/locality.h>
#include <inc/share.h>
#include <inc/kdebug.h>
#include <inc/device.h>
#include <inc/copy.h>

uint64_t JCALL_FUNC(uint32_t num, uint64_t a1, uint64_t a2, uint64_t a3,
		    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);

int	JCALL_NAME(cons_puts)(const char *s, uint64_t size);
int	JCALL_NAME(cons_flush)(void);

int     JCALL_NAME(locality_get)(struct u_locality_matrix *ulm);

int	JCALL_NAME(device_list)(struct u_device_list *udl);
int64_t	JCALL_NAME(device_alloc)(uint64_t sh, uint64_t id, proc_id_t pid);
int	JCALL_NAME(device_stat)(struct sobj_ref devref, 
				struct u_device_stat *uds);
int	JCALL_NAME(device_conf)(struct sobj_ref devref, 
				struct u_device_conf *udc);
int	JCALL_NAME(device_buf)(struct sobj_ref devref, struct sobj_ref sgref,
			       uint64_t offset, devio_type type);

int     JCALL_NAME(obj_get_name)(struct sobj_ref obj, char *name);

int	JCALL_NAME(debug)(kdebug_op_t op, uint64_t a0, uint64_t a1, uint64_t a2,
			  uint64_t a3, uint64_t a4, uint64_t a5);

int64_t JCALL_NAME(share_alloc)(uint64_t sh_id, int mask, const char *name, 
				proc_id_t pid);
int	JCALL_NAME(share_addref)(uint64_t sh_id, struct sobj_ref o);
int	JCALL_NAME(share_unref)(struct sobj_ref o);

int64_t JCALL_NAME(segment_alloc)(uint64_t sh_id, uint64_t num_bytes, 
				  const char *name, proc_id_t pid);
int64_t JCALL_NAME(segment_copy)(uint64_t sh_id, struct sobj_ref sgref, 
				 const char *name, page_sharing_mode mode, 
				 proc_id_t pid);
int64_t JCALL_NAME(segment_get_nbytes)(struct sobj_ref sgref);
int     JCALL_NAME(segment_set_nbytes)(struct sobj_ref sgref, uint64_t nbytes);

int64_t JCALL_NAME(processor_alloc)(uint64_t sh, const char *name, 
				    proc_id_t pid);
int64_t JCALL_NAME(processor_current)(void);
int     JCALL_NAME(processor_vector)(struct sobj_ref psref, 
				     struct u_context *uc);
int     JCALL_NAME(processor_set_interval)(struct sobj_ref psref, uint64_t hz);
int	JCALL_NAME(processor_halt)(struct sobj_ref psref);
int	JCALL_NAME(processor_set_device)(struct sobj_ref psref, uint64_t i,
					 struct sobj_ref devref);

int	JCALL_NAME(self_fp_enable)(void);
int	JCALL_NAME(self_drop_share)(uint64_t sh_id);
int	JCALL_NAME(self_get_pid)(void);
int	JCALL_NAME(self_get_as)(struct sobj_ref *asref);

int64_t JCALL_NAME(at_alloc)(uint64_t sh, char interior, const char *name, 
			     proc_id_t pid);
int     JCALL_NAME(at_get)(struct sobj_ref atref, struct u_address_tree *uat);
int     JCALL_NAME(at_set)(struct sobj_ref atref, struct u_address_tree *uat);
int     JCALL_NAME(at_set_slot)(struct sobj_ref atref, 
				struct u_address_mapping *uam);
int	JCALL_NAME(at_add_share)(struct sobj_ref atref, 
				 struct sobj_ref shref);
int 	JCALL_NAME(machine_reinit)(const char *s, uint64_t size);
