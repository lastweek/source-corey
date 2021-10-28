#define JOBJ(obj)	(obj).share, (obj).object
#define JPTR(ptr)	((uint64_t) (uintptr_t) (ptr))

int
JCALL_NAME(cons_puts)(const char *s, uint64_t size)
{
    return JCALL_FUNC(SYS_cons_puts, JPTR(s), size, 0, 0, 0, 0, 0);
}

int
JCALL_NAME(cons_flush)(void)
{
    return JCALL_FUNC(SYS_cons_flush, 0, 0, 0, 0, 0, 0, 0);
}

int
JCALL_NAME(locality_get)(struct u_locality_matrix *ulm)
{
    return JCALL_FUNC(SYS_locality_get, JPTR(ulm), 0, 0, 0, 0, 0, 0);
}

int
JCALL_NAME(device_list)(struct u_device_list *udl)
{
    return JCALL_FUNC(SYS_device_list, JPTR(udl), 0, 0, 0, 0, 0, 0);
}

int64_t
JCALL_NAME(device_alloc)(uint64_t sh, uint64_t did, proc_id_t pid)
{
    return JCALL_FUNC(SYS_device_alloc, sh, did, pid, 0, 0, 0, 0);
}

int
JCALL_NAME(device_stat)(struct sobj_ref devref, struct u_device_stat *uds)
{
    return JCALL_FUNC(SYS_device_stat, JOBJ(devref), JPTR(uds), 0, 0, 0, 0);
}

int
JCALL_NAME(device_conf)(struct sobj_ref devref, struct u_device_conf *udc)
{
    return JCALL_FUNC(SYS_device_conf, JOBJ(devref), JPTR(udc), 0, 0, 0, 0);
}

int
JCALL_NAME(device_buf)(struct sobj_ref devref, struct sobj_ref sgref, 
		       uint64_t offset, devio_type type)
{
    return JCALL_FUNC(SYS_device_buf, JOBJ(devref), JOBJ(sgref), offset, type, 0);
}

int
JCALL_NAME(obj_get_name)(struct sobj_ref objref, char *name)
{
    return JCALL_FUNC(SYS_obj_get_name, JOBJ(objref), JPTR(name), 0, 0, 0, 0);
}

int
JCALL_NAME(debug)(kdebug_op_t op, uint64_t a0, uint64_t a1, uint64_t a2, 
		  uint64_t a3, uint64_t a4, uint64_t a5)
{
    return JCALL_FUNC(SYS_debug, op, a0, a1, a2, a3, a4, a5);
}

int64_t
JCALL_NAME(share_alloc)(uint64_t sh_id, int mask, const char *name, 
			proc_id_t pid)
{
    return JCALL_FUNC(SYS_share_alloc, sh_id, mask, JPTR(name), pid, 0, 0, 0);
}

int
JCALL_NAME(share_addref)(uint64_t sh_id, struct sobj_ref o)
{
    return JCALL_FUNC(SYS_share_addref, sh_id, JOBJ(o), 0, 0, 0, 0);
}

int
JCALL_NAME(share_unref)(struct sobj_ref o)
{
    return JCALL_FUNC(SYS_share_unref, JOBJ(o), 0, 0, 0, 0, 0);
}

int64_t
JCALL_NAME(segment_alloc)(uint64_t sh_id, uint64_t num_bytes, const char *name, 
			  proc_id_t pid)
{
    return JCALL_FUNC(SYS_segment_alloc, sh_id, num_bytes, JPTR(name), pid, 
		      0, 0, 0);
}

int64_t
JCALL_NAME(segment_copy)(uint64_t sh, struct sobj_ref sgref, const char *name, 
			 page_sharing_mode mode, proc_id_t pid)
{
    return JCALL_FUNC(SYS_segment_copy, sh, JOBJ(sgref), JPTR(name), 
		      SAFE_UNWRAP(mode), pid, 0);
}

int64_t
JCALL_NAME(segment_get_nbytes)(struct sobj_ref sgref)
{
    return JCALL_FUNC(SYS_segment_get_nbytes, JOBJ(sgref), 0, 0, 0, 0, 0);
}

int
JCALL_NAME(segment_set_nbytes)(struct sobj_ref sgref, uint64_t nbytes)
{
    return JCALL_FUNC(SYS_segment_set_nbytes, JOBJ(sgref), nbytes, 0, 0, 0, 0);
}

int64_t 
JCALL_NAME(processor_alloc)(uint64_t sh, const char *name, proc_id_t pid)
{
    return JCALL_FUNC(SYS_processor_alloc, sh, JPTR(name), pid, 0, 0, 0, 0);
}

int64_t 
JCALL_NAME(processor_current)(void)
{
    return JCALL_FUNC(SYS_processor_current, 0, 0, 0, 0, 0, 0, 0);
}

int 
JCALL_NAME(processor_vector)(struct sobj_ref psref, struct u_context *uc)
{
    return JCALL_FUNC(SYS_processor_vector, JOBJ(psref), JPTR(uc), 0, 0, 0, 0);
}

int 
JCALL_NAME(processor_set_interval)(struct sobj_ref psref, uint64_t hz)
{
    return JCALL_FUNC(SYS_processor_set_interval, JOBJ(psref), hz, 0, 0, 0, 0);
}

int 
JCALL_NAME(processor_halt)(struct sobj_ref psref)
{
    return JCALL_FUNC(SYS_processor_halt, JOBJ(psref), 0, 0, 0, 0, 0);
}

int	
JCALL_NAME(processor_set_device)(struct sobj_ref psref, uint64_t i,
				 struct sobj_ref devref)
{
    return JCALL_FUNC(SYS_processor_set_device, JOBJ(psref), i,
		      JOBJ(devref), 0, 0);
}

int	
JCALL_NAME(self_fp_enable)(void)
{
    return JCALL_FUNC(SYS_self_fp_enable, 0, 0, 0, 0, 0, 0, 0);
}

int
JCALL_NAME(self_drop_share)(uint64_t sh_id)
{
    return JCALL_FUNC(SYS_self_drop_share, sh_id, 0, 0, 0, 0, 0, 0);
}

int	
JCALL_NAME(self_get_as)(struct sobj_ref *as)
{
    return JCALL_FUNC(SYS_self_get_as, JPTR(as), 0, 0, 0, 0, 0, 0);
}

int	
JCALL_NAME(self_get_pid)(void)
{
    return JCALL_FUNC(SYS_self_get_pid, 0, 0, 0, 0, 0, 0, 0);
}

int64_t
JCALL_NAME(at_alloc)(uint64_t sh, char interior, const char *name, 
		     proc_id_t pid)
{
    return JCALL_FUNC(SYS_at_alloc, sh, interior, JPTR(name), pid, 0, 0, 0);
}

int
JCALL_NAME(at_get)(struct sobj_ref atref, struct u_address_tree *uat)
{
    return JCALL_FUNC(SYS_at_get, JOBJ(atref), JPTR(uat), 0, 0, 0, 0);
}

int
JCALL_NAME(at_set)(struct sobj_ref atref, struct u_address_tree *uat)
{
    return JCALL_FUNC(SYS_at_set, JOBJ(atref), JPTR(uat), 0, 0, 0, 0);
}

int
JCALL_NAME(at_set_slot)(struct sobj_ref atref, struct u_address_mapping *uam)
{
    return JCALL_FUNC(SYS_at_set_slot, JOBJ(atref), JPTR(uam), 0, 0, 0, 0);
}

int
JCALL_NAME(machine_reinit)(const char *s, uint64_t size)
{
    return JCALL_FUNC(SYS_machine_reinit, JPTR(s), size, 0, 0, 0, 0, 0);	
}
