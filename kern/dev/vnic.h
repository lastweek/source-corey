#ifndef JOS_DEV_VNIC_H
#define JOS_DEV_VNIC_H

#include <kern/nic.h>
#include <kern/intr.h>

// Max number of virtual nic per one card
#define VNIC_MAX 16

int vnic_attach(struct nic *nic, 
		uint32_t irq, struct interrupt_handler *ih, 
		uint32_t num, 
		int (*mac_index)(void *arg, uint8_t *mac_addr),
		void (*index_mac)(void *arg, int index, uint8_t *mac_addr));

#endif
