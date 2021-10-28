#ifndef JOS_DEV_ACPI_H
#define JOS_DEV_ACPI_H

#include <kern/lib.h>

void acpi_init(void);
int  acpi_node_get(struct memory_node *mn, uint32_t n);

#endif
