#include <machine/mmu.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/jnic.h>
#include <inc/device.h>
#include <inc/error.h>
#include <inc/assert.h>
#include <inc/array.h>

#include <string.h>

enum { max_virt_nic = 16 };

int
jnic_init(struct jnic *jn)
{
    return jn->jn_dev->jdev_init(jn);
}

int 
jnic_mac(struct jnic *jn, uint8_t *mac)
{
    return jn->jn_dev->jdev_mac(jn, mac);    
}

int
jnic_txbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    return jn->jn_dev->jdev_txbuf_next(jn, ret);
}

int
jnic_txbuf_done(struct jnic *jn)
{
    return jn->jn_dev->jdev_txbuf_done(jn);
}

int
jnic_rxbuf_next(struct jnic *jn, struct netbuf_hdr **ret)
{
    return jn->jn_dev->jdev_rxbuf_next(jn, ret);
}

int
jnic_rxbuf_done(struct jnic *jn, struct netbuf_hdr *nb)
{
    return jn->jn_dev->jdev_rxbuf_done(jn, nb);
}

int64_t
jnic_alloc_all(struct jnic *jnic, int n, struct sobj_ref *nic_share)
{
    int64_t r;
    int64_t sh_id;
    sh_id = sys_share_alloc(core_env->sh, 
			    (1 << kobj_device) | (1 << kobj_segment), 
			    "nic-share", 
			    core_env->pid);
    if (sh_id < 0)
	return sh_id;
    struct sobj_ref share = SOBJ(core_env->sh, sh_id);

    struct u_device_list udl;
    r = sys_device_list(&udl);
    if (r < 0)
	goto done;
	
    // for virtual nics
    struct sobj_ref buf_ref;
    void *buf_start = 0;
    int virt_count = 0;
    int jnic_num = 0;

    for (uint64_t i = 0; i < udl.ndev ; i++) {
	if (udl.dev[i].type == device_nic) {
	    if (jnic_num == n) {
		r = -E_NO_SPACE;
		goto done;
	    }

	    int64_t dv;
	    r = sys_device_alloc(sh_id, udl.dev[i].id, core_env->pid);
	    if (r < 0)
		goto done;
	    dv = r;
	    
	    r = jnic_real_setup(&jnic[jnic_num], SOBJ(sh_id, dv));
	    if (r < 0)
		goto done;
	} else if (udl.dev[i].type == device_vnic) {
	    if (jnic_num == n) {
		r = -E_NO_SPACE;
		goto done;
	    }

	    if (!buf_start) {
		// Allocate one big segment for all virtual NICs
		r = jnic_virt_segment(sh_id, max_virt_nic, &buf_start, &buf_ref);
		if (r < 0)
		    goto done;
	    }

	    int64_t dv;
	    r = sys_device_alloc(sh_id, udl.dev[i].id, core_env->pid);
	    if (r < 0)
		goto done;
	    dv = r;
	    
	    r = jnic_virt_setup(&jnic[jnic_num], SOBJ(sh_id, dv), 
				buf_start, buf_ref, virt_count);
	    if (r < 0)
		goto done;

	    virt_count++;
	}
	
	if (udl.dev[i].type == device_vnic || udl.dev[i].type == device_nic)
	    jnic_num++;
    }

 done:
    if (r < 0) {
	sys_share_unref(share);
	return r;
    }

    *nic_share = share;    
    return jnic_num;
}
