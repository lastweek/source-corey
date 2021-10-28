#ifndef JOS_KERN_KCLOCK_H
#define JOS_KERN_KCLOCK_H

void pit_init(void);

unsigned mc146818_read(unsigned reg);
void mc146818_write(unsigned reg, unsigned datum);

#endif
