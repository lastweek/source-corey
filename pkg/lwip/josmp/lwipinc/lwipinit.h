#ifndef JOS_LWIP_LWIPINIT_H
#define JOS_LWIP_LWIPINIT_H

struct jnic;

void lib_lwip_init(void (*cb)(uint32_t, void *), void *cbarg, struct jnic *jnic, 
		   uint32_t ipaddr, uint32_t netmask, uint32_t gw)
     __attribute__((noreturn));

#endif
