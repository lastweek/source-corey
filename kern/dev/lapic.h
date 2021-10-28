#ifndef JOS_DEV_LAPIC_H
#define JOS_DEV_LAPIC_H

// kva of local apic
extern uint8_t *lapic;

void    lapic_init(void);
int32_t lapic_id(void);
void    lapic_startap(uint32_t apicid, physaddr_t pa);
void	lapic_eoi(uint32_t irqno);
void	lapic_print_error(void);
int	lapic_ipi(uint32_t apicid, uint32_t ino);
int	lapic_broadcast(int self, uint32_t ino);

#endif
