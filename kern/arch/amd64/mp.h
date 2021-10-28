#ifndef JOS_MACHINE_MP_H
#define JOS_MACHINE_MP_H

#include <machine/types.h>

struct mp_intr {
    uint8_t type;		// entry type (3)
    uint8_t intr_type;
    uint16_t intr_flag;
    uint8_t src_bus_id;
    uint8_t src_bus_irq;
    uint8_t dst_id;
    uint8_t dst_intin;
};

extern struct mp_intr mp_iointr[256];
extern struct mp_intr mp_lintr[256];

void mp_init(void);

#endif
